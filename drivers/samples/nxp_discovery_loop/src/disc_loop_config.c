
#include <disc_loop_config.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nxp_discovery_loop, CONFIG_NCS_NXP_DISCOVERY_LOOP_SAMPLE_LOG_LEVEL);

#ifdef CONFIG_NCS_NXP_DISCOVERY_LOOP_DISC_CONFIG

/**
* Configure the discovery loop with default values for the selected profile.
* Application can read these values from EEPROM and apply them via SetConfig.
 * \param   disc_loop   Discovery loop data parameters
* \param   profile     Reader Library profile
* \note    Values used below are default and are for demonstration purpose.
*/
phStatus_t disc_loop_apply_profile(phacDiscLoop_Sw_DataParams_t *disc_loop,
                               phacDiscLoop_Profile_t profile)
{
    phStatus_t status = PH_ERR_SUCCESS;
    uint16_t   pas_poll_config = 0;

#ifdef NXPBUILD__PHAC_DISCLOOP_TYPEA_TAGS
    pas_poll_config |= PHAC_DISCLOOP_POS_BIT_MASK_A;
#endif
#ifdef NXPBUILD__PHAC_DISCLOOP_TYPEB_TAGS
    pas_poll_config |= PHAC_DISCLOOP_POS_BIT_MASK_B;
#endif

    if (profile == PHAC_DISCLOOP_PROFILE_NFC) {
        /* passive Bailout bitmap config. */
        status = phacDiscLoop_SetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_BAIL_OUT, 0x00);
        if (status != PH_ERR_SUCCESS) {
            LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_BAIL_OUT failed: 0x%04x", status);
            return status;
        }

        /* Set Passive poll bitmap config. */
        status = phacDiscLoop_SetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_PAS_POLL_TECH_CFG, pas_poll_config);
        if (status != PH_ERR_SUCCESS) {
            LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_PAS_POLL_TECH_CFG failed: 0x%04x", status);
            return status;
        }

        /* Set Active poll bitmap config. */
        status = phacDiscLoop_SetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_ACT_POLL_TECH_CFG, 0);
        if (status != PH_ERR_SUCCESS) {
            LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_ACT_POLL_TECH_CFG failed: 0x%04x", status);
            return status;
        }

        /* reset collision Pending */
        status = phacDiscLoop_SetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_COLLISION_PENDING, PH_OFF);
        if (status != PH_ERR_SUCCESS) {
            LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_COLLISION_PENDING failed: 0x%04x", status);
            return status;
        }

        /* whether anti-collision is supported or not. */
        status = phacDiscLoop_SetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_ANTI_COLL, PH_ON);
        if (status != PH_ERR_SUCCESS) {
            LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_ANTI_COLL failed: 0x%04x", status);
            return status;
        }

        /* Poll Mode default state*/
        status = phacDiscLoop_SetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_NEXT_POLL_STATE, PHAC_DISCLOOP_POLL_STATE_DETECTION);
        if (status != PH_ERR_SUCCESS) {
            LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_NEXT_POLL_STATE failed: 0x%04x", status);
            return status;
        }

#ifdef  NXPBUILD__PHAC_DISCLOOP_TYPEA_TAGS
        /* Device limit for Type A */
        status = phacDiscLoop_SetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_TYPEA_DEVICE_LIMIT, 1);
        if (status != PH_ERR_SUCCESS) {
            LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_TYPEA_DEVICE_LIMIT failed: 0x%04x", status);
            return status;
        }

        /* Passive polling Tx Guard times in micro seconds. */
        status = phacDiscLoop_SetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_GTA_VALUE_US, 5100);
        if (status != PH_ERR_SUCCESS) {
            LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_GTA_VALUE_US failed: 0x%04x", status);
            return status;
        }
#endif

#ifdef NXPBUILD__PHAC_DISCLOOP_TYPEB_TAGS
        /* Device limit for Type B */
        status = phacDiscLoop_SetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_TYPEB_DEVICE_LIMIT, 1);
        if (status != PH_ERR_SUCCESS) {
            LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_TYPEB_DEVICE_LIMIT failed: 0x%04x", status);
            return status;
        }

        status = phacDiscLoop_SetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_GTB_VALUE_US, 5100);
        if (status != PH_ERR_SUCCESS) {
            LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_GTB_VALUE_US failed: 0x%04x", status);
            return status;
        }
#endif

        /* Discovery loop Operation mode */
        status = phacDiscLoop_SetConfig(disc_loop, PHAC_DISCLOOP_CONFIG_OPE_MODE, RD_LIB_MODE_NFC);
        if (status != PH_ERR_SUCCESS) {
            LOG_ERR("phacDiscLoop_SetConfig PHAC_DISCLOOP_CONFIG_OPE_MODE failed: 0x%04x", status);
            return status;
        }
    } else {
        /* Do Nothing */
    }

    return status;
}

#endif /* CONFIG_NCS_NXP_DISCOVERY_LOOP_DISC_CONFIG */
