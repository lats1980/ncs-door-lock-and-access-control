#include "phDriver.h"

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>

#define PN5190_NODE                           DT_INST(0, nxp_pn5190)

/*
 * SPI device the PN5190 is connected to, as described in the board overlay (e.g. &spi1
 * with cs-gpios). Chip-select (NSS) is asserted/deasserted automatically by Zephyr around
 * each spi_transceive_dt() call, based on the "cs-gpios" property of the bus node.
 */
static const struct spi_dt_spec spi_dev = SPI_DT_SPEC_GET(
    PN5190_NODE, SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER, 0);

phStatus_t phbalReg_Init(
                         void * pDataParams,
                         uint16_t wSizeOfDataParams
                         )
{
    if ((pDataParams == NULL) || (sizeof(phbalReg_Type_t) != wSizeOfDataParams))
    {
        return (PH_DRIVER_ERROR | PH_COMP_DRIVER);
    }

    if (!spi_is_ready_dt(&spi_dev))
    {
        return (PH_DRIVER_ERROR | PH_COMP_DRIVER);
    }

    ((phbalReg_Type_t *)pDataParams)->wId      = PH_COMP_DRIVER;
    ((phbalReg_Type_t *)pDataParams)->bBalType = PHBAL_REG_TYPE_KERNEL_SPI;

    return PH_DRIVER_SUCCESS;
}

phStatus_t phbalReg_Exchange(
                             void * pDataParams,
                             uint16_t wOption,
                             uint8_t * pTxBuffer,
                             uint16_t wTxLength,
                             uint16_t wRxBufSize,
                             uint8_t * pRxBuffer,
                             uint16_t * pRxLength
                             )
{
    int ret;
    uint16_t wLength;
    struct spi_buf tx_buf;
    struct spi_buf rx_buf;
    struct spi_buf_set tx_set;
    struct spi_buf_set rx_set;
    const struct spi_buf_set * pTxSet = NULL;
    const struct spi_buf_set * pRxSet = NULL;

    (void)pDataParams;
    (void)wOption;

    if ((pTxBuffer == NULL) && (pRxBuffer == NULL))
    {
        return (PH_DRIVER_ERROR | PH_COMP_DRIVER);
    }

    if (pTxBuffer != NULL)
    {
        tx_buf.buf   = pTxBuffer;
        tx_buf.len   = wTxLength;
        tx_set.buffers = &tx_buf;
        tx_set.count   = 1;
        pTxSet = &tx_set;
    }

    if (pRxBuffer != NULL)
    {
        rx_buf.buf   = pRxBuffer;
        rx_buf.len   = wRxBufSize;
        rx_set.buffers = &rx_buf;
        rx_set.count   = 1;
        pRxSet = &rx_set;
    }

    ret = spi_transceive_dt(&spi_dev, pTxSet, pRxSet);
    if (ret != 0)
    {
        return (PH_DRIVER_FAILURE | PH_COMP_DRIVER);
    }

    /* Full-duplex transfer length is dictated by whichever side actually provided a buffer. */
    wLength = (pTxBuffer != NULL) ? wTxLength : wRxBufSize;

    if (pRxLength != NULL)
    {
        *pRxLength = wLength;
    }

    return PH_DRIVER_SUCCESS;
}

phStatus_t phbalReg_SetConfig(
                              void * pDataParams,
                              uint16_t wConfig,
                              uint32_t dwValue
                              )
{
    return PH_DRIVER_SUCCESS;
}

phStatus_t phbalReg_GetConfig(
                              void * pDataParams,
                              uint16_t wConfig,
                              uint32_t * pValue
                              )
{
    return PH_DRIVER_SUCCESS;
}
