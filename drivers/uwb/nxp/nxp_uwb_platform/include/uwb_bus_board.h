/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Bus context type used by the NXP uwb_bus_interface.h API.
 */

#ifndef __UWB_BUS_BOARD_H__
#define __UWB_BUS_BOARD_H__

/*
 * Required by the NXP transport (it holds one by value). The SR150 has a single
 * IRQ line and one SPI device, so everything lives in the platform layer and the
 * context carries no data. An empty struct is a GNU C extension (size 0).
 */
typedef struct {
} uwb_bus_board_ctx_t;

#endif /* __UWB_BUS_BOARD_H__ */
