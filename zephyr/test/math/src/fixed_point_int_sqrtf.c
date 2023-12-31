/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "math.h"
#include "math_util.h"

#include <zephyr/ztest.h>

ZTEST_USER(math, test_int_sqrtf_negative)
{
	zassert_equal(int_sqrtf(-100), 0);
}

ZTEST_USER(math, test_int_sqrtf_overflow)
{
	zassert_equal(int_sqrtf(INT64_MAX), INT32_MAX);
}
