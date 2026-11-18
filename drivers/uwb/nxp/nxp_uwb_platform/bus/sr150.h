/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * SR150 UWB device: SPI and GPIO access
 */

#ifndef SR150_H
#define SR150_H

#include <zephyr/types.h>
#include <stddef.h>
#include <stdbool.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** SR150 device context */
struct sr150_ctx
{
    struct spi_dt_spec   spi;
    struct gpio_dt_spec  gpio_irq;
    struct gpio_dt_spec  gpio_wakeup;
    struct gpio_dt_spec  gpio_en;
    struct k_mutex       spi_mutex;
    struct gpio_callback irq_cb_data;
    bool                 initialized;
};

/** SPI transfer direction */
#define SR150_SPI_TX 0
#define SR150_SPI_RX 1

/**
 * @brief Initialize the SR150 SPI/GPIO bus
 *
 * - gets the SPI device and the GPIOs from the devicetree
 * - configures the GPIOs (outputs: enable/wakeup, input: irq with rising edge interrupt)
 *
 * @return 0 on success, negative errno on failure
 */
int sr150_init(void);

/**
 * @brief Deinitialize the SR150
 *
 * - removes the GPIO interrupt callback
 * - drives the output pins to their default state
 */
int sr150_deinit(void);

/**
 * @brief Hardware reset of the SR150
 *
 * Sequence: WAKEUP=0 -> EN=0 -> delay -> EN=1 -> delay -> WAKEUP=1
 */
int sr150_reset(void);

/**
 * @brief Power on the SR150
 *
 * Sequence: EN=1 -> delay (10 ms) -> WAKEUP=1
 */
int sr150_power_on(void);

/**
 * @brief Power off the SR150
 *
 * Sequence: WAKEUP=0 -> EN=0 -> delay
 */
int sr150_power_off(void);

/**
 * @brief Half-duplex SPI transfer
 *
 * @param tx_data data to send (NULL for a read)
 * @param rx_data buffer for the received data (NULL for a write)
 * @param len     number of bytes
 * @return 0 on success, negative errno on failure
 */
int sr150_spi_transfer(const uint8_t *tx_data, uint8_t *rx_data, size_t len);

/**
 * @brief Get the device context
 *
 * @return the context, or NULL if sr150_init() has not been called
 */
struct sr150_ctx *sr150_get_ctx(void);

/**
 * @brief HOST SYNC pin control
 *
 * The NXP reference SDK toggles HELIOS_SYNC around every SPI UCI transaction:
 *   - high tells the SR150 that the host is ready
 *   - low at the end of the transaction
 * On the SR150 this signal is the WAKEUP pin.
 */
void sr150_sync_high(void);
void sr150_sync_low(void);
bool sr150_sync_get(void);

/* GPIO helpers for NXP bus_io layer */
bool sr150_gpio_irq_get(void);
void sr150_en_set(int on);
void sr150_power_set(int on); /* UWB supply, only if the devicetree has en-uwb-power-gpios */
void sr150_irq_enable(void);
void sr150_irq_disable(void);

#ifdef __cplusplus
}
#endif

#endif /* SR150_H */
