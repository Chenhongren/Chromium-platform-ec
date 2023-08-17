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

void usb_dc_tp_init(int max_x, int max_y, int physical_x, int physical_y,
            int max_pressure);

struct tp_report_desc_para {
    uint16_t logical_max_x;
    uint16_t logical_max_y;
    int physical_max_x;
    int physical_max_y;
    uint16_t logical_max_pressure;
};
int get_tp_report_desc_param(struct tp_report_desc_para *params);


#endif
