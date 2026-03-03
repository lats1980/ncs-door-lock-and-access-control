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

/**
 * @brief Enable AT transport
 */
int at_transport_enable(void);

/**
 * @brief Send data. Queues and sends asynchronously.
 * @param data  Data to send (can be freed after return).
 * @param len   Length in bytes.
 * @return 0 on success, negative errno on failure.
 */
int at_transport_tx(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* AT_TRANSPORT_H */