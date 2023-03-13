#include "usb_hid_touchpad.h"

#undef CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_PRESSURE
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_PRESSURE 255

#undef CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X 2925

#undef CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y 1426

#undef CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X 929

#undef CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y 457

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


#define REPORT_ID_TOUCHPAD 0x01
#define MAX_FINGERS 5

static const uint8_t REPORT_DESC[] = {
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

	0xC0, /* End Collection */
};

const static uint16_t HID_DESC[] = {
	0x1E,
	0x100,
	sizeof(REPORT_DESC), /* report desc length */
	0x02,
	0x03,
	sizeof(struct usb_hid_touchpad_report),
	0x04,
	0,
	0x05,
	0x06,
	0x18D1, /* VID */
	0x5087, /* PID */
	0x100,
	0,
	0,
};

