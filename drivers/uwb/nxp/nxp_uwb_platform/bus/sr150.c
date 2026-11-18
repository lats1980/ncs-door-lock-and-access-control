/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * SR150 UWB device: Zephyr SPI/GPIO access
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "sr150.h"
#include "uwb_bus_interface.h"
#define UWB_POST_RESET_DELAY_MS 100
#define UWB_POWER_ON_DELAY_MS   10

LOG_MODULE_REGISTER(sr150, CONFIG_UWB_NXP_PLATFORM_LOG_LEVEL);

/* ================================================================
 *  Devicetree node and build time checks
 * ================================================================ */

#define SR150_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(nxp_sr150)

#if !DT_NODE_EXISTS(SR150_NODE)
#error "No nxp,sr150 compatible node found in DTS"
#endif

BUILD_ASSERT(DT_NODE_HAS_PROP(SR150_NODE, irq_gpios), "nxp,sr150 node must have irq-gpios");
BUILD_ASSERT(DT_NODE_HAS_PROP(SR150_NODE, wakeup_gpios), "nxp,sr150 node must have wakeup-gpios");
BUILD_ASSERT(DT_NODE_HAS_PROP(SR150_NODE, en_gpios), "nxp,sr150 node must have en-gpios");

/* Optional supply enable of the SR150 */
#define SR150_HAS_POWER_GPIO DT_NODE_HAS_PROP(SR150_NODE, en_uwb_power_gpios)

#if SR150_HAS_POWER_GPIO
static const struct gpio_dt_spec sr150_power_gpio = GPIO_DT_SPEC_GET(SR150_NODE, en_uwb_power_gpios);
#endif

/* ================================================================
 *  SR150 instance
 * ================================================================ */

static struct sr150_ctx sr150_ctx_data = {
    .spi         = SPI_DT_SPEC_GET(SR150_NODE, SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB),
    .gpio_irq    = GPIO_DT_SPEC_GET(SR150_NODE, irq_gpios),
    .gpio_wakeup = GPIO_DT_SPEC_GET(SR150_NODE, wakeup_gpios),
    .gpio_en     = GPIO_DT_SPEC_GET(SR150_NODE, en_gpios),
    .initialized = false,
};

/* ================================================================
 *  Internal functions
 * ================================================================ */

static void sr150_irq_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(pins);
    /* Wake the TML reader waiting in uwb_bus_io_irq_wait() */
    uwb_bus_io_irq_cb(NULL);
}

static int sr150_gpio_init(void)
{
    int ret;

    LOG_DBG("sr150_gpio_init start");

    /* IRQ: input with pull-down (no floating pin), rising edge interrupt */
    ret = gpio_pin_configure_dt(&sr150_ctx_data.gpio_irq, GPIO_INPUT | GPIO_PULL_DOWN);
    if (ret < 0)
    {
        LOG_ERR("IRQ GPIO config failed: %d", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&sr150_ctx_data.gpio_irq, GPIO_INT_EDGE_RISING);
    if (ret < 0)
    {
        LOG_ERR("IRQ interrupt config failed: %d", ret);
        return ret;
    }

    gpio_init_callback(&sr150_ctx_data.irq_cb_data, sr150_irq_handler, BIT(sr150_ctx_data.gpio_irq.pin));
    ret = gpio_add_callback(sr150_ctx_data.gpio_irq.port, &sr150_ctx_data.irq_cb_data);
    if (ret < 0)
    {
        LOG_ERR("IRQ callback add failed: %d", ret);
        return ret;
    }

    /* WAKEUP: output, low */
    ret = gpio_pin_configure_dt(&sr150_ctx_data.gpio_wakeup, GPIO_OUTPUT_LOW);
    if (ret < 0)
    {
        LOG_ERR("WAKEUP GPIO config failed: %d", ret);
        return ret;
    }

    /* EN: output, low */
    ret = gpio_pin_configure_dt(&sr150_ctx_data.gpio_en, GPIO_OUTPUT_LOW);
    if (ret < 0)
    {
        LOG_ERR("EN GPIO config failed: %d", ret);
        return ret;
    }

    return 0;
}

/* Keep SR150 disabled until its full UWB stack is explicitly initialized. */
static int sr150_power_default_off(void)
{
    int ret;

    ret = gpio_pin_configure_dt(&sr150_ctx_data.gpio_wakeup, GPIO_OUTPUT_LOW);
    if (ret != 0)
    {
        LOG_ERR("SR150 WAKEUP default-off configuration failed: %d", ret);
        return ret;
    }

    ret = gpio_pin_configure_dt(&sr150_ctx_data.gpio_en, GPIO_OUTPUT_LOW);
    if (ret != 0)
    {
        LOG_ERR("SR150 EN default-off configuration failed: %d", ret);
        return ret;
    }

#if SR150_HAS_POWER_GPIO
    ret = gpio_pin_configure_dt(&sr150_power_gpio, GPIO_OUTPUT_INACTIVE);
    if (ret != 0)
    {
        LOG_ERR("SR150 supply default-off configuration failed: %d", ret);
        return ret;
    }
#endif

    LOG_DBG("SR150 power default OFF");
    return 0;
}

SYS_INIT(sr150_power_default_off, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);

/* ================================================================
 *  Public API
 * ================================================================ */

int sr150_init(void)
{
    int ret;

    if (sr150_ctx_data.initialized)
    {
        LOG_WRN("SR150 already initialized");
        return 0;
    }

    /* The SPI bus must be ready */
    if (!spi_is_ready_dt(&sr150_ctx_data.spi))
    {
        LOG_ERR("SPI bus %s is not ready", sr150_ctx_data.spi.bus->name);
        return -ENODEV;
    }

    /* Serializes the SPI transfers */
    k_mutex_init(&sr150_ctx_data.spi_mutex);

    /* Initialize the GPIOs */
    ret = sr150_gpio_init();
    if (ret < 0)
    {
        return ret;
    }

    sr150_ctx_data.initialized = true;

    LOG_DBG("SR150 initialized, SPI=%s freq=%u", sr150_ctx_data.spi.bus->name, sr150_ctx_data.spi.config.frequency);

    return 0;
}

int sr150_deinit(void)
{
    if (!sr150_ctx_data.initialized)
    {
        return 0;
    }

    /* Remove the IRQ interrupt */
    (void)gpio_remove_callback(sr150_ctx_data.gpio_irq.port, &sr150_ctx_data.irq_cb_data);
    (void)gpio_pin_interrupt_configure_dt(&sr150_ctx_data.gpio_irq, GPIO_INT_DISABLE);

    /* Drive the output pins low */
    gpio_pin_set_dt(&sr150_ctx_data.gpio_wakeup, 0);
    gpio_pin_set_dt(&sr150_ctx_data.gpio_en, 0);

    sr150_ctx_data.initialized = false;

    LOG_DBG("SR150 deinitialized");
    return 0;
}

int sr150_reset(void)
{
    if (!sr150_ctx_data.initialized)
    {
        return -EPERM;
    }

    LOG_DBG("SR150 reset: toggle EN (supply stays on)");

    /* Hardware reset: only EN is toggled, the supply is not touched. */
    gpio_pin_set_dt(&sr150_ctx_data.gpio_wakeup, 0);
    gpio_pin_set_dt(&sr150_ctx_data.gpio_en, 0);
    k_sleep(K_MSEC(UWB_POST_RESET_DELAY_MS));

    gpio_pin_set_dt(&sr150_ctx_data.gpio_en, 1);
    k_sleep(K_MSEC(UWB_POST_RESET_DELAY_MS));

    gpio_pin_set_dt(&sr150_ctx_data.gpio_wakeup, 1);
    k_sleep(K_MSEC(UWB_POST_RESET_DELAY_MS));

    LOG_DBG("SR150 reset done");
    return 0;
}

int sr150_power_on(void)
{
    if (!sr150_ctx_data.initialized)
    {
        return -EPERM;
    }

    LOG_DBG("SR150 power on sequence start");

    /* Step 1: enable the chip (EN) */
    gpio_pin_set_dt(&sr150_ctx_data.gpio_en, 1);
    k_sleep(K_MSEC(UWB_POWER_ON_DELAY_MS));

    /* Step 2: set WAKEUP high */
    gpio_pin_set_dt(&sr150_ctx_data.gpio_wakeup, 1);
    k_sleep(K_MSEC(UWB_POWER_ON_DELAY_MS));

    LOG_DBG("SR150 power on done");
    return 0;
}

int sr150_power_off(void)
{
    if (!sr150_ctx_data.initialized)
    {
        return -EPERM;
    }

    LOG_DBG("SR150 power off sequence");

    gpio_pin_set_dt(&sr150_ctx_data.gpio_wakeup, 0);
    gpio_pin_set_dt(&sr150_ctx_data.gpio_en, 0);
    k_sleep(K_MSEC(UWB_POWER_ON_DELAY_MS));

    LOG_DBG("SR150 power off done");
    return 0;
}

int sr150_spi_transfer(const uint8_t *tx_data, uint8_t *rx_data, size_t len)
{
    int ret;

    if (!sr150_ctx_data.initialized)
    {
        return -EPERM;
    }

    if (len == 0)
    {
        return 0;
    }

    if (len > CONFIG_UWB_NXP_SPI_SPLIT_SIZE)
    {
        LOG_ERR("SPI transfer too large: %zu (max %u)", len, CONFIG_UWB_NXP_SPI_SPLIT_SIZE);
        return -EINVAL;
    }

    /* NXP: only half-duplex — TX-only via spi_write, RX-only via spi_read (ORC=0xFF on MOSI) */
    k_mutex_lock(&sr150_ctx_data.spi_mutex, K_FOREVER);

    if (tx_data != NULL && rx_data == NULL)
    {
        struct spi_buf     tx_buf = { .buf = (uint8_t *)tx_data, .len = len };
        struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
        ret                       = spi_write_dt(&sr150_ctx_data.spi, &tx_set);
    }
    else if (rx_data != NULL && tx_data == NULL)
    {
        struct spi_buf     rx_buf = { .buf = rx_data, .len = len };
        struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };
        ret                       = spi_read_dt(&sr150_ctx_data.spi, &rx_set);
    }
    else
    {
        ret = -EINVAL;
    }

    k_mutex_unlock(&sr150_ctx_data.spi_mutex);

    if (ret < 0)
    {
        LOG_ERR("SPI transfer failed: %d (len=%zu)", ret, len);
    }
    return ret;
}

struct sr150_ctx *sr150_get_ctx(void)
{
    return sr150_ctx_data.initialized ? &sr150_ctx_data : NULL;
}

void sr150_sync_high(void)
{
    if (sr150_ctx_data.initialized)
    {
        gpio_pin_set_dt(&sr150_ctx_data.gpio_wakeup, 1);
    }
}

void sr150_sync_low(void)
{
    if (sr150_ctx_data.initialized)
    {
        gpio_pin_set_dt(&sr150_ctx_data.gpio_wakeup, 0);
    }
}

bool sr150_sync_get(void)
{
    if (!sr150_ctx_data.initialized)
        return false;
    return (bool)gpio_pin_get_dt(&sr150_ctx_data.gpio_wakeup);
}

bool sr150_gpio_irq_get(void)
{
    if (!sr150_ctx_data.initialized)
        return false;
    return (bool)gpio_pin_get_dt(&sr150_ctx_data.gpio_irq);
}

void sr150_en_set(int on)
{
    if (sr150_ctx_data.initialized)
    {
        gpio_pin_set_dt(&sr150_ctx_data.gpio_en, on ? 1 : 0);
    }
}

void sr150_power_set(int on)
{
#if SR150_HAS_POWER_GPIO
    gpio_pin_set_dt(&sr150_power_gpio, on ? 1 : 0);
#else
    /* No en-uwb-power-gpios in the devicetree: the supply is always on */
    ARG_UNUSED(on);
#endif
}

void sr150_irq_enable(void)
{
    if (sr150_ctx_data.initialized)
    {
        gpio_pin_interrupt_configure_dt(&sr150_ctx_data.gpio_irq, GPIO_INT_EDGE_RISING);
    }
}

void sr150_irq_disable(void)
{
    if (sr150_ctx_data.initialized)
    {
        gpio_pin_interrupt_configure_dt(&sr150_ctx_data.gpio_irq, GPIO_INT_DISABLE);
    }
}
