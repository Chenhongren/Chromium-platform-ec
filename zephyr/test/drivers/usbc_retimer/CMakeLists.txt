# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources_ifdef(CONFIG_LINK_TEST_SUITE_USBC_RETIMER_ANX7483 app PRIVATE src/anx7483.c)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_USBC_RETIMER_PS8811 app PRIVATE src/ps8811.c)
