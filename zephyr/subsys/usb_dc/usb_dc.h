/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB DC Shimming Definitions.
 */

#ifndef __USB_DC_H
#define __USB_DC_H

#include "common.h"

#include <zephyr/usb/usb_ch9.h>

bool check_usb_is_suspended(void);
bool check_usb_is_configured(void);

/**
 * @brief Request usb wake-up
 *
 * @return true if wake up successfully, false otherwise
 */
bool request_usb_wake(void);

/**
 * @brief Check if usb hid boot protocol is set
 *
 * @return true if boot protocol is set, false otherwise
 */
bool boot_proto_is_set(void);

/**
 * @brief Callback function for usb hid protocol change
 */
void protocol_cb(const struct device *dev, uint8_t protocol);

#endif
