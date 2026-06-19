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

#ifdef CONFIG_ALIRO_AT_UART_PM
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#endif

#if defined(CONFIG_ALIRO_AT_HOST)
#include "at_host.h"
#elif defined(CONFIG_ALIRO_AT_MODULE)
#include "at_module.h"
#endif

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

#ifdef CONFIG_ALIRO_AT_UART_PM
static const struct gpio_dt_spec uart_rx_wake =
	GPIO_DT_SPEC_GET(DT_NODELABEL(aliro_at_uart_rx_pin), gpios);

static struct gpio_callback uart_rx_wake_cb;
static struct k_work_delayable rx_wake_work;
static struct k_work_delayable tx_wake_work;
static struct k_work gpio_wake_setup_work;
static struct k_work_delayable idle_work;

static struct uart_data_t wake_tx_data = {
	.data = {0xF9, 0xF9, 0xF9, 0xF9, 0xF9},
	.len = 5,
};

static bool uart_power_save_active;
static bool uart_pm_suspend_pending;
static bool uart_gpio_wake_setup_pending;
/* True while the UART driver has an active uart_tx() transfer. */
static bool uart_tx_in_progress;
/* True from wake-byte send until the 10 ms delay elapses and fifo TX starts. */
static bool uart_tx_wake_pending;
static bool uart_rx_wake_cb_registered;

#define UART_SUSPEND_WAIT_MS      100
#define UART_TX_WAKE_DELAY_MS     10
#define UART_RX_WAKE_DELAY_MS     5
#endif

static void uart_work_handler(struct k_work *item);

#ifdef CONFIG_ALIRO_AT_UART_PM
static void uart_refresh_idle_timer(void);
static bool uart_can_enter_power_save(void);
static int uart_enable_rx_wake_gpio(void);
static void uart_disable_rx_wake_gpio(void);
static int uart_enter_power_save(void);
static int uart_exit_power_save(bool from_tx);
static void gpio_wake_setup_work_handler(struct k_work *work);
static int uart_finish_power_save_entry(void);

static void uart_refresh_idle_timer(void)
{
	if (uart_power_save_active) {
		return;
	}

	k_work_reschedule(&idle_work, K_MSEC(CONFIG_ALIRO_AT_UART_IDLE_TIMEOUT_MS));
}

static bool uart_can_enter_power_save(void)
{
	return !uart_tx_in_progress && !uart_tx_wake_pending &&
	       k_fifo_is_empty(&fifo_uart_tx_data);
}

static void tx_wake_work_handler(struct k_work *work);
static void uart_start_wake_tx(void);
static bool uart_is_wake_rx_data(const uint8_t *data, size_t len);

static void rx_wake_gpio_callback(const struct device *dev, struct gpio_callback *cb,
				  uint32_t pins);

static int uart_enable_rx_wake_gpio(void)
{
	int err;

	if (!gpio_is_ready_dt(&uart_rx_wake)) {
		LOG_ERR("UART RX wake GPIO not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&uart_rx_wake, GPIO_INPUT | GPIO_PULL_UP);
	if (err) {
		LOG_ERR("Failed to configure UART RX wake GPIO: %d", err);
		return err;
	}

	if (!uart_rx_wake_cb_registered) {
		gpio_init_callback(&uart_rx_wake_cb, rx_wake_gpio_callback, BIT(uart_rx_wake.pin));

		err = gpio_add_callback(uart_rx_wake.port, &uart_rx_wake_cb);
		if (err) {
			LOG_ERR("Failed to add UART RX wake callback: %d", err);
			return err;
		}

		uart_rx_wake_cb_registered = true;
	}

	err = gpio_pin_interrupt_configure_dt(&uart_rx_wake, GPIO_INT_EDGE_FALLING);
	if (err) {
		LOG_ERR("Failed to configure UART RX wake interrupt: %d", err);
		return err;
	}
	LOG_DBG("UART RX wake GPIO enabled");

	return 0;
}

static void uart_disable_rx_wake_gpio(void)
{
	(void)gpio_pin_interrupt_configure_dt(&uart_rx_wake, GPIO_INT_DISABLE);
}

static int uart_abort_power_save_entry(void)
{
#ifndef CONFIG_PM_DEVICE_RUNTIME
	int err = pm_device_action_run(uart_dev, PM_DEVICE_ACTION_RESUME);

	if (err && err != -EALREADY) {
		LOG_ERR("Failed to resume UART after power save abort: %d", err);
		return err;
	}
#else
	int err = pm_device_runtime_get(uart_dev);

	if (err && err != -EALREADY) {
		LOG_ERR("Failed to resume UART after power save abort: %d", err);
		return err;
	}
#endif

	uart_gpio_wake_setup_pending = false;
	uart_refresh_idle_timer();
	return 0;
}

static int uart_finish_power_save_entry(void)
{
	int err = uart_enable_rx_wake_gpio();

	if (err) {
		(void)uart_abort_power_save_entry();
		return err;
	}

	uart_gpio_wake_setup_pending = false;
	uart_power_save_active = true;
	LOG_DBG("UART power save active");
	return 0;
}

static void gpio_wake_setup_work_handler(struct k_work *work)
{
	enum pm_device_state state;
	int err;
	int64_t deadline;

	ARG_UNUSED(work);

	if (!uart_gpio_wake_setup_pending) {
		return;
	}

	deadline = k_uptime_get() + UART_SUSPEND_WAIT_MS;
	while (k_uptime_get() < deadline) {
		if (!uart_gpio_wake_setup_pending) {
			return;
		}

		err = pm_device_state_get(uart_dev, &state);
		if (err == 0 && state == PM_DEVICE_STATE_SUSPENDED) {
			break;
		}

		k_msleep(1);
	}

	if (!uart_gpio_wake_setup_pending) {
		return;
	}

	err = pm_device_state_get(uart_dev, &state);
	if (err != 0 || state != PM_DEVICE_STATE_SUSPENDED) {
		LOG_ERR("UART did not suspend in time (state=%d, err=%d)", state, err);
		(void)uart_abort_power_save_entry();
		return;
	}

	(void)uart_finish_power_save_entry();
}

static int uart_enter_power_save(void)
{
	int err;

	if (uart_power_save_active || uart_pm_suspend_pending) {
		return 0;
	}

	if (!uart_can_enter_power_save()) {
		uart_refresh_idle_timer();
		return 0;
	}

	LOG_DBG("Entering UART power save");
	uart_pm_suspend_pending = true;
	err = uart_rx_disable(uart_dev);
	if (err) {
		LOG_ERR("Failed to disable UART RX for power save: %d", err);
		uart_pm_suspend_pending = false;
		uart_refresh_idle_timer();
		return err;
	}

	return 0;
}

static int uart_exit_power_save(bool from_tx)
{
	struct uart_data_t *buf;
	int err;

	if (!uart_power_save_active && !uart_pm_suspend_pending) {
		uart_refresh_idle_timer();
		return 0;
	}

	LOG_DBG("Exiting UART power save (from_tx=%d)", from_tx);
	uart_gpio_wake_setup_pending = false;
	uart_disable_rx_wake_gpio();
#ifdef CONFIG_PM_DEVICE_RUNTIME
	pm_device_runtime_get(uart_dev);
#else
	err = pm_device_action_run(uart_dev, PM_DEVICE_ACTION_RESUME);
	if (err && err != -EALREADY) {
		LOG_ERR("Failed to resume UART: %d", err);
		return err;
	}
#endif
	buf = (struct uart_data_t *)k_malloc(sizeof(*buf));
	if (!buf) {
		LOG_WRN("Not able to allocate UART receive buffer after wake");
		k_work_reschedule(&uart_work, UART_WAIT_FOR_BUF_DELAY);
		return -ENOMEM;
	}
	err = uart_rx_enable(uart_dev, buf->data, sizeof(buf->data), UART_WAIT_FOR_RX);
	if (err) {
		LOG_ERR("Failed to enable UART RX after wake: %d", err);
		k_free(buf);
		return err;
	}
	uart_power_save_active = false;
	uart_pm_suspend_pending = false;
	buf->len = 0;

	uart_refresh_idle_timer();
	return 0;
}

static void idle_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	(void)uart_enter_power_save();
}

static void rx_wake_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	(void)uart_exit_power_save(false);
}

static void tx_wake_work_handler(struct k_work *work)
{
	struct uart_data_t *buf;

	ARG_UNUSED(work);

	if (uart_tx_in_progress) {
		k_work_schedule(&tx_wake_work, K_MSEC(1));
		return;
	}

	uart_tx_wake_pending = false;

	buf = (struct uart_data_t *)k_fifo_get(&fifo_uart_tx_data, K_NO_WAIT);
	if (!buf) {
		return;
	}

	if (uart_tx(uart_dev, buf->data, buf->len, SYS_FOREVER_MS)) {
		LOG_WRN("Failed to send data over UART");
		k_fifo_put(&fifo_uart_tx_data, buf);
	} else {
		uart_tx_in_progress = true;
	}
}

static void uart_start_wake_tx(void)
{
	uart_tx_wake_pending = true;

	if (uart_tx(uart_dev, wake_tx_data.data, wake_tx_data.len, SYS_FOREVER_MS)) {
		LOG_WRN("Failed to send UART wake bytes");
		k_work_schedule(&tx_wake_work, K_MSEC(UART_TX_WAKE_DELAY_MS));
		return;
	}

	uart_tx_in_progress = true;
}

static bool uart_is_wake_rx_data(const uint8_t *data, size_t len)
{
	if (len != wake_tx_data.len) {
		return false;
	}

	return memcmp(data, wake_tx_data.data, wake_tx_data.len) == 0;
}

static void rx_wake_gpio_callback(const struct device *dev, struct gpio_callback *cb,
				  uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	(void)gpio_pin_interrupt_configure_dt(&uart_rx_wake, GPIO_INT_DISABLE);
	k_work_schedule(&rx_wake_work, K_MSEC(UART_RX_WAKE_DELAY_MS));
}
#endif /* CONFIG_ALIRO_AT_UART_PM */

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

#ifdef CONFIG_ALIRO_AT_UART_PM
		if (buf == &wake_tx_data) {
			uart_tx_in_progress = false;
			k_work_schedule(&tx_wake_work, K_MSEC(UART_TX_WAKE_DELAY_MS));
			break;
		}
#endif

		k_free(buf);

		buf = (struct uart_data_t *)k_fifo_get(&fifo_uart_tx_data, K_NO_WAIT);
		if (!buf) {
#ifdef CONFIG_ALIRO_AT_UART_PM
			uart_tx_in_progress = false;
			uart_refresh_idle_timer();
#endif
			return;
		}

		if (uart_tx(uart_dev, buf->data, buf->len, SYS_FOREVER_MS)) {
			LOG_WRN("Failed to send data over UART");
		}
#ifdef CONFIG_ALIRO_AT_UART_PM
		else {
			uart_tx_in_progress = true;
		}
#endif
		break;

	case UART_RX_RDY:
		LOG_DBG("UART_RX_RDY");
		if (evt->data.rx.len == 1 && evt->data.rx.buf[0] == 0xF9) {
			LOG_DBG("Received UART wake-up byte");
			break;
		}
		buf = CONTAINER_OF(evt->data.rx.buf, struct uart_data_t, data[0]);
		buf->len += evt->data.rx.len;
#ifdef CONFIG_ALIRO_AT_UART_PM
		uart_refresh_idle_timer();
#endif
		break;

	case UART_RX_DISABLED:
		LOG_DBG("UART_RX_DISABLED");

#ifdef CONFIG_ALIRO_AT_UART_PM
		if (uart_pm_suspend_pending) {
			uart_pm_suspend_pending = false;
#ifdef CONFIG_PM_DEVICE_RUNTIME
			uart_gpio_wake_setup_pending = true;
			pm_device_runtime_put(uart_dev);
			k_work_submit(&gpio_wake_setup_work);
#else
			int err;

			err = pm_device_action_run(uart_dev, PM_DEVICE_ACTION_SUSPEND);
			if (err && err != -EALREADY) {
				LOG_ERR("Failed to suspend UART: %d", err);
				uart_refresh_idle_timer();
				break;
			}

			err = uart_finish_power_save_entry();
			if (err) {
				break;
			}
#endif
			break;
		}
#endif

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

#ifdef CONFIG_ALIRO_AT_UART_PM
		uart_refresh_idle_timer();
#endif

		if (buf->len > 0) {
#ifdef CONFIG_ALIRO_AT_UART_PM
			if (uart_is_wake_rx_data(buf->data, buf->len)) {
				LOG_DBG("Received UART wake-up bytes, ignoring");
			} else if (at_transport_rx(buf->data, buf->len) == 0) {
#else
			if (at_transport_rx(buf->data, buf->len) == 0) {
#endif
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

		if (uart_tx(uart_dev, &buf->data[aborted_len],
			    buf->len - aborted_len, SYS_FOREVER_MS)) {
			LOG_WRN("Failed to resume aborted UART TX");
		}
#ifdef CONFIG_ALIRO_AT_UART_PM
		else {
			uart_tx_in_progress = true;
		}
#endif
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
#if defined(CONFIG_ALIRO_AT_HOST)
	at_host_update_event(AT_HOST_TRANSPORT_CONNECTED);
#elif defined(CONFIG_ALIRO_AT_MODULE)
	at_module_update_event(AT_MODULE_TRANSPORT_CONNECTED);
#endif
#ifdef CONFIG_ALIRO_AT_UART_PM
	uart_refresh_idle_timer();
#endif
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

#ifdef CONFIG_ALIRO_AT_UART_PM
	k_work_init_delayable(&idle_work, idle_work_handler);
	k_work_init_delayable(&rx_wake_work, rx_wake_work_handler);
	k_work_init_delayable(&tx_wake_work, tx_wake_work_handler);
	k_work_init(&gpio_wake_setup_work, gpio_wake_setup_work_handler);
#ifdef CONFIG_PM_DEVICE_RUNTIME
	pm_device_runtime_enable(uart_dev);
	pm_device_runtime_get(uart_dev);
#endif
#endif

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

#ifdef CONFIG_ALIRO_AT_UART_PM
	uart_refresh_idle_timer();
#endif

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

	if (!data || len == 0 || len > CONFIG_ALIRO_AT_COMMAND_BUF_SIZE) {
		LOG_ERR("Invalid data or length for UART transmission");
		return -EINVAL;
	}

#ifdef CONFIG_ALIRO_AT_UART_PM
	if (uart_power_save_active) {
		int wake_err = uart_exit_power_save(true);

		if (wake_err) {
			return wake_err;
		}
	} else {
		uart_refresh_idle_timer();
	}
#endif

	struct uart_data_t *tx = (struct uart_data_t *)k_malloc(sizeof(*tx));
	if (!tx) {
		LOG_ERR("Failed to allocate memory for UART transmission");
		return -ENOMEM;
	}

	memcpy(tx->data, data, len);
	tx->len = (uint16_t)len;

#ifdef CONFIG_ALIRO_AT_UART_PM
	k_fifo_put(&fifo_uart_tx_data, tx);

	if (!uart_tx_in_progress && !uart_tx_wake_pending) {
		uart_start_wake_tx();
	}

	return 0;
#else
	int err = uart_tx(uart_dev, tx->data, tx->len, SYS_FOREVER_MS);
	if (err) {
		LOG_DBG("Failed to send data over UART: %d", err);
		k_fifo_put(&fifo_uart_tx_data, tx);
	}

	return 0;
#endif
}

int at_transport_rx(const uint8_t *data, size_t len)
{
	return at_receive(data, len);
}

}  // extern "C"
