#include <ph_Status.h>

#ifdef NXPBUILD__PHHAL_HW_PN5180

#include "nxp_nfc_discovery_platform.h"

#include <BoardSelection.h>
#include <phhalHw_Pn5180_Instr.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nfc_st_NxpNfcRdLib_impl, CONFIG_DOOR_LOCK_NXPNFCRDLIB_LOG_LEVEL);

phhalHw_Pn5180_DataParams_t *hal;

phStatus_t nxp_nfc_configure_lpcd(void)
{
	phStatus_t status;
	uint8_t lpcd_threshold_eeprom_addr = 0x37U;
	uint8_t lpcd_threshold = 0x10U;
	uint16_t lpcd_mode = PHHAL_HW_PN5180_LPCD_MODE_POWERDOWN;

	status = phhalHw_Pn5180_Instr_WriteE2Prom(hal, lpcd_threshold_eeprom_addr,
						  &lpcd_threshold, 1U);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phhalHw_Pn5180_Instr_WriteE2Prom LPCD threshold failed: 0x%04x", status);
		return status;
	}

	status = phhalHw_Pn5180_Int_LPCD_SetConfig(hal, PHHAL_HW_CONFIG_LPCD_MODE, lpcd_mode);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phhalHw_Pn5180_Int_LPCD_SetConfig LPCD_MODE failed: 0x%04x", status);
		return status;
	}

	return status;
}

void nxp_nfc_irq_handler(void)
{
	if (phDriver_PinRead(PHDRIVER_PIN_IRQ, PH_DRIVER_PINFUNC_INTERRUPT)) {
		phDriver_PinClearIntStatus(PHDRIVER_PIN_IRQ);

		if (hal->pRFISRCallback != NULL) {
			hal->pRFISRCallback(hal);
		}
	}
}

#endif /* NXPBUILD__PHHAL_HW_PN5180 */
