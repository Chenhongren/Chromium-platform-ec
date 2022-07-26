/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include "accelgyro.h"
#include "cros_board_info.h"
#include "hooks.h"
#include "motionsense_sensors.h"

#include "nissa_common.h"

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

/*
 * Mainboard orientation support.
 */

#define ALT_MAT SENSOR_ROT_STD_REF_NAME(DT_NODELABEL(base_rot_ver1))
#define BASE_SENSOR SENSOR_ID(DT_NODELABEL(base_accel))
#define BASE_GYRO SENSOR_ID(DT_NODELABEL(base_gyro))

static void form_factor_init(void)
{
	int ret;
	uint32_t val;
	/*
	 * If the board version is 1
	 * use ver1 rotation matrix.
	 */
	ret = cbi_get_board_version(&val);
	if (ret == EC_SUCCESS && val == 1) {
		LOG_INF("Switching to ver1 base");
		motion_sensors[BASE_SENSOR].rot_standard_ref = &ALT_MAT;
		motion_sensors[BASE_GYRO].rot_standard_ref = &ALT_MAT;
	}
}
DECLARE_HOOK(HOOK_INIT, form_factor_init, HOOK_PRIO_POST_I2C);