/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Zephyr replacement for boards/Host/Rhodes4/driver_config.h.
 */

#ifndef __DRIVER_CONFIG_H__
#define __DRIVER_CONFIG_H__

#include <uwb_board_values.h>

/*
 * Board variant version reported to the SR150 (UWB_BOARD_* in uwb_board_values.h).
 * It selects the NXP reference board hardware revision the firmware configures
 * itself for; override it from -DNXP_UWB_BOARD_CONFIG_DIR if the board differs.
 */
#ifndef UWB_BOARD_VERSION
#define UWB_BOARD_VERSION UWB_BOARD_RHODES_V4
#endif

/* Size of the HBCI/UCI header read first on every SPI read */
#define HBCI_HEADER_SIZE 4

#endif /* __DRIVER_CONFIG_H__ */
