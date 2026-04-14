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
#include <zephyr/logging/log.h>

#ifdef CONFIG_DOOR_LOCK_BLE_NUS

#include "bt_nus/bt_nus.h"
#endif // CONFIG_DOOR_LOCK_BLE_NUS

LOG_MODULE_REGISTER(at_transport_ble, CONFIG_DOOR_LOCK_APP_LOG_LEVEL);

#define UART_BUF_SIZE 256

namespace at_transport_ble {

static at_transport_state_callback_t s_transport_state_cb;

}  // namespace at_transport_ble

extern "C" {

int at_transport_enable(at_transport_state_callback_t state_cb)
{
	at_transport_ble::s_transport_state_cb = state_cb;

	return 0;
}

int at_transport_tx(const uint8_t *data, size_t len)
{
	bool ret;

	ret = Aliro::BtNus::NUSService::Instance().SendData((const char *)data, len);
	if (!ret) {
		LOG_ERR("Failed to send data over NUS");
		return -EIO;
	}
	return 0;
}

int at_transport_rx(const uint8_t *data, size_t len)
{
	return at_receive(data, len);
}

void at_transport_set_state(enum at_transport_state state)
{
	using namespace at_transport_ble;

	LOG_WRN("AT transport state set: %d", static_cast<int>(state));
	if (s_transport_state_cb) {
		s_transport_state_cb(state);
	}
}

}  // extern "C"
