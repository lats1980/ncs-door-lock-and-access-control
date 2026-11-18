/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Zephyr replacement for the NXP board header (boards/Host/Rhodes4/uwb_board.h).
 * Found ahead of the NXP sources through the include path order.
 */

#ifndef __UWB_BOARD_H__
#define __UWB_BOARD_H__

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <uwb_board_values.h>
#include <driver_config.h>

/* uwb_logging.h of the NXP middleware requires the board to provide PRINTF. */
#ifndef PRINTF
#define PRINTF(...) printk(__VA_ARGS__)
#endif

#define UWB_BOARD_ENABLE_FW_DOWNLOAD_ON_UWBINIT      1
#define UWB_BOARD_ENABLE_EXT_FLASH_BASED_FW_DOWNLOAD 0

/* FW image is linked into the host image and downloaded from RAM */
#define HELIOS_FW_USE_RAM 1

#endif /* __UWB_BOARD_H__ */
