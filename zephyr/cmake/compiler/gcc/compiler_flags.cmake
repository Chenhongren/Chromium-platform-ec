# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Zephyr cmake system looks into ${TOOLCHAIN_ROOT}, but we just send
# this out to the copy in ${ZEPHYR_BASE}.
include("${ZEPHYR_BASE}/cmake/compiler/gcc/compiler_flags.cmake")

# gcc flags for coverage generation
set_compiler_property(PROPERTY coverage -fprofile-arcs -ftest-coverage -fno-inline)
