/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file phOsal.h
 * @brief Wrapper that applies Zephyr OSAL config before including NXP phOsal.h.
 *
 * NXP phOsal.h uses #include "phOsal_Config.h", which resolves to the NXP copy
 * in the same directory. This wrapper pre-includes the Zephyr config (setting
 * PHOSAL_INC_PHOSAL_CONFIG_H_) so the NXP config header is skipped, then
 * forwards to the unmodified NXP phOsal.h via #include_next.
 */

#ifndef PHOSAL_ZEPHYR_SHIM_H_
#define PHOSAL_ZEPHYR_SHIM_H_

#include "phOsal_Config.h"

#include_next <phOsal.h>

#endif /* PHOSAL_ZEPHYR_SHIM_H_ */
