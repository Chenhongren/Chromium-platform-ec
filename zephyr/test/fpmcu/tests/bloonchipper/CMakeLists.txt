# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources_ifdef(CONFIG_TEST_BOARD_TRANSPORT_SPI app PRIVATE
	src/transport_spi.c
	${PLATFORM_EC_PROGRAM_DIR}/fpmcu/${CONFIG_TEST_BOARD_NAME}/src/fp_transport.c)

target_sources_ifdef(CONFIG_TEST_BOARD_TRANSPORT_UART app PRIVATE
	src/transport_uart.c
	${PLATFORM_EC_PROGRAM_DIR}/fpmcu/${CONFIG_TEST_BOARD_NAME}/src/fp_transport.c)
