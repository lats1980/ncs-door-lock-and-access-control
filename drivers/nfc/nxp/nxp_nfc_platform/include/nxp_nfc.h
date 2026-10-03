/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef NXP_NFC_H
#define NXP_NFC_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief NXP NFC reader IRQ handler invoked from the platform monitor thread. */
typedef void (*nxp_nfc_irq_handler_t)(void);

/**
 * @brief Initializes the NXP NFC platform layer.
 *
 * Configures platform GPIO (reset/IRQ), OSAL, BAL, and NfcLib context.
 * Call before phNfcLib_Init().
 *
 * @return 0 on success, negative errno otherwise.
 */
int nxp_nfc_init(void);

/**
 * @brief Starts the NXP NFC IRQ monitor thread.
 *
 * Monitors the IRQ GPIO and invokes @p handler from thread context.
 * Call after phNfcLib_Init() and HAL data params are available.
 *
 * @param handler IRQ callback provided by the application.
 *
 * @return 0 on success, negative errno otherwise.
 */
int nxp_nfc_start_irq_monitor(nxp_nfc_irq_handler_t handler);

#ifdef __cplusplus
}
#endif

#endif /* NXP_NFC_H */
