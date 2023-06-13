/* Copyright 2023 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/uart.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include "console.h"
#include "drivers/cros_one_wire_uart.h"
#include "keyboard_scan.h"
#include "touchpad.h"
#include "usb_hid_touchpad.h"
#include "update_fw.h"
#include "system.h"
#include "ec_commands.h"
#include "flash.h"
#include "hooks.h"
#include "rwsig.h"

const static struct device *one_wire_uart = DEVICE_DT_GET(DT_NODELABEL(one_wire_uart));

/* TODO: move to header */
int elan_tp_set_power(int);

#define RX_IDLE 0
#define RX_INSIDE_BLOCK 1
#define RX_OUTSIDE_BLOCK 2

static void jump_to_rw(void)
{
	system_run_image_copy(EC_IMAGE_RW);
}
DECLARE_DEFERRED(jump_to_rw);

static int try_vendor_command(const uint8_t *buffer, size_t count)
{
	struct update_frame_header *cmd_buffer = (void *)buffer;
	int rv = 0;

	if (sys_be32_to_cpu(cmd_buffer->cmd.block_base) != UPDATE_EXTRA_CMD)
		return 0;

	if (sys_be32_to_cpu(cmd_buffer->block_size) != count) {
		ccprints("%s: problem: block size and count mismatch (%d != %d)",
			__func__, sys_be32_to_cpu(cmd_buffer->block_size), count);
		return 0;
	}

	/* Looks like this is a vendor command, let's verify it. */
	if (update_pdu_valid(&cmd_buffer->cmd,
			     count - offsetof(struct update_frame_header,
					      cmd))) {
		enum update_extra_command subcommand;
		uint8_t response;
		size_t response_size = sizeof(response);
		int __attribute__((unused)) header_size;
		int __attribute__((unused)) data_count;

		/* looks good, let's process it. */
		rv = 1;

		subcommand = sys_be16_to_cpu(*((uint16_t *)(cmd_buffer + 1)));

		/*
		 * header size: update frame header + 2 bytes for subcommand
		 * data_count: Some commands take in extra data as parameter
		 */
		header_size = sizeof(*cmd_buffer) + sizeof(uint16_t);
		data_count = count - header_size;

		switch (subcommand) {
		case UPDATE_EXTRA_CMD_IMMEDIATE_RESET:
			ccprints("Rebooting!");
			ccprints("\n\n");
			cflush();
			system_reset(SYSTEM_RESET_MANUALLY_TRIGGERED);
			/* Unreachable, unless something bad happens. */
			response = EC_RES_ERROR;
			break;
		case UPDATE_EXTRA_CMD_JUMP_TO_RW:
#ifdef CONFIG_RWSIG
			/*
			 * Tell rwsig task to jump to RW. This does nothing if
			 * verification failed, and will only jump later on if
			 * verification is still in progress.
			 */
			rwsig_continue();

			switch (rwsig_get_status()) {
			case RWSIG_VALID:
				response = EC_RES_SUCCESS;
				break;
			case RWSIG_INVALID:
				response = EC_RES_INVALID_CHECKSUM;
				break;
			case RWSIG_IN_PROGRESS:
				response = EC_RES_IN_PROGRESS;
				break;
			default:
				response = EC_RES_ERROR;
			}
#else
			hook_call_deferred(&jump_to_rw_data, 100 * MSEC);
#endif
			break;
#ifdef CONFIG_RWSIG
		case UPDATE_EXTRA_CMD_STAY_IN_RO:
			rwsig_abort();
			response = EC_RES_SUCCESS;
			break;
#endif
		case UPDATE_EXTRA_CMD_UNLOCK_RW:
			crec_flash_set_protect(EC_FLASH_PROTECT_RW_AT_BOOT, 0);
			response = EC_RES_SUCCESS;
			break;
#ifdef CONFIG_ROLLBACK
		case UPDATE_EXTRA_CMD_UNLOCK_ROLLBACK:
			crec_flash_set_protect(
				EC_FLASH_PROTECT_ROLLBACK_AT_BOOT, 0);
			response = EC_RES_SUCCESS;
			break;
#ifdef CONFIG_ROLLBACK_SECRET_SIZE
#ifdef CONFIG_ROLLBACK_UPDATE
		case UPDATE_EXTRA_CMD_INJECT_ENTROPY: {
			if (data_count < CONFIG_ROLLBACK_SECRET_SIZE) {
				ccprints("Entropy too short");
				response = EC_RES_INVALID_PARAM;
				break;
			}

			ccprints("Adding %db of entropy", data_count);
			/* Add the entropy to secret. */
			rollback_add_entropy(buffer + header_size, data_count);
			break;
		}
#endif /* CONFIG_ROLLBACK_UPDATE */
#ifdef CONFIG_USB_PAIRING
		case UPDATE_EXTRA_CMD_PAIR_CHALLENGE: {
			if (data_count < sizeof(struct pair_challenge)) {
				ccprints("Challenge data too short");
				response = EC_RES_INVALID_PARAM;
				break;
			}

			/* pair_challenge takes care of answering */
			return pair_challenge((
				struct pair_challenge *)(buffer + header_size));
		}
#endif
#endif /* CONFIG_ROLLBACK_SECRET_SIZE */
#endif /* CONFIG_ROLLBACK */
		default:
			response = EC_RES_INVALID_COMMAND;
		}

		cros_one_wire_uart_send(one_wire_uart, ROACH_CMD_UPDATER_COMMAND, &response, response_size);
	}

	return rv;
}

void mock_updater(const uint8_t *payload, int len)
{
	static int state = RX_IDLE;
	static uint8_t data_was_transferred = 0;
	static uint8_t
		block_buffer[sizeof(struct update_command) + CONFIG_UPDATE_PDU_SIZE];
	static int block_size;
	static int block_index;

	union {
		struct update_frame_header upfr;
		struct {
			uint32_t unused;
			struct first_response_pdu startup_resp;
		};
	} u;

	memcpy(&u, payload, MIN(len, sizeof(u)));

	ccprints("\x1b[1;31mupdater command state %d len=%d\x1b[m", state, len);
	if (state == RX_IDLE) {
		size_t resp_size;

		if (try_vendor_command(payload, len)) {
			return;
		}

		if (sys_be32_to_cpu(u.upfr.block_size) !=
			    sizeof(struct update_frame_header) ||
		    u.upfr.cmd.block_digest != 0 ||
		    u.upfr.cmd.block_base != 0) {
			/*
			 * Something is wrong, this payload is not a valid
			 * update start PDU. Let'w indicate this by returning
			 * a single byte error code.
			 */
			ccprints("FW update: invalid start.");
			return;
		}

		ccprints("FW update: starting...");
		fw_update_command_handler(
			&u.upfr.cmd,
			len - offsetof(struct update_frame_header, cmd),
			&resp_size);

		if (!u.startup_resp.return_value) {
			state = RX_OUTSIDE_BLOCK; /* We're in business. */
			data_was_transferred = 0; /* No data received yet. */
		}

		for (int i = 0; i < resp_size; i++) {
			ccprintf("%02x ", ((uint8_t*)&u.startup_resp)[i]);
		}
		ccprintf("\n");
		/* Let the host know what updater had to say. */
		cros_one_wire_uart_send(one_wire_uart, ROACH_CMD_UPDATER_COMMAND, (uint8_t*)&u.startup_resp, resp_size);
		return;

	} else if (state == RX_OUTSIDE_BLOCK) {
		if (len == 4) {
			uint32_t command = sys_be32_to_cpu(*(uint32_t*)payload);
			if (command == UPDATE_DONE) {
				ccprints("FW update: done");

				if (data_was_transferred) {
					fw_update_complete();
					data_was_transferred = 0;
				}

				uint8_t resp_value = 0;
				cros_one_wire_uart_send(one_wire_uart, ROACH_CMD_UPDATER_COMMAND, &resp_value, 1);
				state = RX_IDLE;
				return;
			}
		}

		/* Let's allocate a large enough buffer. */
		block_size = sys_be32_to_cpu(u.upfr.block_size) -
			     offsetof(struct update_frame_header, cmd);

		/*
		 * Only update start PDU is allowed to have a size 0 payload.
		 */
		if (block_size <= sizeof(struct update_command) ||
		    block_size > sizeof(block_buffer)) {
			uint8_t err = UPDATE_GEN_ERROR;
			ccprints("Invalid block size (%d).", block_size);
			cros_one_wire_uart_send(one_wire_uart, ROACH_CMD_UPDATER_COMMAND, &err, 1);
			return;
		}

		/*
		 * Copy the rest of the message into the block buffer to pass
		 * to the updater.
		 */
		block_index = sizeof(u.upfr) -
			      offsetof(struct update_frame_header, cmd);
		memcpy(block_buffer, &u.upfr.cmd, block_index);
		block_size -= block_index;
		state = RX_INSIDE_BLOCK;
		return;
	} else {

		/* TODO: check buffer size */
		memcpy(block_buffer + block_index, payload, len);
		block_index += len;
		block_size -= len;
		if (block_size > 0) {
			if (len <= sizeof(u.upfr)) {
				uint8_t err = UPDATE_GEN_ERROR;
				/*
				 * A block header size instead of chunk size message
				 * has been received, let's abort the transfer.
				 */
				ccprints("Unexpected header");
				cros_one_wire_uart_send(one_wire_uart, ROACH_CMD_UPDATER_COMMAND, &err, 1);
				return;
			}
			return;
		}

		data_was_transferred = 1;
		/*
		fw_update_command_handler(block_buffer, block_index, &resp_size);
		*/
		block_buffer[0] = 0;
		cros_one_wire_uart_send(one_wire_uart, ROACH_CMD_UPDATER_COMMAND, &block_buffer[0], 1);
		state = RX_OUTSIDE_BLOCK;
	}
}

void board_process_packet(const struct RoachMessage *msg)
{
	if (msg->header.cmd == ROACH_CMD_SUSPEND) {
		elan_tp_set_power(0);
	}
	if (msg->header.cmd == ROACH_CMD_RESUME) {
		elan_tp_set_power(1);
	}

	if (msg->header.cmd == ROACH_CMD_UPDATER_COMMAND) {
		mock_updater(msg->payload, msg->header.payload_len);
	}
}

static int ec_ec_comm_init(void)
{
	cros_one_wire_uart_enable(DEVICE_DT_GET(DT_NODELABEL(one_wire_uart)));

	uint8_t lcr_cache = *(volatile uint8_t*)0xf02803;
	*(volatile uint8_t*)0xf02803 |= 0x80; /* access divisor latches */
	*(volatile uint8_t*)0xf02800 = 0x01; /* set divisor = 0x8001 */
	*(volatile uint8_t*)0xf02801 = 0x80;
	*(volatile uint8_t*)0xf02808 = 2; /* high speed select */
	*(volatile uint8_t*)0xf02803 = lcr_cache;

	return 0;
}
SYS_INIT(ec_ec_comm_init, APPLICATION, 50);

void keyboard_state_changed(int row, int col, int is_pressed)
{
}

void detachable_keyboard_add(const uint8_t *state)
{
	cros_one_wire_uart_send(one_wire_uart, ROACH_CMD_KEYBOARD_MATRIX, state, KEYBOARD_COLS_MAX);
}

void board_touchpad_reset(void)
{
}

void set_touchpad_report(struct usb_hid_touchpad_report *report)
{
	cros_one_wire_uart_send(one_wire_uart, ROACH_CMD_TOUCHPAD_REPORT, (uint8_t*)report, sizeof(*report));
}
