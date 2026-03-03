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

#define UART_BUF_SIZE             CONFIG_ALIRO_AT_UART_BUF_SIZE
#define UART_WAIT_FOR_RX          CONFIG_ALIRO_AT_UART_RX_WAIT_TIME
#define UART_WAIT_FOR_BUF_DELAY   K_MSEC(50)

namespace at_transport_serial {

const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(aliro_at_uart));

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
			struct rx_event_t rx_evt = {
				.owner = buf,
				.buf   = buf->data,
				.len   = buf->len,
			};
			int err = k_msgq_put(&rx_event_queue, &rx_evt, K_NO_WAIT);

			if (err) {
				LOG_ERR("RX event queue full, dropped %u bytes", buf->len);
				k_free(buf);
			} else {
				k_work_submit(&rx_process_work);
			}
		} else {
			k_free(buf);
		}
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

int at_transport_enable(void)
{
	return at_transport_serial::uart_init();
}

int at_transport_tx(const uint8_t *data, size_t len)
{
	using namespace at_transport_serial;

	if (!data || len == 0 || len > UART_BUF_SIZE) {
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

}  // extern "C"
