# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cros_ec_library_include_directories_ifdef(CONFIG_BOARD_MARKARTH include)
zephyr_library_sources(
		"src/usb_mux_config.c"
		"src/ppc_config.c"
		"src/form_factor.c"
		"src/keyboard.c"
		"src/kb_backlight.c"
		"src/thermal.c"
)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
	"src/keyboard.c"
	"src/keyboard_customization.c"
)
