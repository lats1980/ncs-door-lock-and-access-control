/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file phOsal_Config.h
 * @brief Zephyr OSAL configuration for NXP NfcRdLib.
 *
 * This file shadows NXP phOsal_Config.h via include path ordering and
 * phOsal.h wrapper. NXP Confidential sources are not modified.
 */

#ifndef PHOSAL_INC_PHOSAL_CONFIG_H_
#define PHOSAL_INC_PHOSAL_CONFIG_H_

#define PHOSAL_MAX_DELAY      (0xFFFFFFFFU)

#define PH_OSAL_CONFIG_MAX_NUM_EVENTS      5U

#ifndef PHOSAL_MAX_DELAY
#error "PHOSAL_MAX_DELAY not defined by phOsal_Zephyr.h"
#endif

#endif /* PHOSAL_INC_PHOSAL_CONFIG_H_ */
