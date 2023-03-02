/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Misc utilities for fingerprint management */

#ifndef __CROS_EC_FPSENSOR_UTILS_H
#define __CROS_EC_FPSENSOR_UTILS_H

#define CPRINTF(format, args...) cprintf(CC_FP, format, ##args)
#define CPRINTS(format, args...) cprints(CC_FP, format, ##args)

/**
 * Test that size+offset does not exceed buffer_size
 *
 * Returns:
 *   EC_ERROR_OVERFLOW: if size+offset does not fit in uint32_t
 *   EC_ERROR_INVAL: if size+offset > buffer_size
 *   EC_SUCCESS: otherwise
 */
int validate_fp_buffer_offset(uint32_t buffer_size, uint32_t offset,
			      uint32_t size);
#endif /* __CROS_EC_FPSENSOR_UTILS_H */