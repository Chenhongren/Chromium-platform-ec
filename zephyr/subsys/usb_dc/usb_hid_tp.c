/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "clock.h"
#include "common.h"
#include "config.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "link_defs.h"
#include "queue.h"
#include "registers.h"
#include "task.h"
#include "util.h"
#include "usb_dc.h"

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/usb/usb_dc.h>
#include <zephyr/usb/class/usb_hid.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(usb_hid_tp, LOG_LEVEL_INF);

#define REPORT_ID_TOUCHPAD 0x01
#define REPORT_ID_MOUSE 0x02
#define REPORT_ID_DEVICE_CAPS 0x0A
#define REPORT_ID_DEVICE_CERT 0x0B

#define MAX_FINGERS 5

struct usb_hid_touchpad_report {
	uint8_t id; /* 0x01 */
	struct {
		uint16_t confidence : 1;
		uint16_t tip : 1;
		uint16_t inrange : 1;
		uint16_t id : 4;
		uint16_t pressure : 9;
		uint16_t width : 12;
		uint16_t height : 12;
		uint16_t x : 12;
		uint16_t y : 12;
	} __packed finger[MAX_FINGERS];
	uint8_t count : 7;
	uint8_t button : 1;
	uint16_t timestamp;
} __packed;

static const struct device *hid_dev;
static const int touchpad_debug;
static struct queue const report_queue = QUEUE_NULL(8, struct usb_hid_touchpad_report);
static struct k_mutex *report_queue_mutex;

#define HID_TOUCHPAD_REPORT_SIZE sizeof(struct usb_hid_touchpad_report)

/* Touchpad interface, firmware size and physical dimension. */
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X 2644
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y 1440
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_PRESSURE 511
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X 839 /* tenth of mm */
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y 457 /* tenth of mm */
#define CONFIG_TOUCHPAD_VIRTUAL_SIZE (64 * 1024)

#define FINGER_USAGE                                                           \
		0x05, 0x0D, /*   Usage Page (Digitizer) */                             \
		0x09, 0x22, /*   Usage (Finger) */                             \
		0xA1, 0x02, /*   Collection (Logical) */                       \
		0x09, 0x47, /*     Usage (Confidence) */                       \
		0x09, 0x42, /*     Usage (Tip Switch) */                       \
		0x09, 0x32, /*     Usage (In Range) */                         \
		0x15, 0x00, /*     Logical Minimum (0) */                      \
		0x25, 0x01, /*     Logical Maximum (1) */                      \
		0x75, 0x01, /*     Report Size (1) */                          \
		0x95, 0x03, /*     Report Count (3) */                         \
		0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
		0x09, 0x51, /*     Usage (0x51) Contact identifier */          \
		0x75, 0x04, /*     Report Size (4) */                          \
		0x95, 0x01, /*     Report Count (1) */                         \
		0x25, 0x0F, /*     Logical Maximum (15) */                     \
		0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
		0x05, 0x0D, /*     Usage Page (Digitizer) */ /*     Logical    \
								Maximum of     \
								Pressure */    \
		0x26, (CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_PRESSURE & 0xFF),   \
		(CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_PRESSURE >> 8), 0x75,     \
		0x09, /*     Report Size (9) */                                \
		0x09, 0x30, /*     Usage (Tip pressure) */                     \
		0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
		0x26, 0xFF, 0x0F, /*     Logical Maximum (4095) */             \
		0x75, 0x0C, /*     Report Size (12) */                         \
		0x09, 0x48, /*     Usage (WIDTH) */                            \
		0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
		0x09, 0x49, /*     Usage (HEIGHT) */                           \
		0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
		0x05, 0x01, /*     Usage Page (Generic Desktop Ctrls) */       \
		0x75, 0x0C, /*     Report Size (12) */                         \
		0x55, 0x0E, /*     Unit Exponent (-2) */                       \
		0x65, 0x11, /*     Unit (System: SI Linear, Length: cm) */     \
		0x09, 0x30, /*     Usage (X) */                                \
		0x35, 0x00, /*     Physical Minimum (0) */                     \
		0x26, (CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X & 0xff),          \
		(CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X >> 8), /*     Logical   \
								 Maximum */    \
		0x46, (CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X & 0xff),         \
		(CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X >> 8), /*     Physical \
								  Maximum      \
								  (tenth of    \
								  mm) */       \
		0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
		0x26, (CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y & 0xff),          \
		(CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y >> 8), /*     Logical   \
								 Maximum */    \
		0x46, (CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y & 0xff),         \
		(CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y >> 8), /*     Physical \
								  Maximum      \
								  (tenth of    \
								  mm) */       \
		0x09, 0x31, /*     Usage (Y) */                                \
		0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
		0xC0 /*   End Collection */

/*
 * HID: Report Descriptor
 * TODO(b/35582031): There are ways to reduce flash usage, as the
 * Finger Usage is repeated 5 times.
 */
static const uint8_t report_desc[] = {
	/* Touchpad Collection */
	0x05, 0x0D, /* Usage Page (Digitizer) */
	0x09, 0x05, /* Usage (Touch Pad) */
	0xA1, 0x01, /* Collection (Application) */
	0x85, REPORT_ID_TOUCHPAD, /*   Report ID (1, Touch) */
	/* Finger 0 */
	FINGER_USAGE,
	/* Finger 1 */
	FINGER_USAGE,
	/* Finger 2 */
	FINGER_USAGE,
	/* Finger 3 */
	FINGER_USAGE,
	/* Finger 4 */
	FINGER_USAGE,
	/* Contact count */
	0x05, 0x0D, /*   Usage Page (Digitizer) */
	0x09, 0x54, /*   Usage (Contact count) */
	0x25, MAX_FINGERS, /*   Logical Maximum (MAX_FINGERS) */
	0x75, 0x07, /*   Report Size (7) */
	0x95, 0x01, /*   Report Count (1) */
	0x81, 0x02, /*   Input (Data,Var,Abs) */
	/* Button */
	0x05, 0x01, /*   Usage Page(Generic Desktop Ctrls) */
	0x05, 0x09, /*   Usage (Button) */
	0x19, 0x01, /*   Usage Minimum (0x01) */
	0x29, 0x01, /*   Usage Maximum (0x01) */
	0x15, 0x00, /*   Logical Minimum (0) */
	0x25, 0x01, /*   Logical Maximum (1) */
	0x75, 0x01, /*   Report Size (1) */
	0x95, 0x01, /*   Report Count (1) */
	0x81, 0x02, /*   Input (Data,Var,Abs) */
	/* Timestamp */
	0x05, 0x0D, /*   Usage Page (Digitizer) */
	0x55, 0x0C, /*   Unit Exponent (-4) */
	0x66, 0x01, 0x10, /*   Unit (Seconds) */
	0x47, 0xFF, 0xFF, 0x00, 0x00, /*   Physical Maximum (65535) */
	0x27, 0xFF, 0xFF, 0x00, 0x00, /*   Logical Maximum (65535) */
	0x75, 0x10, /*   Report Size (16) */
	0x95, 0x01, /*   Report Count (1) */
	0x09, 0x56, /*   Usage (0x56, Relative Scan Time) */
	0x81, 0x02, /*   Input (Data,Var,Abs) */

	0x85, REPORT_ID_DEVICE_CAPS, /*   Report ID (Device Capabilities) */
	0x09, 0x55, /*   Usage (Contact Count Maximum) */
	0x09, 0x59, /*   Usage (Pad Type) */
	0x25, 0x0F, /*   Logical Maximum (15) */
	0x75, 0x08, /*   Report Size (8) */
	0x95, 0x02, /*   Report Count (2) */
	0xB1, 0x02, /*   Feature (Data,Var,Abs) */

	/* Page 0xFF, usage 0xC5 is device certificate. */
	0x06, 0x00, 0xFF, /*   Usage Page (Vendor Defined) */
	0x85, REPORT_ID_DEVICE_CERT, /*   Report ID (Device Certification) */
	0x09, 0xC5, /*   Usage (Vendor Usage 0xC5) */
	0x15, 0x00, /*   Logical Minimum (0) */
	0x26, 0xFF, 0x00, /*   Logical Maximum (255) */
	0x75, 0x08, /*   Report Size (8) */
	0x96, 0x00, 0x01, /*   Report Count (256) */
	0xB1, 0x02, /*   Feature (Data,Var,Abs) */

	0xC0, /* End Collection */
};

static void write_tp_report(struct usb_hid_touchpad_report *report)
{
	if (touchpad_debug) {
		LOG_DBG("[%5d][size:%d]:Button[%d] - X:%4d, Y:%4d, "
		"Width:%4d, Height:%4d, Tipswitch:%d, Pressure:%d\r\n",
		report->timestamp, sizeof(*report), report->button,
		report->finger[0].x, report->finger[0].y,
		report->finger[0].width, report->finger[0].height,
		report->finger[0].tip, report->finger[0].pressure);
	}

	int ret = hid_int_ep_write(hid_dev, (uint8_t *)report,
			sizeof(*report), NULL);

	if (ret) {
		LOG_ERR("hid tp write error, %d", ret);
	}
}

static void hid_tp_proc_queue(void);
DECLARE_DEFERRED(hid_tp_proc_queue);

static void hid_tp_proc_queue(void)
{
	struct usb_hid_touchpad_report report;

	mutex_lock(report_queue_mutex);

	if (queue_is_empty(&report_queue)) {
		mutex_unlock(report_queue_mutex);
		return;
	}

	queue_peek_units(&report_queue, &report, 0, 1);

	write_tp_report(&report);

	queue_advance_head(&report_queue, 1);

	mutex_unlock(report_queue_mutex);
	hook_call_deferred(&hid_tp_proc_queue_data, 1 * MSEC);
}

void set_touchpad_report(struct usb_hid_touchpad_report *report)
{
	static int print_full = 1;

	if (!check_usb_is_configured()) {
		return;
	}

	mutex_lock(report_queue_mutex);

	if (!check_usb_is_suspended()) {
		if (queue_is_empty(&report_queue)) {
			write_tp_report(report);
			mutex_unlock(report_queue_mutex);
			return;
		}
	} else {
		if (!request_usb_wake()) {
			return;
		}
	}

	if (queue_is_full(&report_queue)) {
		if (print_full)
			LOG_WRN("TP queue full\n");
		print_full = 0;

		queue_advance_head(&report_queue, 1);
	} else {
		print_full = 1;
	}
	queue_add_unit(&report_queue, report);

	mutex_unlock(report_queue_mutex);

	hook_call_deferred(&hid_tp_proc_queue_data, 0);
}

static int usb_hid_tp_init(void)
{
	hid_dev = device_get_binding("HID_1");

	if (hid_dev == NULL) {
		LOG_ERR("Cannot get USB HID Device");
		return 0;
	}

	usb_hid_register_device(hid_dev,
				report_desc, sizeof(report_desc),
				NULL);

	usb_hid_init(hid_dev);

	return 0;

}
SYS_INIT(usb_hid_tp_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
