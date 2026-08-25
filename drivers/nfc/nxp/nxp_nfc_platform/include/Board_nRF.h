/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */


#ifndef BOARD_NRF_H
#define BOARD_NRF_H

#include <ph_Status.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

/******************************************************************
 * Board Pin/Gpio configurations
 * Pin numbers are taken from the "nxp,pn5190" devicetree node
 * (reset-gpios / irq-gpios) defined in the board overlay.
 ******************************************************************/
#if !defined(CONFIG_PN5190_DRV)
#error "NXP NFC driver requires CONFIG_PN5190_DRV"
#endif

#define NXP_NFC_NODE                DT_INST(0, nxp_pn5190)

#define PHDRIVER_PIN_RESET          DT_GPIO_PIN(NXP_NFC_NODE, reset_gpios)
#define PHDRIVER_PIN_IRQ            DT_GPIO_PIN(NXP_NFC_NODE, irq_gpios)
#define PHDRIVER_PIN_DWL            0xFFFF /** No functionality. To suppress build error in HAL. */
#define PHDRIVER_PIN_BUSY           PHDRIVER_PIN_IRQ

/******************************************************************
 * PIN Pull-Up/Pull-Down configurations.
 ******************************************************************/
#define PHDRIVER_PIN_RESET_PULL_CFG    0xFF /** No functionality. To suppress build error in HAL. */
#define PHDRIVER_PIN_IRQ_PULL_CFG      0xFF /** No functionality. To suppress build error in HAL. */
#define PHDRIVER_PIN_BUSY_PULL_CFG     0xFF /** No functionality. To suppress build error in HAL. */
#define PHDRIVER_PIN_DWL_PULL_CFG      0xFF /** No functionality. To suppress build error in HAL. */

/******************************************************************
 * IRQ & BUSY PIN TRIGGER settings
 ******************************************************************/
#define PIN_IRQ_TRIGGER_TYPE         PH_DRIVER_INTERRUPT_RISINGEDGE

/*****************************************************************
 * Front End Reset logic level settings (NXP DAL physical levels).
 * phDriver_PinWrite() maps these to Zephyr GPIO active/inactive using
 * the reset-gpios polarity from devicetree.
 ****************************************************************/
#define PH_DRIVER_SET_HIGH            1          /**< Drive GPIO physically high. */
#define PH_DRIVER_SET_LOW             0          /**< Drive GPIO physically low. */
#define RESET_POWERDOWN_LEVEL         PH_DRIVER_SET_LOW
#define RESET_POWERUP_LEVEL           PH_DRIVER_SET_HIGH

/*****************************************************************
 * Dummy entries
 * No functionality. To suppress build error in HAL. No pin functionality in SPI BAL.
 *****************************************************************/
#    define PHDRIVER_PIN_SSEL                            0xFFFF
#    define PHDRIVER_PIN_NSS_PULL_CFG                    PH_DRIVER_PULL_UP

/**
 * @brief Configures the PN5190 IRQ GPIO for interrupt-driven operation.
 *
 * Called from nxp_nfc_init(). phDriver_PinConfig() delegates here for
 * the IRQ/BUSY pin and remains idempotent.
 */
phStatus_t phDriver_ConfigureIrqPin(void);

#endif /* BOARD_NRF_H */
