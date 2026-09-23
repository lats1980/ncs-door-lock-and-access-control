/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * AT transport over BLE (AT Module): NUS server.
 */

#include <aliro_at/at_transport.h>
#include <aliro_at/at_command.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_DOOR_LOCK_NUS_SERVICE
#include <nus_service/nus_service.h>
#endif // CONFIG_DOOR_LOCK_NUS_SERVICE

LOG_MODULE_REGISTER(at_transport_ble, CONFIG_DOOR_LOCK_APP_LOG_LEVEL);

extern "C" {

int at_transport_enable(void)
{
	return 0;
}

int at_transport_tx(const uint8_t *data, size_t len)
{
#ifdef CONFIG_DOOR_LOCK_NUS_SERVICE
	int ret = DoorLock::NUSService::Send(reinterpret_cast<const char *>(data), len);

	if (ret) {
		LOG_ERR("Failed to send data over NUS: %d", ret);
		return ret;
	}
	return 0;
#else
	ARG_UNUSED(data);
	ARG_UNUSED(len);
	return -ENOTSUP;
#endif // CONFIG_DOOR_LOCK_NUS_SERVICE
}

int at_transport_rx(const uint8_t *data, size_t len)
{
	return at_receive(data, len);
}

}  // extern "C"
