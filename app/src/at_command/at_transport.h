/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef AT_TRANSPORT_H
#define AT_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum at_transport_state {
    AT_TRANSPORT_DISCONNECTED,
    AT_TRANSPORT_CONNECTED
};

typedef void (*at_transport_state_callback_t)(enum at_transport_state state);

/**
 * @brief Enable AT transport
 */
int at_transport_enable(at_transport_state_callback_t state_cb);

/**
 * @brief Send data. Queues and sends asynchronously.
 * @param data  Data to send (can be freed after return).
 * @param len   Length in bytes.
 * @return 0 on success, negative errno on failure.
 */
int at_transport_tx(const uint8_t *data, size_t len);

/**
 * @brief Receive data. Called from transport when data is received.
 * @param data  Received data (must be copied if needed after return).
 * @param len   Length in bytes.
 * @return 0 on success, negative errno on failure.
 */
int at_transport_rx(const uint8_t *data, size_t len);

/**
 * @brief Set the state of the AT transport.
 * @param state The new state.
 */
void at_transport_set_state(enum at_transport_state state);

#ifdef __cplusplus
}
#endif

#endif /* AT_TRANSPORT_H */