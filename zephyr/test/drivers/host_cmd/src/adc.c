/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "host_command.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, adc_read_channel, enum adc_channel);

ZTEST(hc_adc, test_normal_path)
{
	struct ec_params_adc_read params = {
		.adc_channel = ADC_TEMP_SENSOR_CHARGER,
	};
	struct ec_response_adc_read response;
	uint16_t ret;

	adc_read_channel_fake.return_val = 123;

	ret = ec_cmd_adc_read(NULL, &params, &response);

	zassert_ok(ret, "Host command returned %u", ret);
	zassert_equal(1, adc_read_channel_fake.call_count);
	zassert_equal(123, response.adc_value);
}

ZTEST(hc_adc, test_bad_ch_number)
{
	struct ec_params_adc_read params = {
		.adc_channel = ADC_CH_COUNT + 1, /* Invalid */
	};
	struct ec_response_adc_read response;
	uint16_t ret;

	ret = ec_cmd_adc_read(NULL, &params, &response);

	zassert_equal(EC_RES_INVALID_PARAM, ret, "Host command returned %u",
		      ret);
}

static void reset(void *data)
{
	ARG_UNUSED(data);

	RESET_FAKE(adc_read_channel);
}

ZTEST_SUITE(hc_adc, drivers_predicate_post_main, NULL, reset, reset, NULL);
