/* Copyright 2023 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <stdint.h>

enum RoachCommand {
	ROACH_CMD_KEYBOARD_MATRIX,
	ROACH_CMD_TOUCHPAD_REPORT,
	ROACH_CMD_SUSPEND,
	ROACH_CMD_RESUME,
	ROACH_CMD_UPDATER_COMMAND,
};

struct RoachMsgHeader {
	uint8_t magic;
	uint8_t payload_len;
	uint8_t checksum;
	uint8_t cmd: 6;
	uint8_t sender: 2;
	uint8_t msg_id: 7;
	uint8_t ack: 1;
} __packed;

#define HEADER_SIZE sizeof(struct RoachMsgHeader)
_Static_assert(HEADER_SIZE == 5);

#define ROACH_MAX_MESSAGE_SIZE 256

struct RoachMessage {
	struct RoachMsgHeader header;
	uint8_t payload[ROACH_MAX_MESSAGE_SIZE - HEADER_SIZE];
} __packed;

_Static_assert(sizeof(struct RoachMessage) == ROACH_MAX_MESSAGE_SIZE);

void cros_one_wire_uart_send(const struct device *device, enum RoachCommand cmd, const uint8_t* payload, int size);

void board_process_packet(const struct RoachMessage *msg);

void cros_one_wire_uart_enable(const struct device *dev);
