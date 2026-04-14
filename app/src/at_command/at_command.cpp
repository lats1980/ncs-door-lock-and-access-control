/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "at_command.h"
#include "at_transport.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(at_command, CONFIG_DOOR_LOCK_APP_LOG_LEVEL);

#define UART_BUF_SIZE             CONFIG_ALIRO_AT_COMMAND_BUF_SIZE

namespace at_command {

struct uart_data_t {
	void *fifo_reserved;
	uint8_t data[UART_BUF_SIZE];
	uint16_t len;
};

struct rx_event_t {
	struct uart_data_t *owner;
	uint8_t *buf;
	size_t len;
};

#define RX_EVENT_ALIGNMENT 4
K_MSGQ_DEFINE(rx_event_queue, sizeof(struct rx_event_t),
	      CONFIG_ALIRO_AT_RX_EVENT_QUEUE_SIZE, RX_EVENT_ALIGNMENT);

static void rx_process(struct k_work *work);
static K_WORK_DEFINE(rx_process_work, rx_process);

static void rx_process(struct k_work *work)
{
	struct rx_event_t rx_evt;
	size_t processed;
	bool stop_at_receive = false;
	int err;

	ARG_UNUSED(work);

	while (k_msgq_get(&rx_event_queue, &rx_evt, K_NO_WAIT) == 0) {
		processed = at_process(rx_evt.buf, rx_evt.len, &stop_at_receive);
		if (processed == rx_evt.len) {
			/* All data processed, release the buffer. */
			k_free(rx_evt.owner);
		} else {
			rx_evt.len -= processed;
			rx_evt.buf += processed;
			err = k_msgq_put_front(&rx_event_queue, &rx_evt, K_NO_WAIT);
			if (err) {
				LOG_ERR("RX event queue full, dropped %zu bytes", rx_evt.len);
				k_free(rx_evt.owner);
			}
		}

		if (stop_at_receive) {
			break;
		}
	}
}

} // namespace at_command

extern "C" {

int at_send(const uint8_t *data, size_t len)
{
	LOG_DBG("Sending AT data: %.*s", (int)len, data);
	return at_transport_tx(data, len);
}

int at_receive(const uint8_t *data, size_t len)
{
	using namespace at_command;
	struct uart_data_t *buf;

	if (len > UART_BUF_SIZE) {
		LOG_ERR("Invalid length for UART reception");
		return -EINVAL;
	}

	if (!data) {
		LOG_ERR("Invalid data for UART reception");
		return -EINVAL;
	}

	LOG_HEXDUMP_DBG(data, len, "Received AT data");

	buf = (struct uart_data_t *)k_malloc(sizeof(struct uart_data_t));
	if (!buf) {
		LOG_ERR("Not able to allocate UART receive buffer");
		return -ENOMEM;
	}
	memcpy(buf->data, data, len);
	buf->len = len;
	struct rx_event_t event = {
		.owner = buf,
		.buf = buf->data,
		.len = buf->len,
	};
	if (k_msgq_put(&rx_event_queue, &event, K_NO_WAIT) != 0) {
		LOG_ERR("RX event queue full, dropping UART data");
		k_free(buf);
		return -ENOMEM;
	}
	k_work_submit(&rx_process_work);

	return 0;
}

int at_send_str(const char *str)
{
	if (!str) {
		return -EINVAL;
	}
	return at_send((const uint8_t *)str, strlen(str));
}

} /* extern "C" */
