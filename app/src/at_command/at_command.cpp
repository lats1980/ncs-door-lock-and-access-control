/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "at_command.h"
#include "at_transport.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(at_command, CONFIG_DOOR_LOCK_APP_LOG_LEVEL);

extern "C" {

int at_send(const uint8_t *data, size_t len)
{
	LOG_DBG("Sending AT data: %.*s", (int)len, data);
	return at_transport_tx(data, len);
}

int at_send_str(const char *str)
{
	if (!str) {
		return -EINVAL;
	}
	return at_send((const uint8_t *)str, strlen(str));
}

} /* extern "C" */
