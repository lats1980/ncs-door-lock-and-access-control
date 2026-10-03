/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef NXP_NFC_DEBUG_H
#define NXP_NFC_DEBUG_H

#include <ph_Status.h>
#include <phacDiscLoop.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Print the technology being resolved in the discovery loop. */
void nxp_nfc_debug_print_tech(uint8_t tech_type);

/** @brief Print buffer contents as a hex string. */
void nxp_nfc_debug_print_buff(uint8_t *buff, uint8_t num);

/** @brief Print tag information from discovery loop data parameters. */
void nxp_nfc_debug_print_tag_info(phacDiscLoop_Sw_DataParams_t *p_data_params,
				  uint16_t number_of_tags, uint16_t tags_detected);

/** @brief Print decoded NXP Reader Library error status. */
void nxp_nfc_debug_print_error_info(phStatus_t status);

#ifdef __cplusplus
}
#endif

#endif /* NXP_NFC_DEBUG_H */
