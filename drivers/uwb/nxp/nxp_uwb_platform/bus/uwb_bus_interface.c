#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include "sr150.h"
#include "uwb_bus_interface.h"

LOG_MODULE_REGISTER(uwb_bus_iface, CONFIG_UWB_NXP_PLATFORM_LOG_LEVEL);

/* Largest SPI transfer, longer buffers are sent / received in pieces */
#define SPLIT_SZ CONFIG_UWB_NXP_SPI_SPLIT_SIZE

uwb_bus_status_t uwb_bus_init(uwb_bus_board_ctx_t *pCtx)
{
    if (pCtx == NULL) return kUWB_bus_Status_FAILED;

    /* Initialize SR150 HAL (GPIO, SPI, mutex, semaphore, IRQ) */
    if (sr150_init() < 0) {
        return kUWB_bus_Status_FAILED;
    }
    uwb_bus_io_init(pCtx);

    /* Power on the SR150 (POWER → EN → WAKEUP).
     * After power-on, the SR150 bootloader asserts IRQ to indicate
     * it's ready. We must wait for this IRQ before the SDK's init()
     * resets the chip again (EN toggle) and starts FWDL.
     */
    sr150_power_on();

    /* Wait up to 5s for the bootloader IRQ, indicating the chip is
     * alive.  If it doesn't come, the chip might need a longer wait.
     */
    if (uwb_bus_io_irq_wait(pCtx, 5000) != kUWB_bus_Status_OK) {
        LOG_WRN("SR150 boot IRQ not received within 5s, proceeding anyway");
    }

    return kUWB_bus_Status_OK;
}

uwb_bus_status_t uwb_bus_deinit(uwb_bus_board_ctx_t *pCtx)
{
    (void)pCtx;
    return kUWB_bus_Status_OK;
}

uwb_bus_status_t uwb_bus_reset(uwb_bus_board_ctx_t *pCtx)
{
    (void)pCtx;
    return kUWB_bus_Status_OK;
}

uwb_bus_status_t uwb_bus_data_tx(uwb_bus_board_ctx_t *pCtx, uint8_t *pBuf, size_t bufLen)
{
    if (pBuf == NULL || bufLen == 0) return kUWB_bus_Status_FAILED;
    do {
        size_t chunk = (bufLen > SPLIT_SZ) ? SPLIT_SZ : bufLen;
        if (sr150_spi_transfer(pBuf, NULL, chunk) < 0) {
            return kUWB_bus_Status_FAILED;
        }
        pBuf += chunk; bufLen -= chunk;
    } while (bufLen > 0);
    return kUWB_bus_Status_OK;
}

uwb_bus_status_t uwb_bus_data_rx(uwb_bus_board_ctx_t *pCtx, uint8_t *pBuf, size_t pBufLen)
{
    if (pBuf == NULL || pBufLen == 0) return kUWB_bus_Status_FAILED;
    do {
        size_t chunk = (pBufLen > SPLIT_SZ) ? SPLIT_SZ : pBufLen;
        if (sr150_spi_transfer(NULL, pBuf, chunk) < 0) {
            return kUWB_bus_Status_FAILED;
        }
        pBuf += chunk; pBufLen -= chunk;
    } while (pBufLen > 0);
    return kUWB_bus_Status_OK;
}

void uwb_port_DelayinMicroSec(int delay)
{
    if (delay > 0) k_busy_wait((uint32_t)delay);
}
