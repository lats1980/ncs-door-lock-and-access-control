/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * UART TX/RX with message queue
 */

#include "at_transport.h"
#include "at_command.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(at_transport_serial, CONFIG_DOOR_LOCK_APP_LOG_LEVEL);

#define UART_WAIT_FOR_RX          50000
#define UART_WAIT_FOR_BUF_DELAY   K_MSEC(50)

namespace at_transport_serial {

const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(aliro_at_uart));

struct uart_data_t {
	void *fifo_reserved;
	uint8_t data[CONFIG_ALIRO_AT_COMMAND_BUF_SIZE];
	uint16_t len;
};

static K_FIFO_DEFINE(fifo_uart_tx_data);
static struct k_work_delayable uart_work;

static void uart_work_handler(struct k_work *item);

static void uart_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	static size_t aborted_len;
	struct uart_data_t *buf;
	static uint8_t *aborted_buf;

	switch (evt->type) {
	case UART_TX_DONE:
		LOG_DBG("UART_TX_DONE");
		if ((evt->data.tx.len == 0) || (!evt->data.tx.buf)) {
			return;
		}

		if (aborted_buf) {
			buf = CONTAINER_OF(aborted_buf, struct uart_data_t, data[0]);
			aborted_buf = nullptr;
			aborted_len = 0;
		} else {
			buf = CONTAINER_OF(evt->data.tx.buf, struct uart_data_t, data[0]);
		}

		k_free(buf);

		buf = (struct uart_data_t *)k_fifo_get(&fifo_uart_tx_data, K_NO_WAIT);
		if (!buf) {
			return;
		}

		if (uart_tx(uart_dev, buf->data, buf->len, SYS_FOREVER_MS)) {
			LOG_WRN("Failed to send data over UART");
		}
		break;

	case UART_RX_RDY:
		LOG_DBG("UART_RX_RDY");
		buf = CONTAINER_OF(evt->data.rx.buf, struct uart_data_t, data[0]);
		buf->len += evt->data.rx.len;
		break;

	case UART_RX_DISABLED:
		LOG_DBG("UART_RX_DISABLED");

		buf = (struct uart_data_t *)k_malloc(sizeof(*buf));
		if (buf) {
			buf->len = 0;
		} else {
			LOG_WRN("Not able to allocate UART receive buffer");
			k_work_reschedule(&uart_work, UART_WAIT_FOR_BUF_DELAY);
			return;
		}

		uart_rx_enable(uart_dev, buf->data, sizeof(buf->data), UART_WAIT_FOR_RX);
		break;

	case UART_RX_BUF_REQUEST:
		LOG_DBG("UART_RX_BUF_REQUEST");
		buf = (struct uart_data_t *)k_malloc(sizeof(*buf));
		if (buf) {
			buf->len = 0;
			uart_rx_buf_rsp(uart_dev, buf->data, sizeof(buf->data));
		} else {
			LOG_WRN("Not able to allocate UART receive buffer");
		}
		break;

	case UART_RX_BUF_RELEASED:
		LOG_DBG("UART_RX_BUF_RELEASED");
		buf = CONTAINER_OF(evt->data.rx_buf.buf, struct uart_data_t, data[0]);

		if (buf->len > 0) {
			if (at_transport_rx(buf->data, buf->len) == 0) {
				LOG_DBG("AT transport received data successfully");
			} else {
				LOG_ERR("Failed to send data to AT transport");
			}
		}
		k_free(buf);
		break;

	case UART_TX_ABORTED:
		LOG_DBG("UART_TX_ABORTED");
		if (!aborted_buf) {
			aborted_buf = (uint8_t *)evt->data.tx.buf;
		}
		aborted_len += evt->data.tx.len;
		buf = CONTAINER_OF((void *)aborted_buf, struct uart_data_t, data);

		uart_tx(uart_dev, &buf->data[aborted_len],
			buf->len - aborted_len, SYS_FOREVER_MS);
		break;

	default:
		break;
	}
}

static void uart_work_handler(struct k_work *item)
{
	struct uart_data_t *buf;

	ARG_UNUSED(item);

	buf = (struct uart_data_t *)k_malloc(sizeof(*buf));
	if (buf) {
		buf->len = 0;
	} else {
		LOG_WRN("Not able to allocate UART receive buffer");
		k_work_reschedule(&uart_work, UART_WAIT_FOR_BUF_DELAY);
		return;
	}

	uart_rx_enable(uart_dev, buf->data, sizeof(buf->data), UART_WAIT_FOR_RX);
}

static int uart_init(void)
{
	int err;
	struct uart_data_t *rx;

	if (!device_is_ready(uart_dev)) {
		LOG_ERR("UART device not ready");
		return -ENODEV;
	}

	rx = (struct uart_data_t *)k_malloc(sizeof(*rx));
	if (!rx) {
		return -ENOMEM;
	}
	rx->len = 0;

	k_work_init_delayable(&uart_work, uart_work_handler);

	err = uart_callback_set(uart_dev, uart_cb, nullptr);
	if (err) {
		k_free(rx);
		LOG_ERR("Cannot initialize UART callback: %d", err);
		return err;
	}

	err = uart_rx_enable(uart_dev, rx->data, sizeof(rx->data), UART_WAIT_FOR_RX);
	if (err) {
		k_free(rx);
		LOG_ERR("Cannot enable UART RX: %d", err);
		return err;
	}

	LOG_DBG("AT Host UART transport ready");
	return 0;
}

}  // namespace at_transport_serial

extern "C" {

int at_transport_enable(at_transport_state_callback_t state_cb)
{
	return at_transport_serial::uart_init();
}

int at_transport_tx(const uint8_t *data, size_t len)
{
	using namespace at_transport_serial;

	if (!data || len == 0 || len > CONFIG_ALIRO_AT_COMMAND_BUF_SIZE) {
		LOG_ERR("Invalid data or length for UART transmission");
		return -EINVAL;
	}

	struct uart_data_t *tx = (struct uart_data_t *)k_malloc(sizeof(*tx));
	if (!tx) {
		LOG_ERR("Failed to allocate memory for UART transmission");
		return -ENOMEM;
	}

	memcpy(tx->data, data, len);
	tx->len = (uint16_t)len;

	int err = uart_tx(uart_dev, tx->data, tx->len, SYS_FOREVER_MS);
	if (err) {
		LOG_DBG("Failed to send data over UART: %d", err);
		k_fifo_put(&fifo_uart_tx_data, tx);
	}

	return 0;
}

int at_transport_rx(const uint8_t *data, size_t len)
{
	return at_receive(data, len);
}

}  // extern "C"
