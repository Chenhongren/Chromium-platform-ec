/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi.h"
#include "hooks.h"

#include <stdbool.h>

#include <zephyr/drivers/gpio.h>

#include <ap_power/ap_power.h>

/* Enable/Disable keyboard backlight gpio */
static inline void kbd_backlight_enable(bool enable)
{
	if (get_board_id() == 1)
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_id_1_ec_kb_bl_en),
				enable);
	else
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_kb_bl_en_l),
				!enable);
}

static void board_backlight_handler(struct ap_power_ev_callback *cb,
				    struct ap_power_ev_data data)
{
	bool enable;

	switch (data.event) {
	default:
		return;

	case AP_POWER_RESUME:
		/* Called on AP S3 -> S0 transition */
		enable = true;
		break;

	case AP_POWER_SUSPEND:
		/* Called on AP S0 -> S3 transition */
		enable = false;
		break;
	}
	kbd_backlight_enable(enable);
}

/*
 * Explicitly apply the board ID 1 *gpio.inc settings to pins that
 * were reassigned on current boards.
 */
static void set_board_id_1_gpios(void)
{
	static struct ap_power_ev_callback cb;
	const struct gpio_dt_spec *ec_kb_bl_en =
		GPIO_DT_FROM_NODELABEL(gpio_id_1_ec_kb_bl_en);
	gpio_flags_t flags = ec_kb_bl_en->dt_flags;

	/*
	 * Add a callback for suspend/resume to
	 * control the keyboard backlight.
	 */
	ap_power_ev_init_callback(&cb, board_backlight_handler,
				  AP_POWER_RESUME | AP_POWER_SUSPEND);
	ap_power_ev_add_callback(&cb);

	if (get_board_id() != 1)
		return;

	/*
	 * Note, use gpio_pin_configure() instead of gpio_pin_configure_dt()
	 * because gpio_pin_configure_dt() only sets additional flags, and
	 * cannot clear flags.
	 */
	flags &= ~GPIO_INPUT;
	flags |= GPIO_OUTPUT_LOW;
	gpio_pin_configure(ec_kb_bl_en->port, ec_kb_bl_en->pin, flags);
}
DECLARE_HOOK(HOOK_INIT, set_board_id_1_gpios, HOOK_PRIO_FIRST);
