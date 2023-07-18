/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "hooks.h"
#include "keyboard_config.h"
#include "queue.h"
#include "task.h"
#include "usb_dc.h"
#include "usb_hid.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>

LOG_MODULE_DECLARE(usb_hid_kb, LOG_LEVEL_INF);

/* TODO: pull bland_kb and board_vivaldi_keybd_config to board code */
static const struct ec_response_keybd_config bland_kb = {
	.num_top_row_keys = 10,
	.action_keys = {
		TK_BACK,
		TK_REFRESH,
		TK_FULLSCREEN,
		TK_OVERVIEW,
		TK_BRIGHTNESS_DOWN,
		TK_BRIGHTNESS_UP,
		TK_MICMUTE,
		TK_VOL_MUTE,
		TK_VOL_DOWN,
		TK_VOL_UP,
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	return &bland_kb;
}

/* TODO: add this configuration to shim code and enable it in board code */
#define CONFIG_USB_HID_KEYBOARD_VIVALDI

/* Supported function key range */
#define HID_F1 0x3a
#define HID_F12 0x45
#define HID_F13 0x68
#define HID_F15 0x6a

#define HID_KEYBOARD_MODIFIER_LOW 0xe0
#define HID_KEYBOARD_MODIFIER_HIGH 0xe7
#define HID_KEYBOARD_ASSISTANT_KEY 0xf0

/* Special keys/switches */
#define HID_KEYBOARD_EXTRA_LOW 0xf0
#define HID_KEYBOARD_ASSISTANT_KEY 0xf0
#define HID_KEYBOARD_TABLET_MODE_SWITCH 0xf1
#define HID_KEYBOARD_EXTRA_HIGH 0xf1

#define CONFIG_USB_HID_KB_NUM_TOP_ROW_KEYS 10

struct action_key_config {
	uint32_t mask; /* bit position of usb_hid_keyboard_report.top_row */
	uint32_t usage; /*usage ID */
};

static const struct action_key_config action_key[] = {
	[TK_BACK] = { .mask = BIT(0), .usage = 0x000C0224 },
	[TK_FORWARD] = { .mask = BIT(1), .usage = 0x000C0225 },
	[TK_REFRESH] = { .mask = BIT(2), .usage = 0x000C0227 },
	[TK_FULLSCREEN] = { .mask = BIT(3), .usage = 0x000C0232 },
	[TK_OVERVIEW] = { .mask = BIT(4), .usage = 0x000C029F },
	[TK_BRIGHTNESS_DOWN] = { .mask = BIT(5), .usage = 0x000C0070 },
	[TK_BRIGHTNESS_UP] = { .mask = BIT(6), .usage = 0x000C006F },
	[TK_VOL_MUTE] = { .mask = BIT(7), .usage = 0x000C00E2 },
	[TK_VOL_DOWN] = { .mask = BIT(8), .usage = 0x000C00EA },
	[TK_VOL_UP] = { .mask = BIT(9), .usage = 0x000C00E9 },
	[TK_SNAPSHOT] = { .mask = BIT(10), .usage = 0x00070046 },
	[TK_PRIVACY_SCRN_TOGGLE] = { .mask = BIT(11), .usage = 0x000C02D0 },
	[TK_KBD_BKLIGHT_DOWN] = { .mask = BIT(12), .usage = 0x000C007A },
	[TK_KBD_BKLIGHT_UP] = { .mask = BIT(13), .usage = 0x000C0079 },
	[TK_PLAY_PAUSE] = { .mask = BIT(14), .usage = 0x000C00CD },
	[TK_NEXT_TRACK] = { .mask = BIT(15), .usage = 0x000C00B5 },
	[TK_PREV_TRACK] = { .mask = BIT(16), .usage = 0x000C00B6 },
	[TK_KBD_BKLIGHT_TOGGLE] = { .mask = BIT(17), .usage = 0x000C007C },
	[TK_MICMUTE] = { .mask = BIT(18), .usage = 0x000B002F },
};

/* TK_* is 1-indexed, so the next bit is at ARRAY_SIZE(action_key) - 1 */
static const int SLEEP_KEY_MASK = BIT(ARRAY_SIZE(action_key) - 1);

#define REPORT_ID_1 0x01

#ifdef CONFIG_USB_HID_KEYBOARD_VIVALDI
#define FEATURE_REPORT_SIZE \
	sizeof(uint32_t) * CONFIG_USB_HID_KB_NUM_TOP_ROW_KEYS + 1
static uint8_t feature_report[FEATURE_REPORT_SIZE];

static void hid_keyboard_feature_init(void)
{
	const struct ec_response_keybd_config *config =
		board_vivaldi_keybd_config();

	feature_report[0] = REPORT_ID_1;
	for (int i = 0; i < CONFIG_USB_HID_KB_NUM_TOP_ROW_KEYS; i++) {
		int key = config->action_keys[i];

		if (IN_RANGE(key, 0, ARRAY_SIZE(action_key) - 1))
			memcpy(feature_report + i * sizeof(uint32_t) + 1,
			       &action_key[key].usage, sizeof(uint32_t));
	}
}
DECLARE_HOOK(HOOK_INIT, hid_keyboard_feature_init, HOOK_PRIO_DEFAULT - 1);
#endif

#define KEYBOARD_BASE_DESC                                                    \
	HID_USAGE_PAGE(HID_USAGE_GEN_DESKTOP),                                \
		HID_USAGE(HID_USAGE_GEN_DESKTOP_KEYBOARD),                    \
		HID_COLLECTION(HID_COLLECTION_APPLICATION),                   \
		/* Report ID byte */                                          \
		HID_REPORT_ID(REPORT_ID_1),                                   \
		/* Modifier byte */                                           \
		HID_USAGE_PAGE(HID_USAGE_GEN_DESKTOP_KEYPAD),                 \
		HID_USAGE_MIN8(HID_KEYBOARD_MODIFIER_LOW),                    \
		HID_USAGE_MAX8(HID_KEYBOARD_MODIFIER_HIGH),                   \
		HID_LOGICAL_MIN8(0x00), HID_LOGICAL_MAX8(0x01),               \
		HID_REPORT_SIZE(1), HID_REPORT_COUNT(8),                      \
		HID_INPUT(0x02),                                              \
		/* Six-keycode bytes */                                       \
		HID_REPORT_COUNT(6), HID_REPORT_SIZE(8),                      \
		HID_LOGICAL_MIN8(0x0), HID_LOGICAL_MAX8(0xa4),                \
		HID_USAGE_MIN8(0x00), HID_USAGE_MAX8(0xa4), HID_INPUT(0x00),

#define KEYBOARD_TOP_ROW_DESC                                                 \
	/* Modifiers */                                                       \
	0x05, 0x0C, /* Consumer Page */                                       \
		0x0A, 0x24, 0x02, /* AC Back (0x224) */                       \
		0x0A, 0x25, 0x02, /* AC Forward (0x225) */                    \
		0x0A, 0x27, 0x02, /* AC Refresh (0x227) */                    \
		0x0A, 0x32, 0x02, /* AC View Toggle (0x232) */                \
		0x0A, 0x9F, 0x02, /* AC Desktop Show All windows (0x29F) */   \
		0x09, 0x70, /* Display Brightness Decrement (0x70) */         \
		0x09, 0x6F, /* Display Brightness Increment (0x6F) */         \
		0x09, 0xE2, /* Mute (0xE2) */                                 \
		0x09, 0xEA, /* Volume Decrement (0xEA) */                     \
		0x09, 0xE9, /* Volume Increment (0xE9) */                     \
		0x0B, 0x46, 0x00, 0x07, 0x00, /* PrintScreen (Page 0x7, Usage \
						 0x46) */                     \
		0x0A, 0xD0, 0x02, /* Privacy Screen Toggle (0x2D0) */         \
		0x09, 0x7A, /* Keyboard Brightness Decrement (0x7A) */        \
		0x09, 0x79, /* Keyboard Brightness Increment (0x79)*/         \
		0x09, 0xCD, /* Play / Pause (0xCD) */                         \
		0x09, 0xB5, /* Scan Next Track (0xB5) */                      \
		0x09, 0xB6, /* Scan Previous Track (0xB6) */                  \
		0x09, 0x7C, /* Keyboard Backlight OOC (0x7C) */               \
		0x0B, 0x2F, 0x00, 0x0B, 0x00, /* Phone Mute (Page 0xB, Usage  \
						 0x2F) */                     \
		0x09, 0x32, /* Sleep (0x32) */                                \
		0x15, 0x00, /* Logical Minimum (0) */                         \
		0x25, 0x01, /* Logical Maximum (1) */                         \
		0x75, 0x01, /* Report Size (1) */                             \
		0x95, 0x14, /* Report Count (20) */                           \
		0x81, 0x02, /* Input (Data, Variable, Absolute), ;Modifier    \
			       byte */                                        \
                                                                              \
		/* 12-bit padding */                                          \
		0x95, 0x0C, /* Report Count (12) */                           \
		0x75, 0x01, /* Report Size (1) */                             \
		0x81, 0x01, /* Input (Constant), ;1-bit padding */

#define KEYBOARD_TOP_ROW_FEATURE_DESC                                         \
	0x06, 0xd1, 0xff, /* Usage Page (Google) */                           \
		0x09, 0x01, /* Usage (Top Row List) */                        \
		0xa1, 0x02, /* Collection (Logical) */                        \
		0x05, 0x0a, /*   Usage Page (Ordinal) */                      \
		0x19, 0x01, /*   Usage Minimum (1) */                         \
		0x29, CONFIG_USB_HID_KB_NUM_TOP_ROW_KEYS, /* Usage Maximum */ \
		0x95, CONFIG_USB_HID_KB_NUM_TOP_ROW_KEYS, /* Report Count */  \
		0x75, 0x20, /*   Report Size (32) */                          \
		0xb1, 0x03, /*   Feature (Cnst,Var,Abs) */                    \
		0xc0, /* End Collection */

/*
 * Vendor-defined Usage Page 0xffd1:
 *  - 0x18: Assistant key
 *  - 0x19: Tablet mode switch
 */
#ifdef HID_KEYBOARD_EXTRA_FIELD
#ifdef CONFIG_KEYBOARD_ASSISTANT_KEY
#define KEYBOARD_ASSISTANT_KEY_DESC                                        \
	0x19, 0x18, /* Usage Minimum */                                    \
		0x29, 0x18, /* Usage Maximum */                            \
		0x15, 0x00, /* Logical Minimum (0) */                      \
		0x25, 0x01, /* Logical Maximum (1) */                      \
		0x75, 0x01, /* Report Size (1) */                          \
		0x95, 0x01, /* Report Count (1) */                         \
		0x81, 0x02, /* Input (Data, Variable, Absolute), ;Modifier \
			       byte */
#else
/* No assistant key: just pad 1 bit. */
#define KEYBOARD_ASSISTANT_KEY_DESC               \
	0x95, 0x01, /* Report Count (1) */        \
		0x75, 0x01, /* Report Size (1) */ \
		0x81, 0x01, /* Input (Constant), ;1-bit padding */
#endif /* !CONFIG_KEYBOARD_ASSISTANT_KEY */

#ifdef CONFIG_KEYBOARD_TABLET_MODE_SWITCH
#define KEYBOARD_TABLET_MODE_SWITCH_DESC                                   \
	0x19, 0x19, /* Usage Minimum */                                    \
		0x29, 0x19, /* Usage Maximum */                            \
		0x15, 0x00, /* Logical Minimum (0) */                      \
		0x25, 0x01, /* Logical Maximum (1) */                      \
		0x75, 0x01, /* Report Size (1) */                          \
		0x95, 0x01, /* Report Count (1) */                         \
		0x81, 0x02, /* Input (Data, Variable, Absolute), ;Modifier \
			       byte */
#else
/* No tablet mode swtch: just pad 1 bit. */
#define KEYBOARD_TABLET_MODE_SWITCH_DESC          \
	0x95, 0x01, /* Report Count (1) */        \
		0x75, 0x01, /* Report Size (1) */ \
		0x81, 0x01, /* Input (Constant), ;1-bit padding */
#endif /* CONFIG_KEYBOARD_TABLET_MODE_SWITCH */

#define KEYBOARD_VENDOR_DESC                                                 \
	0x06, 0xd1, 0xff, /* Usage Page (Vendor-defined 0xffd1) */           \
                                                                             \
		KEYBOARD_ASSISTANT_KEY_DESC KEYBOARD_TABLET_MODE_SWITCH_DESC \
                                                                             \
		0x95,                                                        \
		0x01, /* Report Count (1) */                                 \
		0x75, 0x06, /* Report Size (6) */                            \
		0x81, 0x01, /* Input (Constant), ;6-bit padding */
#endif /* HID_KEYBOARD_EXTRA_FIELD */

#define KEYBOARD_BACKLIGHT_DESC                                       \
	0xA1, 0x02, /* Collection (Logical) */                        \
		0x05, 0x14, /*   Usage Page (Alphanumeric Display) */ \
		0x09, 0x46, /*   Usage (Display Brightness) */        \
		0x95, 0x01, /*   Report Count (1) */                  \
		0x75, 0x08, /*   Report Size (8) */                   \
		0x15, 0x00, /*   Logical Minimum (0) */               \
		0x25, 0x64, /*   Logical Maximum (100) */             \
		0x91, 0x02, /*   Output (Data, Variable, Absolute) */ \
		0xC0, /* End Collection */

/* HID : Report Descriptor */
static const uint8_t hid_report_desc[] = {
	KEYBOARD_BASE_DESC

#ifdef KEYBOARD_VENDOR_DESC
	KEYBOARD_VENDOR_DESC
#endif

#ifdef CONFIG_USB_HID_KEYBOARD_VIVALDI
	KEYBOARD_TOP_ROW_DESC KEYBOARD_TOP_ROW_FEATURE_DESC
#endif
	HID_END_COLLECTION
};

/* The standard Chrome OS keyboard matrix table. See HUT 1.12v2 Table 12 and
 * https://www.w3.org/TR/DOM-Level-3-Events-code .
 *
 * Assistant key is mapped as 0xf0, but this key code is never actually send.
 */
const uint8_t keycodes[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	{ 0x00, 0x00, 0xe0, 0xe3, 0xe4, HID_KEYBOARD_ASSISTANT_KEY, 0x00,
	  0x00 },
	{ 0xe3, 0x29, 0x2b, 0x35, 0x04, 0x1d, 0x1e, 0x14 },
	{ 0x3a, 0x3d, 0x3c, 0x3b, 0x07, 0x06, 0x20, 0x08 },
	{ 0x05, 0x0a, 0x17, 0x22, 0x09, 0x19, 0x21, 0x15 },
	{ 0x43, 0x40, 0x3f, 0x3e, 0x16, 0x1b, 0x1f, 0x1a },
	{ 0x87, 0x00, 0x30, 0x00, 0x0e, 0x36, 0x25, 0x0c },
	{ 0x11, 0x0b, 0x1c, 0x23, 0x0d, 0x10, 0x24, 0x18 },
	{ 0x00, 0x00, 0x64, 0x00, 0x00, 0xe1, 0x00, 0xe5 },
	{ 0x2e, 0x34, 0x2F, 0x2d, 0x33, 0x38, 0x27, 0x13 },
	{ 0x00, 0x42, 0x41, 0x68, 0x0f, 0x37, 0x26, 0x12 },
	{ 0xe6, 0x00, 0x89, 0x00, 0x31, 0x00, 0xe2, 0x00 },
	{ 0x00, 0x2a, 0x00, 0x31, 0x28, 0x2c, 0x51, 0x52 },
	{ 0x00, 0x8a, 0x00, 0x8b, 0x00, 0x00, 0x4f, 0x50 },
};

/*
 * Note: This first 8 bytes of this report format cannot be changed, as that
 * would break HID Boot protocol compatibility (see HID 1.11 "Appendix B: Boot
 * Interface Descriptors").
 */
struct usb_hid_keyboard_report {
	union {
		uint8_t boot_modifiers; /* boot protocol */
		uint8_t report_id; /* report protocol */
	} byte_0;

	union {
		uint8_t reserved; /* boot protocol */
		uint8_t report_modifiers; /* report protocol */
	} byte_1;

	uint8_t keys[6];
	/* Non-boot protocol fields below */
#ifdef HID_KEYBOARD_EXTRA_FIELD
	/* Assistant/tablet mode switch bitmask */
	uint8_t extra;
#endif
#ifdef CONFIG_USB_HID_KEYBOARD_VIVALDI
	uint32_t top_row; /* bitmap of top row action keys */
#endif
} __packed;

static int kb_get_report(const struct device *dev,
			 struct usb_setup_packet *setup, int32_t *len,
			 uint8_t **data);

static const struct hid_ops ops = {
	.protocol_change = protocol_cb,
	.get_report = kb_get_report,
};

static struct queue const report_queue =
	QUEUE_NULL(32, struct usb_hid_keyboard_report);
static struct k_mutex *report_queue_mutex;
static struct usb_hid_keyboard_report report;

static const struct device *hid_dev;

static int kb_get_report(const struct device *dev,
			 struct usb_setup_packet *setup, int32_t *len,
			 uint8_t **data)
{
	/* The wValue field specifies the report id in the low byte*/
	if ((setup->wValue & 0xFF) != REPORT_ID_1) {
		LOG_ERR("unknown report id");
		return -ENOTSUP;
	}

	/* The report type is in the high byte */
	switch ((setup->wValue & 0xFF00) >> 8) {
	case REPORT_TYPE_INPUT:
		*data = (uint8_t *)&report;
		*len = sizeof(report);
		return 0;
	case REPORT_TYPE_FEATURE:
#ifdef CONFIG_USB_HID_KEYBOARD_VIVALDI
		*data = (uint8_t *)feature_report;
		*len = FEATURE_REPORT_SIZE;
		return 0;
#endif
	case REPORT_TYPE_OUTPUT:
	default:
		break;
	}
	return -ENOTSUP;
}

static uint32_t maybe_convert_function_key(int keycode)
{
	const struct ec_response_keybd_config *config =
		board_vivaldi_keybd_config();
	/* zero-based function key index (e.g. F1 -> 0) */
	int index;

	if (!IS_ENABLED(CONFIG_USB_HID_KEYBOARD_VIVALDI) || !config)
		return 0;

	if (IN_RANGE(keycode, HID_F1, HID_F12))
		index = keycode - HID_F1;
	else if (IN_RANGE(keycode, HID_F13, HID_F15))
		index = keycode - HID_F13 + 12;
	else
		return 0; /* not a function key */

	/* convert F13 to Sleep */
	if (index == 12 && (config->capabilities & KEYBD_CAP_SCRNLOCK_KEY))
		return SLEEP_KEY_MASK;

	if (index >= config->num_top_row_keys ||
	    config->action_keys[index] == TK_ABSENT)
		return 0; /* not mapped */
	return action_key[config->action_keys[index]].mask;
}

static void hid_kb_proc_queue(void);
DECLARE_DEFERRED(hid_kb_proc_queue);

void keyboard_state_changed(int row, int col, int is_pressed)
{
	static int print_full = 1;
	static uint8_t modifiers = 0x0;
	bool valid = 0;
	uint8_t mask;
	int i;
	uint32_t action_key_mask;
	uint8_t keycode = keycodes[col][row];
	if (!keycode) {
		LOG_ERR("Unknown key at %d/%d\n", row, col);
		return;
	}

	action_key_mask = maybe_convert_function_key(keycode);

	if (action_key_mask) {
#ifdef CONFIG_USB_HID_KEYBOARD_VIVALDI
		if (is_pressed)
			report.top_row |= action_key_mask;
		else
			report.top_row &= ~action_key_mask;
		valid = 1;
#endif
	} else if (keycode >= HID_KEYBOARD_EXTRA_LOW &&
		   keycode <= HID_KEYBOARD_EXTRA_HIGH) {
#ifdef HID_KEYBOARD_EXTRA_FIELD
		mask = 0x01 << (keycode - HID_KEYBOARD_EXTRA_LOW);
		if (is_pressed)
			report.extra |= mask;
		else
			report.extra &= ~mask;
		valid = 1;
#endif
	} else if (keycode >= HID_KEYBOARD_MODIFIER_LOW &&
		   keycode <= HID_KEYBOARD_MODIFIER_HIGH) {
		mask = 0x01 << (keycode - HID_KEYBOARD_MODIFIER_LOW);
		if (is_pressed)
			modifiers |= mask;
		else
			modifiers &= ~mask;
		valid = 1;
	} else if (is_pressed) {
		/*
		 * Add keycode to the list of keys (does nothing if the
		 * array is already full).
		 */
		for (i = 0; i < ARRAY_SIZE(report.keys); i++) {
			/* Is key already pressed? */
			if (report.keys[i] == keycode)
				break;
			if (report.keys[i] == 0) {
				report.keys[i] = keycode;
				valid = 1;
				break;
			}
		}
	} else {
		/*
		 * Remove keycode from the list of keys (does nothing
		 * if the key is not in the array).
		 */
		for (i = 0; i < ARRAY_SIZE(report.keys); i++) {
			if (report.keys[i] == keycode) {
				report.keys[i] = 0;
				valid = 1;
				break;
			}
		}
	}
	if (valid) {
		if (!check_usb_is_configured()) {
			return;
		}

		if (check_usb_is_suspended()) {
			if (!request_usb_wake()) {
				return;
			}
		}

		if (boot_proto_is_set()) {
			report.byte_0.boot_modifiers = modifiers;
			report.byte_1.reserved = 0x0;
		} else {
			report.byte_0.report_id = REPORT_ID_1;
			report.byte_1.report_modifiers = modifiers;
		}

		mutex_lock(report_queue_mutex);
		if (queue_is_full(&report_queue)) {
			if (print_full)
				LOG_WRN("KB queue full\n");
			print_full = 0;

			queue_advance_head(&report_queue, 1);
		} else {
			print_full = 1;
		}
		queue_add_unit(&report_queue, &report);
		mutex_unlock(report_queue_mutex);

		hook_call_deferred(&hid_kb_proc_queue_data, 0);
	}
}

static void hid_kb_proc_queue(void)
{
	struct usb_hid_keyboard_report kb_data;
	int ret;
	size_t size;

	mutex_lock(report_queue_mutex);

	/* clear queue if the usb dc status is reset or disconected */
	if (!check_usb_is_configured() && !check_usb_is_suspended()) {
		queue_remove_units(&report_queue, NULL,
				   queue_count(&report_queue));
		mutex_unlock(report_queue_mutex);
		return;
	}

	if (queue_is_empty(&report_queue)) {
		mutex_unlock(report_queue_mutex);
		return;
	}

	queue_peek_units(&report_queue, &kb_data, 0, 1);

	size = boot_proto_is_set() ? 8 : sizeof(struct usb_hid_keyboard_report);
	ret = hid_int_ep_write(hid_dev, (uint8_t *)&kb_data, size, NULL);
	if (ret) {
		LOG_INF("hid kb write error, %d", ret);
	} else {
		queue_advance_head(&report_queue, 1);
	}

	mutex_unlock(report_queue_mutex);
	hook_call_deferred(&hid_kb_proc_queue_data, 1 * MSEC);
}

static int usb_hid_kb_init(void)
{
	hid_dev = device_get_binding("HID_0");
	if (!hid_dev) {
		LOG_ERR("Cannot get USB HID Device");
		return 1;
	}

	usb_hid_register_device(hid_dev, hid_report_desc,
				sizeof(hid_report_desc), &ops);

	if (usb_hid_set_proto_code(hid_dev, HID_BOOT_IFACE_CODE_KEYBOARD)) {
		LOG_WRN("failed to set interface protocol code");
	}
	usb_hid_init(hid_dev);

	return 0;
}
SYS_INIT(usb_hid_kb_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
