# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Add source files
target_sources(app
  PRIVATE
    src/test_charge_state.c
    src/test_common_charger.c
    src/test_common_charger_mocked.c
)
