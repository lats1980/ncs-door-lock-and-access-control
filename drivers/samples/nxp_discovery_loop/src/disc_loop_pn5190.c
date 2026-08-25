#include <ph_Status.h>

#ifdef NXPBUILD__PHHAL_HW_PN5190

#include <disc_loop_config.h>

#include <BoardSelection.h>
#include <phbalReg.h>
#include <phhalHw_Pn5190_Instr.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nxp_discovery_loop, CONFIG_NCS_NXP_DISCOVERY_LOOP_SAMPLE_LOG_LEVEL);

phhalHw_Pn5190_DataParams_t *hal;

#define PN5190_DIRECTION_BYTE_LEN 0x01U
#define PN5190_TYPE_FIELD_LEN     0x01U
#define PN5190_LENGTH_FIELD_LEN   0x02U

static volatile uint8_t skip_event_post;

static void pn5190_async_callback(void)
{
	uint32_t event_status_reg = 0x0U;
	uint16_t event_len = 0x0U;
	uint8_t backup = 0U;
	uint8_t spi_read_len = 0U;

	event_len = (uint16_t)hal->sIrqResp.pIsrEvtBuffPtr[2];
	event_len <<= 8;
	event_len |= ((uint16_t)hal->sIrqResp.pIsrEvtBuffPtr[3]);

	event_status_reg = (uint32_t)hal->sIrqResp.pIsrEvtBuffPtr[4];
	event_status_reg |= ((uint32_t)hal->sIrqResp.pIsrEvtBuffPtr[5]) << 8U;
	event_status_reg |= ((uint32_t)hal->sIrqResp.pIsrEvtBuffPtr[6]) << 16U;
	event_status_reg |= ((uint32_t)hal->sIrqResp.pIsrEvtBuffPtr[7]) << 24U;

	if ((event_status_reg & PH_PN5190_EVT_TX_OVERCURRENT_ERROR) != 0U) {
		LOG_INF("Received TX Over Current Event... ");
		(void)phhalHw_AsyncAbort(hal);
	}
	if ((event_status_reg & PH_PN5190_EVT_CTS) != 0U) {
		LOG_INF("Received CTS Event... ");
		(void)phhalHw_SetConfig(hal, PHHAL_HW_PN5190_CONFIG_CTS_EVENT_STATUS, PH_ON);
	}
	if ((event_status_reg & PH_PN5190_EVT_RFON_DETECT) != 0U) {
		phStatus_t status;
		uint16_t response_len = 0U;
		uint16_t rx_length;

		(void)phhalHw_SetConfig(hal, PHHAL_HW_PN5190_CONFIG_RF_ON_EVENT_STATUS, PH_ON);
		skip_event_post = 0x1U;

		spi_read_len = (hal->sIrqResp.bIsrBytesRead -
				(PN5190_DIRECTION_BYTE_LEN + PN5190_TYPE_FIELD_LEN +
				 PN5190_LENGTH_FIELD_LEN));

		if (event_len > spi_read_len) {
			rx_length = (uint16_t)(event_len - spi_read_len + PN5190_DIRECTION_BYTE_LEN);
			backup = hal->sIrqResp.pIsrEvtBuffPtr[event_len - 1];

			status = phbalReg_Exchange(NULL, PH_EXCHANGE_DEFAULT, NULL, 0U, rx_length,
						   &hal->sIrqResp.pIsrEvtBuffPtr[event_len - 1],
						   &response_len);

			if ((status != PH_DRIVER_SUCCESS) || (response_len != rx_length)) {
				return;
			}

			hal->sIrqResp.pIsrEvtBuffPtr[event_len - 1] = backup;
		}
	}
	if ((event_status_reg & (0xFFFFFFFF << 0x0C)) != 0U) {
		LOG_INF("Received Unexpected Event that should not be reported by PN5190... ");
	}
}

phStatus_t disc_loop_configure_lpcd(void)
{
	phStatus_t status = PH_ERR_SUCCESS;

	status = phhalHw_Pn5190_Instr_LPCD_SetConfig(hal, PHHAL_HW_CONFIG_SET_LPCD_WAKEUPTIME_MS,
						     100U);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phhalHw_Pn5190_Instr_LPCD_SetConfig WAKEUPTIME_MS failed: 0x%04x", status);
		return status;
	}

	status = phhalHw_Pn5190_Instr_LPCD_SetConfig(hal, PHHAL_HW_CONFIG_LPCD_MODE,
						     PHHAL_HW_PN5190_LPCD_MODE_DEFAULT);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phhalHw_Pn5190_Instr_LPCD_SetConfig LPCD_MODE DEFAULT failed: 0x%04x", status);
		return status;
	}

	status = phhalHw_Pn5190_Instr_LPCD_SetConfig(hal, PHHAL_HW_CONFIG_LPCD_CONFIG,
						     PHHAL_HW_PN5190_LPCD_CTRL_LPCD_CALIB);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phhalHw_Pn5190_Instr_LPCD_SetConfig LPCD_CALIB failed: 0x%04x", status);
		return status;
	}

	status = phhalHw_Lpcd(hal);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phhalHw_Lpcd failed: 0x%04x", status);
		return status;
	}

	status = phhalHw_Pn5190_Instr_LPCD_SetConfig(hal, PHHAL_HW_CONFIG_SET_LPCD_WAKEUPTIME_MS,
						     330U);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phhalHw_Pn5190_Instr_LPCD_SetConfig WAKEUPTIME_MS 330 failed: 0x%04x", status);
		return status;
	}

	status = phhalHw_Pn5190_Instr_LPCD_SetConfig(hal, PHHAL_HW_CONFIG_LPCD_MODE,
						     PHHAL_HW_PN5190_LPCD_MODE_POWERDOWN);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phhalHw_Pn5190_Instr_LPCD_SetConfig LPCD_MODE POWERDOWN failed: 0x%04x",
			status);
		return status;
	}

	status = phhalHw_Pn5190_Instr_LPCD_SetConfig(hal, PHHAL_HW_CONFIG_LPCD_CONFIG,
						     PHHAL_HW_PN5190_LPCD_CTRL_LPCD);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phhalHw_Pn5190_Instr_LPCD_SetConfig LPCD_CTRL failed: 0x%04x", status);
		return status;
	}

	return status;
}

void disc_loop_irq_handler(void)
{
	if (phDriver_PinRead(PHDRIVER_PIN_IRQ, PH_DRIVER_PINFUNC_INTERRUPT)) {
		phDriver_PinClearIntStatus(PHDRIVER_PIN_IRQ);

		if ((hal->wId == (PH_COMP_HAL | PHHAL_HW_PN5190_ID)) &&
		    (hal->pRFISRCallback != NULL)) {
			phStatus_t status;
			uint16_t response_len = 0U;
			uint16_t rx_length = (uint16_t)hal->sIrqResp.bIsrBytesRead;

			status = phbalReg_Exchange(NULL, PH_EXCHANGE_DEFAULT, NULL, 0U, rx_length,
						   hal->sIrqResp.pHandlerModeBuffPtr,
						   &response_len);

			if ((status != PH_DRIVER_SUCCESS) || (response_len != rx_length)) {
				return;
			}

			if (hal->sIrqResp.pHandlerModeBuffPtr[1] == PH_PN5190_EVT_RSP) {
				hal->sIrqResp.pIsrEvtBuffPtr = hal->sIrqResp.pHandlerModeBuffPtr;

				pn5190_async_callback();

				if (skip_event_post == 0x0U) {
					hal->pRFISRCallback(hal);
				}
				skip_event_post = 0x0U;
			} else {
				hal->sIrqResp.pIsrBuffPtr = hal->sIrqResp.pHandlerModeBuffPtr;
				hal->pRFISRCallback(hal);
			}

			if (hal->sIrqResp.pHandlerModeBuffPtr == &hal->sIrqResp.aISRReadBuf[0]) {
				hal->sIrqResp.pHandlerModeBuffPtr =
					&hal->sIrqResp.aISRReadBuf2[0];
			} else {
				hal->sIrqResp.pHandlerModeBuffPtr =
					&hal->sIrqResp.aISRReadBuf[0];
			}
		}
	}
}

#endif /* NXPBUILD__PHHAL_HW_PN5190 */
