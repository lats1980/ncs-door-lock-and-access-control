/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef NXP_NFC_ISODEP_H
#define NXP_NFC_ISODEP_H

#include <phacDiscLoop.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure ISO-DEP and notify Aliro when a Type 4A tag is activated.
 *
 * @param disc_loop_params Discovery loop data parameters for the activated tag.
 */
void nxp_nfc_isodep_on_activated(phacDiscLoop_Sw_DataParams_t *disc_loop_params);

/** @return non-zero while an ISO-DEP Aliro session is active. */
int nxp_nfc_isodep_session_active(void);

/**
 * @brief Run the ISO-DEP exchange loop until the session is terminated.
 *
 * Must be called from the NFC discovery thread while the tag remains activated.
 *
 * @param disc_loop_params Discovery loop data parameters for the activated tag.
 */
void nxp_nfc_isodep_run_session(phacDiscLoop_Sw_DataParams_t *disc_loop_params);

/**
 * @brief Tear down ISO-DEP and prepare the discovery loop for the next poll cycle.
 *
 * @param disc_loop_params Discovery loop data parameters for the activated tag.
 */
void nxp_nfc_isodep_cleanup(phacDiscLoop_Sw_DataParams_t *disc_loop_params);

#ifdef __cplusplus
}
#endif

#endif /* NXP_NFC_ISODEP_H */
