#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "nxp_nfc_discovery_platform.h"
#include "nxp_nfc_isodep.h"
#if defined(CONFIG_NFC_NXP_DEBUG_HELPERS)
#include <nxp_nfc_debug.h>
#endif

LOG_MODULE_DECLARE(nfc_st_NxpNfcRdLib_impl, CONFIG_DOOR_LOCK_NXPNFCRDLIB_LOG_LEVEL);

phacDiscLoop_Sw_DataParams_t *s_disc_loop_params;

#define NXP_NFC_DISCOVERY_THREAD_PRIO  4
#define NXP_NFC_DISCOVERY_THREAD_STACK_SIZE 4096
K_THREAD_STACK_DEFINE(nxp_nfc_discovery_thread_stack, NXP_NFC_DISCOVERY_THREAD_STACK_SIZE);
static struct k_thread nxp_nfc_discovery_thread_data;

static uint8_t type_a_ats_buf[64];

static uint16_t saved_poll_tech_cfg;

static uint16_t nxp_nfc_discovery_handle_status(uint16_t entry_point, phStatus_t discovery_status)
{
	phStatus_t status;
	uint16_t tech_detected;
	uint16_t num_tags;
	uint16_t error_info;
	uint8_t tech_idx;

	if (entry_point != PHAC_DISCLOOP_ENTRY_POINT_POLL) {
		return PHAC_DISCLOOP_ENTRY_POINT_POLL;
	}

	if ((discovery_status & PH_ERR_MASK) == PHAC_DISCLOOP_MULTI_TECH_DETECTED) {
		LOG_INF("Multiple technology detected");

		status = phacDiscLoop_GetConfig(s_disc_loop_params, PHAC_DISCLOOP_CONFIG_TECH_DETECTED,
						&tech_detected);
		if (status != PH_ERR_SUCCESS) {
			LOG_ERR("phacDiscLoop_GetConfig PHAC_DISCLOOP_CONFIG_TECH_DETECTED failed: 0x%04x",
				status);
			return PHAC_DISCLOOP_ENTRY_POINT_POLL;
		}

		if (!PHAC_DISCLOOP_CHECK_ANDMASK(tech_detected, PHAC_DISCLOOP_POS_BIT_MASK_A)) {
			LOG_INF("Non-Type A technology detected (0x%04x), ignoring", tech_detected);
			return PHAC_DISCLOOP_ENTRY_POINT_POLL;
		}

		LOG_INF("Type A detected");

		for (tech_idx = 0; tech_idx < PHAC_DISCLOOP_PASS_POLL_MAX_TECHS_SUPPORTED;
		     tech_idx++) {
			if (PHAC_DISCLOOP_CHECK_ANDMASK(tech_detected, (1 << tech_idx))) {
				status = phacDiscLoop_SetConfig(s_disc_loop_params,
								PHAC_DISCLOOP_CONFIG_PAS_POLL_TECH_CFG,
								(1 << tech_idx));
				if (status != PH_ERR_SUCCESS) {
					LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_PAS_POLL_TECH_CFG failed: 0x%04x",
						status);
					return PHAC_DISCLOOP_ENTRY_POINT_POLL;
				}
				break;
			}
		}
#if defined(CONFIG_NFC_NXP_DEBUG_HELPERS)
		nxp_nfc_debug_print_tech((1 << tech_idx));
#endif
		status = phacDiscLoop_SetConfig(s_disc_loop_params, PHAC_DISCLOOP_CONFIG_NEXT_POLL_STATE,
						PHAC_DISCLOOP_POLL_STATE_COLLISION_RESOLUTION);
		if (status != PH_ERR_SUCCESS) {
			LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_NEXT_POLL_STATE failed: 0x%04x",
				status);
			return PHAC_DISCLOOP_ENTRY_POINT_POLL;
		}

		discovery_status = phacDiscLoop_Run(s_disc_loop_params, entry_point);
	}

	if ((discovery_status & PH_ERR_MASK) == PHAC_DISCLOOP_MULTI_DEVICES_RESOLVED) {
		status = phacDiscLoop_GetConfig(s_disc_loop_params, PHAC_DISCLOOP_CONFIG_TECH_DETECTED,
						&tech_detected);
		if (status != PH_ERR_SUCCESS) {
			LOG_ERR("phacDiscLoop_GetConfig PHAC_DISCLOOP_CONFIG_TECH_DETECTED failed: 0x%04x",
				status);
			return PHAC_DISCLOOP_ENTRY_POINT_POLL;
		}

		status = phacDiscLoop_GetConfig(s_disc_loop_params, PHAC_DISCLOOP_CONFIG_NR_TAGS_FOUND,
						&num_tags);
		if (status != PH_ERR_SUCCESS) {
			LOG_ERR("phacDiscLoop_GetConfig PHAC_DISCLOOP_CONFIG_NR_TAGS_FOUND failed: 0x%04x",
				status);
			return PHAC_DISCLOOP_ENTRY_POINT_POLL;
		}

		LOG_INF("Multiple cards resolved: %d cards", num_tags);
#if defined(CONFIG_NFC_NXP_DEBUG_HELPERS)
		nxp_nfc_debug_print_tag_info(s_disc_loop_params, num_tags, tech_detected);
#endif

		if (num_tags > 1) {
			for (tech_idx = 0; tech_idx < PHAC_DISCLOOP_PASS_POLL_MAX_TECHS_SUPPORTED;
			     tech_idx++) {
				if (PHAC_DISCLOOP_CHECK_ANDMASK(tech_detected, (1 << tech_idx))) {
					LOG_INF("Activating one card");
					status = phacDiscLoop_ActivateCard(s_disc_loop_params, tech_idx, 0);
					break;
				}
			}

			if ((status & PH_ERR_MASK) == PHAC_DISCLOOP_DEVICE_ACTIVATED) {
				status = phacDiscLoop_GetConfig(s_disc_loop_params,
								PHAC_DISCLOOP_CONFIG_TECH_DETECTED,
								&tech_detected);
				if (status != PH_ERR_SUCCESS) {
					LOG_ERR("phacDiscLoop_GetConfig PHAC_DISCLOOP_CONFIG_TECH_DETECTED failed: 0x%04x",
						status);
					return PHAC_DISCLOOP_ENTRY_POINT_POLL;
				}
#if defined(CONFIG_NFC_NXP_DEBUG_HELPERS)
				nxp_nfc_debug_print_tag_info(s_disc_loop_params, 0x01, tech_detected);
#endif
				nxp_nfc_isodep_on_activated(s_disc_loop_params);
			} else {
				LOG_ERR("Card activation failed");
			}
		} else {
			nxp_nfc_isodep_on_activated(s_disc_loop_params);
		}
	} else if ((discovery_status & PH_ERR_MASK) == PHAC_DISCLOOP_DEVICE_ACTIVATED) {
		LOG_INF("Card detected and activated successfully");
		status = phacDiscLoop_GetConfig(s_disc_loop_params, PHAC_DISCLOOP_CONFIG_NR_TAGS_FOUND,
						&num_tags);
		if (status != PH_ERR_SUCCESS) {
			LOG_ERR("phacDiscLoop_GetConfig PHAC_DISCLOOP_CONFIG_NR_TAGS_FOUND failed: 0x%04x",
				status);
			return PHAC_DISCLOOP_ENTRY_POINT_POLL;
		}

		status = phacDiscLoop_GetConfig(s_disc_loop_params, PHAC_DISCLOOP_CONFIG_TECH_DETECTED,
						&tech_detected);
		if (status != PH_ERR_SUCCESS) {
			LOG_ERR("phacDiscLoop_GetConfig PHAC_DISCLOOP_CONFIG_TECH_DETECTED failed: 0x%04x",
				status);
			return PHAC_DISCLOOP_ENTRY_POINT_POLL;
		}
#if defined(CONFIG_NFC_NXP_DEBUG_HELPERS)
		nxp_nfc_debug_print_tag_info(s_disc_loop_params, num_tags, tech_detected);
#endif
		nxp_nfc_isodep_on_activated(s_disc_loop_params);
	} else if ((discovery_status & PH_ERR_MASK) != PHAC_DISCLOOP_NO_TECH_DETECTED &&
		   (discovery_status & PH_ERR_MASK) != PHAC_DISCLOOP_NO_DEVICE_RESOLVED &&
		   (discovery_status & PH_ERR_MASK) != PHAC_DISCLOOP_LPCD_NO_TECH_DETECTED) {
		if ((discovery_status & PH_ERR_MASK) == PHAC_DISCLOOP_FAILURE) {
			status = phacDiscLoop_GetConfig(s_disc_loop_params,
							PHAC_DISCLOOP_CONFIG_ADDITIONAL_INFO,
							&error_info);
			if (status != PH_ERR_SUCCESS) {
				LOG_ERR("phacDiscLoop_GetConfig PHAC_DISCLOOP_CONFIG_ADDITIONAL_INFO failed: 0x%04x",
					status);
				return PHAC_DISCLOOP_ENTRY_POINT_POLL;
			}
#if defined(CONFIG_NFC_NXP_DEBUG_HELPERS)
			nxp_nfc_debug_print_error_info(error_info);
#endif
		} else {
#if defined(CONFIG_NFC_NXP_DEBUG_HELPERS)
			nxp_nfc_debug_print_error_info(discovery_status);
#endif
		}
	}

	return PHAC_DISCLOOP_ENTRY_POINT_POLL;
}

static phStatus_t nxp_nfc_discovery_configure(phacDiscLoop_Sw_DataParams_t *disc_loop_params)
{
	phStatus_t status = PH_ERR_SUCCESS;
	uint16_t pas_poll_config = 0;

	pas_poll_config |= PHAC_DISCLOOP_POS_BIT_MASK_A;

	status = phacDiscLoop_SetConfig(disc_loop_params, PHAC_DISCLOOP_CONFIG_BAIL_OUT, 0x00);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_BAIL_OUT failed: 0x%04x", status);
		return status;
	}

	status = phacDiscLoop_SetConfig(disc_loop_params, PHAC_DISCLOOP_CONFIG_PAS_POLL_TECH_CFG,
					pas_poll_config);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_PAS_POLL_TECH_CFG failed: 0x%04x",
			status);
		return status;
	}

	status = phacDiscLoop_SetConfig(disc_loop_params, PHAC_DISCLOOP_CONFIG_ACT_POLL_TECH_CFG, 0);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_ACT_POLL_TECH_CFG failed: 0x%04x",
			status);
		return status;
	}

	status = phacDiscLoop_SetConfig(disc_loop_params, PHAC_DISCLOOP_CONFIG_COLLISION_PENDING, PH_OFF);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_COLLISION_PENDING failed: 0x%04x",
			status);
		return status;
	}

	status = phacDiscLoop_SetConfig(disc_loop_params, PHAC_DISCLOOP_CONFIG_ANTI_COLL, PH_ON);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_ANTI_COLL failed: 0x%04x", status);
		return status;
	}

	status = phacDiscLoop_SetConfig(disc_loop_params, PHAC_DISCLOOP_CONFIG_NEXT_POLL_STATE,
					PHAC_DISCLOOP_POLL_STATE_DETECTION);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_NEXT_POLL_STATE failed: 0x%04x",
			status);
		return status;
	}

	status = phacDiscLoop_SetConfig(disc_loop_params, PHAC_DISCLOOP_CONFIG_TYPEA_DEVICE_LIMIT, 1);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_TYPEA_DEVICE_LIMIT failed: 0x%04x",
			status);
		return status;
	}

	status = phacDiscLoop_SetConfig(disc_loop_params, PHAC_DISCLOOP_CONFIG_GTA_VALUE_US, 5100);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_GTA_VALUE_US failed: 0x%04x", status);
		return status;
	}

	status = phacDiscLoop_SetConfig(disc_loop_params, PHAC_DISCLOOP_CONFIG_OPE_MODE, RD_LIB_MODE_NFC);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_OPE_MODE failed: 0x%04x", status);
		return status;
	}

	return status;
}

/**
 * Restore poll configuration and apply NFC Forum field-off guard time
 * before the next discovery iteration.
 */
static phStatus_t nxp_nfc_discovery_finish_poll_cycle(void)
{
	phStatus_t status;

	status = phacDiscLoop_SetConfig(s_disc_loop_params, PHAC_DISCLOOP_CONFIG_PAS_POLL_TECH_CFG,
					saved_poll_tech_cfg);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_PAS_POLL_TECH_CFG failed: 0x%04x",
			status);
		return status;
	}

	status = phhalHw_FieldOff(hal);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phhalHw_FieldOff failed: 0x%04x", status);
		return status;
	}

	status = phhalHw_Wait(hal, PHHAL_HW_TIME_MICROSECONDS, 5100);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phhalHw_Wait failed: 0x%04x", status);
		return status;
	}

	return PH_ERR_SUCCESS;
}

static void nxp_nfc_discovery_worker(void *p1, void *p2, void *p3);

int nxp_nfc_lib_init(void)
{
  phNfcLib_Status_t nfc_lib_status;

  nfc_lib_status = phNfcLib_Init();
  
  if (nfc_lib_status != PH_NFCLIB_STATUS_SUCCESS) {

	  return  -1;
  }

  return 0;
}

int nxp_nfc_discovery_start(void)
{
	int ret;
	
	ret = 0;

	do {

		hal = phNfcLib_GetDataParams(PH_COMP_HAL);
		s_disc_loop_params = phNfcLib_GetDataParams(PH_COMP_AC_DISCLOOP);

		s_disc_loop_params->sTypeATargetInfo.sTypeA_I3P4.pAts = type_a_ats_buf;

		if (nxp_nfc_start_irq_monitor(nxp_nfc_irq_handler) != 0) {
			ret = -1;
			break;
		}
       
        if (k_thread_create(&nxp_nfc_discovery_thread_data, nxp_nfc_discovery_thread_stack,
			K_THREAD_STACK_SIZEOF(nxp_nfc_discovery_thread_stack),
			nxp_nfc_discovery_worker, NULL, NULL, NULL,
			NXP_NFC_DISCOVERY_THREAD_PRIO, 0, K_NO_WAIT) == NULL)
        {
          ret = -2;
		  break;
        }
	} while (0);

	return ret;
}

/**
 * Run NFC discovery in poll mode.
 *
 * Applies fixed Aliro discovery settings and optionally enables LPCD based on
 * Kconfig. Does not return.
 */
static void nxp_nfc_discovery_worker(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	phStatus_t status;
	phStatus_t tmp_status;
	uint16_t entry_point;

	status = nxp_nfc_discovery_configure(s_disc_loop_params);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("nxp_nfc_discovery_configure failed: 0x%04x", status);
		return;
	}

	status = phacDiscLoop_GetConfig(s_disc_loop_params, PHAC_DISCLOOP_CONFIG_PAS_POLL_TECH_CFG,
					&saved_poll_tech_cfg);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phacDiscLoop_GetConfig PHAC_DISCLOOP_CONFIG_PAS_POLL_TECH_CFG failed: 0x%04x",
			status);
		return;
	}

	entry_point = PHAC_DISCLOOP_ENTRY_POINT_POLL;
	status = PHAC_DISCLOOP_LPCD_NO_TECH_DETECTED;

	tmp_status = phhalHw_FieldOff(hal);
	if (tmp_status != PH_ERR_SUCCESS) {
		LOG_ERR("phhalHw_FieldOff failed: 0x%04x", tmp_status);
		return;
	}

	while (true) {
		tmp_status = phacDiscLoop_SetConfig(s_disc_loop_params, PHAC_DISCLOOP_CONFIG_NEXT_POLL_STATE,
						    PHAC_DISCLOOP_POLL_STATE_DETECTION);
		if (tmp_status != PH_ERR_SUCCESS) {
			LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_NEXT_POLL_STATE failed: 0x%04x",
				tmp_status);
			return;
		}

#if defined(CONFIG_NCS_NXP_DISCOVERY_LOOP_LPCD)
		if ((status & PH_ERR_MASK) == PHAC_DISCLOOP_LPCD_NO_TECH_DETECTED) {
			status = nxp_nfc_configure_lpcd();
			if (status != PH_ERR_SUCCESS) {
				LOG_ERR("nxp_nfc_configure_lpcd failed: 0x%04x", status);
				return;
			}
		}

		status = phacDiscLoop_SetConfig(s_disc_loop_params, PHAC_DISCLOOP_CONFIG_ENABLE_LPCD, PH_ON);
		if (status != PH_ERR_SUCCESS) {
			LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_ENABLE_LPCD failed: 0x%04x",
				status);
			return;
		}
#endif /* CONFIG_NCS_NXP_DISCOVERY_LOOP_LPCD */

		/* Run the discovery loop to detect NFC tags */
		status = phacDiscLoop_Run(s_disc_loop_params, entry_point);

		entry_point = nxp_nfc_discovery_handle_status(entry_point, status);

		if (nxp_nfc_isodep_session_active()) {
			nxp_nfc_isodep_run_session(s_disc_loop_params);
			nxp_nfc_isodep_cleanup(s_disc_loop_params);
		} else {
			/* No Type 4A ISO-DEP session; proceed to standard poll cycle cleanup. */
		}

		tmp_status = nxp_nfc_discovery_finish_poll_cycle();
		if (tmp_status != PH_ERR_SUCCESS) {
			return;
		}
	}
}
