# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Create library name based on current directory
zephyr_library_get_current_dir_lib_name(${ZEPHYR_BASE} lib_name)

# Create interface library
zephyr_interface_library_named(${lib_name})

# Add source files
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_VDM_AP_CONTROL "${CMAKE_CURRENT_SOURCE_DIR}/src/ap_vdm_control.c")
zephyr_library_sources_ifndef(CONFIG_PLATFORM_EC_USB_PD_VDM_AP_CONTROL "${CMAKE_CURRENT_SOURCE_DIR}/src/ap_vdm_control_disabled.c")

# Link in the library
zephyr_library_link_libraries(${lib_name})
