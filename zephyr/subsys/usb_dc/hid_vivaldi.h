/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __HID_VIVALDI_H
#define __HID_VIVALDI_H

#include "ec_commands.h"

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
		0x81, 0x01 /* Input (Constant), ;1-bit padding */

#define KEYBOARD_TOP_ROW_FEATURE_DESC                                          \
	0x06, 0xd1, 0xff, /* Usage Page (Google) */                            \
		0x09, 0x01, /* Usage (Top Row List) */                         \
		0xa1, 0x02, /* Collection (Logical) */                         \
		0x05, 0x0a, /*   Usage Page (Ordinal) */                       \
		0x19, 0x01, /*   Usage Minimum (1) */                          \
		0x29, CONFIG_USB_DC_KEYBOARD_NUM_TOP_ROW_KEYS, /* Usage        \
								       Maximum \
								     */        \
		0x95, CONFIG_USB_DC_KEYBOARD_NUM_TOP_ROW_KEYS, /* Report       \
								       Count   \
								     */        \
		0x75, 0x20, /*   Report Size (32) */                           \
		0xb1, 0x03, /*   Feature (Cnst,Var,Abs) */                     \
		0xc0 /* End Collection */

int32_t get_vivaldi_feature_report(uint8_t report_id, uint8_t *data);
uint32_t vivaldi_convert_function_key(int keycode);

__overridable const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	return NULL;
}

#endif /* __HID_VIVALDI_H */
