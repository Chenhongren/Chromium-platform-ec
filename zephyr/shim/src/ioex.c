/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifdef __REQUIRE_ZEPHYR_GPIOS__
#undef __REQUIRE_ZEPHYR_GPIOS__
#endif
#include "common.h"
#include "ioexpander.h"

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ioex_shim, LOG_LEVEL_ERR);

int ioex_init(int ioex)
{
	const struct ioexpander_drv *drv = ioex_config[ioex].drv;
	int rv;

	if (ioex_config[ioex].flags & IOEX_FLAGS_INITIALIZED)
		return EC_SUCCESS;

	if (drv->init != NULL) {
		rv = drv->init(ioex);
		if (rv != EC_SUCCESS)
			return rv;
	}

	ioex_config[ioex].flags |= IOEX_FLAGS_INITIALIZED;

	return EC_SUCCESS;
}

static int ioex_init_default(void)
{
	int ret;
	int i;

	for (i = 0; i < CONFIG_IO_EXPANDER_PORT_COUNT; i++) {
		/* IO Expander has been initialized, skip re-initializing */
		if (ioex_config[i].flags &
		    (IOEX_FLAGS_INITIALIZED | IOEX_FLAGS_DEFAULT_INIT_DISABLED))
			continue;

		ret = ioex_init(i);
		if (ret)
			LOG_ERR("Can't initialize ioex %d", i);
	}

	return 0;
}
SYS_INIT(ioex_init_default, POST_KERNEL, CONFIG_PLATFORM_EC_IOEX_INIT_PRIORITY);
