/* Copyright 2023 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_one_wire_uart

#include <zephyr/drivers/uart.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

#include "console.h"
#include "drivers/cros_one_wire_uart.h"
#include "hooks.h"
#include "queue.h"
#include "touchpad.h"

K_MSGQ_DEFINE(rx_queue, sizeof(struct RoachMessage), 16, 4);
K_MSGQ_DEFINE(tx_queue, sizeof(struct RoachMessage), 16, 4);
static int ack = -1;
static int pending_ack = -1;

struct one_wire_uart_config {
	const struct device *bus;
	int id;
	const struct deferred_data *schedule_tx_deferred;
};

struct one_wire_uart_data {
};

static uint8_t checksum(const struct RoachMessage* msg)
{
	uint8_t sum = 0;
	const uint8_t *raw = (const uint8_t*)msg;
	int size = ROACH_HEADER_SIZE + msg->header.payload_len;

	for (int i = 0; i < size; i++) {
		sum += raw[i];
	}

	return (uint8_t)(-sum);
}

void cros_one_wire_uart_send(const struct device *dev, enum RoachCommand cmd,
			     const uint8_t* payload, int size)
{
	static unsigned int msg_id = 0;

	struct RoachMessage msg;
	const struct one_wire_uart_config *config = dev->config;
	const struct device *bus = config->bus;

	if (size > ROACH_MAX_PAYLOAD_SIZE) {
		return;
	}

	msg.header = (struct RoachMsgHeader){
		.magic = 0xEC,
		.payload_len = size,
		.cmd = cmd,
		.sender = config->id,
		.msg_id = msg_id++ % 128,
		.ack = 0,
		.checksum = 0,
	};

	memcpy(msg.payload, payload, size);
	msg.header.checksum = checksum(&msg);

	if (k_msgq_put(&tx_queue, &msg, K_NO_WAIT) == 0) {
		uart_irq_tx_enable(bus);
	} else {
		ccprints("tx queue full!");
	}
}

static void process_packet(void);
DECLARE_DEFERRED(process_packet);

static void process_packet(void)
{
	struct RoachMessage msg;
	static int last_msg_id = -1;

	while (k_msgq_get(&rx_queue, &msg, K_NO_WAIT) == 0) {
		if (last_msg_id != msg.header.msg_id) {
			board_process_packet(&msg);
		}
		last_msg_id = msg.header.msg_id;
	}
}

static void gen_ack_response(const struct device *dev,
			     struct RoachMsgHeader *msg, int msg_id)
{
	const struct one_wire_uart_config *config = dev->config;

	*msg = (struct RoachMsgHeader){
		.magic = 0xEC,
		.payload_len = 0,
		.cmd = 0,
		.sender = config->id,
		.msg_id = msg_id,
		.ack = 1,
		.checksum = 0,
	};

	msg->checksum = checksum((struct RoachMessage*)msg);
}

static void process_tx_irq(const struct device *dev)
{
	static timestamp_t resend_time = {.val = 0};
	static struct RoachMessage tx_buf;
	static int tx_buf_sent = -1;

	const struct one_wire_uart_config *config = dev->config;
	const struct device *bus = config->bus;
	int filled = 0;

	/* ack received, drop current message */
	if (tx_buf_sent >= 0 && ack == tx_buf.header.msg_id) {
		ack = -1;
		tx_buf_sent = -1;
	}

	/* always send ack first */
	if (tx_buf_sent <= 0 && pending_ack >= 0) {
		struct RoachMsgHeader ack_resp;

		/* (it8xxx2 only) tx interrupt triggers when fifo is empty,
		 * so we can assume that there's enough empty space to put the
		 * 5 byte header in the fifo without extra checking.
		 */
		gen_ack_response(dev, &ack_resp, pending_ack);
		uart_fifo_fill(bus, (uint8_t*)&ack_resp, sizeof(ack_resp));
		pending_ack = -1;
	}

	/* prepare next message */
	if (tx_buf_sent < 0) {
		if (k_msgq_get(&tx_queue, &tx_buf, K_NO_WAIT) == 0) {
			tx_buf_sent = 0;
			resend_time = get_time();
		}
	}

	if (tx_buf_sent >= 0) {
		int msg_size = ROACH_HEADER_SIZE + tx_buf.header.payload_len;

		/* resend */
		if (tx_buf_sent == msg_size && timestamp_expired(resend_time, NULL)) {
			tx_buf_sent = 0;
		}

		if (tx_buf_sent < msg_size) {
			filled = uart_fifo_fill(bus, (uint8_t*)&tx_buf + tx_buf_sent,
					msg_size - tx_buf_sent);

			if (filled > 0) {
				tx_buf_sent += filled;
			}

			resend_time.val = get_time().val + 3 * MSEC;
		} else {
			hook_call_deferred(config->schedule_tx_deferred, 3 * MSEC);
		}
	}

	if (filled == 0 && uart_irq_tx_complete(bus)) {
		uart_irq_tx_disable(bus);
	}
}

static void process_rx_irq(const struct device *dev)
{
	static struct RoachMessage rx_buf;
	static int tail = 0;
	const struct one_wire_uart_config *config = dev->config;
	const struct device *bus = config->bus;

	int bytes_read = uart_fifo_read(bus, (uint8_t*)&rx_buf + tail,
					sizeof(rx_buf) - tail);

	tail += bytes_read;

	while (true) {
		/* search for header marker 0xEC */
		void *header_begin = memchr(&rx_buf, 0xEC, tail);

		if (!header_begin) {
			tail = 0;
		} else {
			int shift = (uint8_t*)header_begin - (uint8_t*)&rx_buf;
			memmove(&rx_buf, header_begin, tail - shift);
			tail -= shift;
		}

		if (tail < ROACH_HEADER_SIZE) {
			break;
		}

		int msg_size = ROACH_HEADER_SIZE + rx_buf.header.payload_len;

		/* size too large, looks like an invalid message */
		if (msg_size > sizeof(rx_buf)) {
			/* erase current marker and find next one */
			rx_buf.header.magic = 0;
			continue;
		}

		/* incomplete message, wait for another chunk */
		if (tail < msg_size) {
			break;
		}

		if (checksum(&rx_buf) != 0) {
			/* erase current marker and find next one */
			rx_buf.header.magic = 0;
			continue;
		}

		/* proceed if the message is not sent by ourselves */
		if (rx_buf.header.sender != config->id) {
			uint8_t msg_id = rx_buf.header.msg_id;

			if (rx_buf.header.ack) {
				ack = msg_id;
			} else {
				k_msgq_put(&rx_queue, &rx_buf, K_NO_WAIT);
				hook_call_deferred(&process_packet_data, 0);
				pending_ack = msg_id;
				uart_irq_tx_enable(bus);
			}
		}

		memmove(&rx_buf, (uint8_t*)&rx_buf + msg_size, tail - msg_size);
		tail -= msg_size;
	}
}

void uart_handler(const struct device *bus, void *user_data)
{
	const struct device *dev = user_data;

	uart_irq_update(bus);

	if (uart_irq_rx_ready(bus)) {
		process_rx_irq(dev);
	}

	if (uart_irq_tx_ready(bus)) {
		process_tx_irq(dev);
	}
}

void cros_one_wire_uart_enable(const struct device *dev)
{
	const struct one_wire_uart_config *config = dev->config;
	const struct device *bus = config->bus;

	uart_irq_callback_user_data_set(bus, uart_handler, (void*)dev);
	uart_irq_rx_enable(bus);
}

#define INIT_ONE_WIRE_UART_DEVICE(n) \
	static void schedule_tx ## n(void);                                    \
	DECLARE_DEFERRED(schedule_tx ## n);                                    \
	static const struct one_wire_uart_config one_wire_uart_config ## n = { \
		.bus = DEVICE_DT_GET(DT_INST_PARENT(n)),                       \
		.id = DT_INST_PROP(n, id),                                     \
		.schedule_tx_deferred = &schedule_tx ## n ## _data,            \
	};                                                                     \
	static void schedule_tx ## n(void)                                     \
	{                                                                      \
		uart_irq_tx_enable(DEVICE_DT_GET(DT_INST_PARENT(n)));          \
	}                                                                      \
	static struct one_wire_uart_data one_wire_uart_data ## n = {           \
	};                                                                     \
	DEVICE_DT_INST_DEFINE(n, NULL, NULL, &one_wire_uart_data ## n,         \
			      &one_wire_uart_config ## n, POST_KERNEL, 50,     \
			      NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_ONE_WIRE_UART_DEVICE);
