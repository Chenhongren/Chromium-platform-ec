/* Copyright 2023 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

#include "ap_power/ap_power.h"
#include "console.h"
#include "hooks.h"
#include "keyboard_mkbp.h"
#include "mkbp_event.h"
#include "mkbp_fifo.h"
#include "drivers/one_wire_uart.h"
#include "usb_hid_touchpad.h"
#include "roach_cmds.h"

#define UPDATER_RESPONSE_SIZE_MAX 64

K_MSGQ_DEFINE(touchpad_report_queue, sizeof(struct usb_hid_touchpad_report), 16, 4);
K_MSGQ_DEFINE(usb_updater_queue, UPDATER_RESPONSE_SIZE_MAX, 16, 4);

const static struct device *one_wire_uart = DEVICE_DT_GET(DT_NODELABEL(one_wire_uart));

void board_process_packet(uint8_t cmd, uint8_t *payload, int length)
{
	if (cmd == ROACH_CMD_KEYBOARD_MATRIX) {
		mkbp_keyboard_add(payload);
	}
	if (cmd == ROACH_CMD_TOUCHPAD_REPORT) {
		k_msgq_put(&touchpad_report_queue, payload, K_NO_WAIT);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(ec_ap_hid_int_odl), 0);
	}
	if (cmd == ROACH_CMD_UPDATER_COMMAND) {
		uint8_t buf[UPDATER_RESPONSE_SIZE_MAX];

		if (length >= UPDATER_RESPONSE_SIZE_MAX) {
			return;
		}
		buf[0] = length;
		memcpy(buf + 1, payload, length);
		k_msgq_put(&usb_updater_queue, buf, K_MSEC(1));
	}
}

static void base_shutdown_hook(struct ap_power_ev_callback *cb,
			      struct ap_power_ev_data data)
{
	switch (data.event) {
	case AP_POWER_SHUTDOWN:
		one_wire_uart_send(one_wire_uart, ROACH_CMD_SUSPEND, NULL, 0);
		break;
	case AP_POWER_STARTUP:
		one_wire_uart_send(one_wire_uart, ROACH_CMD_RESUME, NULL, 0);
		break;
	default:
		return;
	}
}

static int ec_ec_comm_init(void)
{
	static struct ap_power_ev_callback cb;

	ap_power_ev_init_callback(&cb, base_shutdown_hook, AP_POWER_STARTUP | AP_POWER_SHUTDOWN);
	ap_power_ev_add_callback(&cb);

	one_wire_uart_set_callback(one_wire_uart, board_process_packet);
	one_wire_uart_enable(one_wire_uart);

	i2c_target_driver_register(DEVICE_DT_GET(DT_NODELABEL(hid_i2c_target)));

	/* UART1PMR */
	*(volatile uint8_t*)0xf03a23 = 1;

	uint8_t lcr_cache = *(volatile uint8_t*)0xf02803;
	*(volatile uint8_t*)0xf02803 |= 0x80; /* access divisor latches */
	*(volatile uint8_t*)0xf02800 = 0x01; /* set divisor = 0x8001 */
	*(volatile uint8_t*)0xf02801 = 0x80;
	*(volatile uint8_t*)0xf02808 = 2; /* high speed select */
	*(volatile uint8_t*)0xf02803 = lcr_cache;

	return 0;
}
SYS_INIT(ec_ec_comm_init, APPLICATION, 1);
