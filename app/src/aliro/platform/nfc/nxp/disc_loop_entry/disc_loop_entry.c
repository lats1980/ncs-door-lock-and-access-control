#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "disc_loop_config.h"
#include <nxp_nfc_debug.h>

LOG_MODULE_DECLARE(nfc_st_NxpNfcRdLib_impl, CONFIG_DOOR_LOCK_NXPNFCRDLIB_LOG_LEVEL);

phacDiscLoop_Sw_DataParams_t *disc_loop;

#define NXP_NFC_DISC_LOOP_THREAD_PRIO  4
#define NXP_NFC_DISC_LOOP_THREAD_STACK_SIZE 4096
K_THREAD_STACK_DEFINE(disc_loop_thread_stack, NXP_NFC_DISC_LOOP_THREAD_STACK_SIZE);
static struct k_thread disc_loop_thread_data;

#if defined(CONFIG_NFC_NXP_PAL_I14443P3A) && \
	defined(CONFIG_NFC_NXP_PAL_I14443P4A) && \
	defined(CONFIG_NFC_NXP_PAL_I14443P4)
static uint8_t type_a_ats_buf[64];
#endif

static uint16_t saved_poll_tech_cfg;

static uint16_t nfc_forum_process(uint16_t entry_point, phStatus_t disc_loop_status)
{
	phStatus_t status;
	uint16_t tech_detected;
	uint16_t num_tags;
	uint16_t error_info;
	uint8_t tech_idx;

	if (entry_point != PHAC_DISCLOOP_ENTRY_POINT_POLL) {
		return PHAC_DISCLOOP_ENTRY_POINT_POLL;
	}

	if ((disc_loop_status & PH_ERR_MASK) == PHAC_DISCLOOP_MULTI_TECH_DETECTED) {
		LOG_INF("Multiple technology detected");

		status = phacDiscLoop_GetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_TECH_DETECTED,
						&tech_detected);
		if (status != PH_ERR_SUCCESS) {
			LOG_ERR("phacDiscLoop_GetConfig PHAC_DISCLOOP_CONFIG_TECH_DETECTED failed: 0x%04x",
				status);
			return PHAC_DISCLOOP_ENTRY_POINT_POLL;
		}

		if (PHAC_DISCLOOP_CHECK_ANDMASK(tech_detected, PHAC_DISCLOOP_POS_BIT_MASK_A)) {
			LOG_INF("Type A detected");
		}
		if (PHAC_DISCLOOP_CHECK_ANDMASK(tech_detected, PHAC_DISCLOOP_POS_BIT_MASK_B)) {
			LOG_INF("Type B detected");
		}

		for (tech_idx = 0; tech_idx < PHAC_DISCLOOP_PASS_POLL_MAX_TECHS_SUPPORTED;
		     tech_idx++) {
			if (PHAC_DISCLOOP_CHECK_ANDMASK(tech_detected, (1 << tech_idx))) {
				status = phacDiscLoop_SetConfig(disc_loop,
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

		nxp_nfc_debug_print_tech((1 << tech_idx));

		status = phacDiscLoop_SetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_NEXT_POLL_STATE,
						PHAC_DISCLOOP_POLL_STATE_COLLISION_RESOLUTION);
		if (status != PH_ERR_SUCCESS) {
			LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_NEXT_POLL_STATE failed: 0x%04x",
				status);
			return PHAC_DISCLOOP_ENTRY_POINT_POLL;
		}

		disc_loop_status = phacDiscLoop_Run(disc_loop, entry_point);
	}

	if ((disc_loop_status & PH_ERR_MASK) == PHAC_DISCLOOP_MULTI_DEVICES_RESOLVED) {
		status = phacDiscLoop_GetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_TECH_DETECTED,
						&tech_detected);
		if (status != PH_ERR_SUCCESS) {
			LOG_ERR("phacDiscLoop_GetConfig PHAC_DISCLOOP_CONFIG_TECH_DETECTED failed: 0x%04x",
				status);
			return PHAC_DISCLOOP_ENTRY_POINT_POLL;
		}

		status = phacDiscLoop_GetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_NR_TAGS_FOUND,
						&num_tags);
		if (status != PH_ERR_SUCCESS) {
			LOG_ERR("phacDiscLoop_GetConfig PHAC_DISCLOOP_CONFIG_NR_TAGS_FOUND failed: 0x%04x",
				status);
			return PHAC_DISCLOOP_ENTRY_POINT_POLL;
		}

		LOG_INF("Multiple cards resolved: %d cards", num_tags);
		nxp_nfc_debug_print_tag_info(disc_loop, num_tags, tech_detected);

		if (num_tags > 1) {
			for (tech_idx = 0; tech_idx < PHAC_DISCLOOP_PASS_POLL_MAX_TECHS_SUPPORTED;
			     tech_idx++) {
				if (PHAC_DISCLOOP_CHECK_ANDMASK(tech_detected, (1 << tech_idx))) {
					LOG_INF("Activating one card");
					status = phacDiscLoop_ActivateCard(disc_loop, tech_idx, 0);
					break;
				}
			}

			if ((status & PH_ERR_MASK) == PHAC_DISCLOOP_DEVICE_ACTIVATED) {
				status = phacDiscLoop_GetConfig(disc_loop,
								PHAC_DISCLOOP_CONFIG_TECH_DETECTED,
								&tech_detected);
				if (status != PH_ERR_SUCCESS) {
					LOG_ERR("phacDiscLoop_GetConfig PHAC_DISCLOOP_CONFIG_TECH_DETECTED failed: 0x%04x",
						status);
					return PHAC_DISCLOOP_ENTRY_POINT_POLL;
				}

				nxp_nfc_debug_print_tag_info(disc_loop, 0x01, tech_detected);
			} else {
				LOG_ERR("Card activation failed");
			}
		}
	} else if ((disc_loop_status & PH_ERR_MASK) == PHAC_DISCLOOP_DEVICE_ACTIVATED) {
		LOG_INF("Card detected and activated successfully");
		status = phacDiscLoop_GetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_NR_TAGS_FOUND,
						&num_tags);
		if (status != PH_ERR_SUCCESS) {
			LOG_ERR("phacDiscLoop_GetConfig PHAC_DISCLOOP_CONFIG_NR_TAGS_FOUND failed: 0x%04x",
				status);
			return PHAC_DISCLOOP_ENTRY_POINT_POLL;
		}

		status = phacDiscLoop_GetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_TECH_DETECTED,
						&tech_detected);
		if (status != PH_ERR_SUCCESS) {
			LOG_ERR("phacDiscLoop_GetConfig PHAC_DISCLOOP_CONFIG_TECH_DETECTED failed: 0x%04x",
				status);
			return PHAC_DISCLOOP_ENTRY_POINT_POLL;
		}

		nxp_nfc_debug_print_tag_info(disc_loop, num_tags, tech_detected);
	} else if ((disc_loop_status & PH_ERR_MASK) != PHAC_DISCLOOP_NO_TECH_DETECTED &&
		   (disc_loop_status & PH_ERR_MASK) != PHAC_DISCLOOP_NO_DEVICE_RESOLVED &&
		   (disc_loop_status & PH_ERR_MASK) != PHAC_DISCLOOP_LPCD_NO_TECH_DETECTED) {
		if ((disc_loop_status & PH_ERR_MASK) == PHAC_DISCLOOP_FAILURE) {
			status = phacDiscLoop_GetConfig(disc_loop,
							PHAC_DISCLOOP_CONFIG_ADDITIONAL_INFO,
							&error_info);
			if (status != PH_ERR_SUCCESS) {
				LOG_ERR("phacDiscLoop_GetConfig PHAC_DISCLOOP_CONFIG_ADDITIONAL_INFO failed: 0x%04x",
					status);
				return PHAC_DISCLOOP_ENTRY_POINT_POLL;
			}
			nxp_nfc_debug_print_error_info(error_info);
		} else {
			nxp_nfc_debug_print_error_info(disc_loop_status);
		}
	}

	return PHAC_DISCLOOP_ENTRY_POINT_POLL;
}

static void disc_loop_run(void *p1, void *p2, void *p3);

int nxp_nfc_lib_init(void)
{
  phNfcLib_Status_t nfc_lib_status;

  nfc_lib_status = phNfcLib_Init();
  
  if (nfc_lib_status != PH_NFCLIB_STATUS_SUCCESS) {

	  return  -1;
  }

  return 0;
}

int disc_loop_pn5190_entry(void)
{
	int ret;
	
	ret = 0;

	do {

		hal = phNfcLib_GetDataParams(PH_COMP_HAL);
		disc_loop = phNfcLib_GetDataParams(PH_COMP_AC_DISCLOOP);

#if defined(CONFIG_NFC_NXP_PAL_I14443P3A) && \
	defined(CONFIG_NFC_NXP_PAL_I14443P4A) && \
	defined(CONFIG_NFC_NXP_PAL_I14443P4)
		disc_loop->sTypeATargetInfo.sTypeA_I3P4.pAts = type_a_ats_buf;
#endif

		if (nxp_nfc_start_irq_monitor(disc_loop_irq_handler) != 0) {
			ret = -1;
			break;
		}
       
        if (k_thread_create(&disc_loop_thread_data, disc_loop_thread_stack,
			K_THREAD_STACK_SIZEOF(disc_loop_thread_stack),
			disc_loop_run, NULL, NULL, NULL,
			NXP_NFC_DISC_LOOP_THREAD_PRIO, 0, K_NO_WAIT) == NULL)
        {
          ret = -2;
		  break;
        }
	} while (0);

	return ret;
}

/**
 * Run the discovery loop in poll mode.
 *
 * Detects and reports NFC technology types. Optionally applies a discovery
 * loop profile and enables LPCD based on Kconfig. Does not return.
 */
static void disc_loop_run(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	phStatus_t status;
	phStatus_t tmp_status;
	uint16_t entry_point;
#ifdef CONFIG_NCS_NXP_DISCOVERY_LOOP_DISC_CONFIG
	phacDiscLoop_Profile_t profile = PHAC_DISCLOOP_PROFILE_NFC;

	status = disc_loop_apply_profile(disc_loop, profile);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("disc_loop_apply_profile failed: 0x%04x", status);
		return;
	}
#endif /* CONFIG_NCS_NXP_DISCOVERY_LOOP_DISC_CONFIG */

	status = phacDiscLoop_GetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_PAS_POLL_TECH_CFG,
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
		tmp_status = phacDiscLoop_SetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_NEXT_POLL_STATE,
						    PHAC_DISCLOOP_POLL_STATE_DETECTION);
		if (tmp_status != PH_ERR_SUCCESS) {
			LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_NEXT_POLL_STATE failed: 0x%04x",
				tmp_status);
			return;
		}

#if defined(CONFIG_NCS_NXP_DISCOVERY_LOOP_LPCD)
		if ((status & PH_ERR_MASK) == PHAC_DISCLOOP_LPCD_NO_TECH_DETECTED) {
			status = disc_loop_configure_lpcd();
			if (status != PH_ERR_SUCCESS) {
				LOG_ERR("disc_loop_configure_lpcd failed: 0x%04x", status);
				return;
			}
		}

		status = phacDiscLoop_SetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_ENABLE_LPCD, PH_ON);
		if (status != PH_ERR_SUCCESS) {
			LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_ENABLE_LPCD failed: 0x%04x",
				status);
			return;
		}
#endif /* CONFIG_NCS_NXP_DISCOVERY_LOOP_LPCD */

		status = phacDiscLoop_Run(disc_loop, entry_point);

		entry_point = nfc_forum_process(entry_point, status);

		tmp_status = phacDiscLoop_SetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_PAS_POLL_TECH_CFG,
						    saved_poll_tech_cfg);
		if (tmp_status != PH_ERR_SUCCESS) {
			LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_PAS_POLL_TECH_CFG failed: 0x%04x",
				tmp_status);
			return;
		}

		tmp_status = phhalHw_FieldOff(hal);
		if (tmp_status != PH_ERR_SUCCESS) {
			LOG_ERR("phhalHw_FieldOff failed: 0x%04x", tmp_status);
			return;
		}

		tmp_status = phhalHw_Wait(hal, PHHAL_HW_TIME_MICROSECONDS, 5100);
		if (tmp_status != PH_ERR_SUCCESS) {
			LOG_ERR("phhalHw_Wait failed: 0x%04x", tmp_status);
			return;
		}
	}
}
