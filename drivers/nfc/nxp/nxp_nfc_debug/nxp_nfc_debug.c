/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <nxp_nfc_debug.h>

#include <phDriver.h>
#include <phNfcLib.h>

#include <stdio.h>
#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(nxp_nfc_debug, CONFIG_NFC_LOG_LEVEL);

static void nxp_nfc_debug_format_hex(char *dest, size_t dest_size, const uint8_t *buff, uint8_t num);
static const char *nxp_nfc_debug_type_a_tag_type_name(uint8_t tag_type);
static const char *nxp_nfc_debug_comp_name(uint16_t comp);
static const char *nxp_nfc_debug_err_name(uint16_t err);

static void nxp_nfc_debug_format_hex(char *dest, size_t dest_size, const uint8_t *buff, uint8_t num)
{
	size_t pos = 0;

	for (uint8_t i = 0; i < num && pos + 2 < dest_size; i++) {
		pos += snprintf(dest + pos, dest_size - pos, "%02X", buff[i]);
	}
}

static const char *nxp_nfc_debug_type_a_tag_type_name(uint8_t tag_type)
{
	switch (tag_type) {
	case PHAC_DISCLOOP_TYPEA_TYPE2_TAG_CONFIG_MASK:
		return "Type 2 Tag";
	case PHAC_DISCLOOP_TYPEA_TYPE4A_TAG_CONFIG_MASK:
		return "Type 4A Tag";
	case PHAC_DISCLOOP_TYPEA_TYPE_NFC_DEP_TAG_CONFIG_MASK:
		return "P2P";
	case PHAC_DISCLOOP_TYPEA_TYPE_NFC_DEP_TYPE4A_TAG_CONFIG_MASK:
		return "NFC-DEP and 4A Tag";
	default:
		return "Unknown";
	}
}

static const char *nxp_nfc_debug_comp_name(uint16_t comp)
{
	switch (comp) {
	case PH_COMP_BAL:
		return "PH_COMP_BAL";
	case PH_COMP_HAL:
		return "PH_COMP_HAL";
	case PH_COMP_PAL_ISO14443P3A:
		return "PH_COMP_PAL_ISO14443P3A";
	case PH_COMP_PAL_ISO14443P3B:
		return "PH_COMP_PAL_ISO14443P3B";
	case PH_COMP_PAL_ISO14443P4A:
		return "PH_COMP_PAL_ISO14443P4A";
	case PH_COMP_PAL_ISO14443P4:
		return "PH_COMP_PAL_ISO14443P4";
	case PH_COMP_PAL_FELICA:
		return "PH_COMP_PAL_FELICA";
	case PH_COMP_PAL_EPCUID:
		return "PH_COMP_PAL_EPCUID";
	case PH_COMP_PAL_SLI15693:
		return "PH_COMP_PAL_SLI15693";
	case PH_COMP_PAL_I18000P3M3:
		return "PH_COMP_PAL_I18000P3M3";
	case PH_COMP_PAL_I18092MPI:
		return "PH_COMP_PAL_I18092MPI";
	case PH_COMP_PAL_I18092MT:
		return "PH_COMP_PAL_I18092MT";
	case PH_COMP_PAL_I14443P4MC:
		return "PH_COMP_PAL_I14443P4MC";
	case PH_COMP_AC_DISCLOOP:
		return "PH_COMP_AC_DISCLOOP";
	case PH_COMP_OSAL:
		return "PH_COMP_OSAL";
	default:
		return "unknown";
	}
}

static const char *nxp_nfc_debug_err_name(uint16_t err)
{
	switch (err) {
	case PH_ERR_SUCCESS_INCOMPLETE_BYTE:
		return "PH_ERR_SUCCESS_INCOMPLETE_BYTE";
	case PH_ERR_IO_TIMEOUT:
		return "PH_ERR_IO_TIMEOUT";
	case PH_ERR_INTEGRITY_ERROR:
		return "PH_ERR_INTEGRITY_ERROR";
	case PH_ERR_COLLISION_ERROR:
		return "PH_ERR_COLLISION_ERROR";
	case PH_ERR_BUFFER_OVERFLOW:
		return "PH_ERR_BUFFER_OVERFLOW";
	case PH_ERR_FRAMING_ERROR:
		return "PH_ERR_FRAMING_ERROR";
	case PH_ERR_PROTOCOL_ERROR:
		return "PH_ERR_PROTOCOL_ERROR";
	case PH_ERR_RF_ERROR:
		return "PH_ERR_RF_ERROR";
	case PH_ERR_EXT_RF_ERROR:
		return "PH_ERR_EXT_RF_ERROR";
	case PH_ERR_NOISE_ERROR:
		return "PH_ERR_NOISE_ERROR";
	case PH_ERR_ABORTED:
		return "PH_ERR_ABORTED";
	case PH_ERR_INTERNAL_ERROR:
		return "PH_ERR_INTERNAL_ERROR";
	case PH_ERR_INVALID_DATA_PARAMS:
		return "PH_ERR_INVALID_DATA_PARAMS";
	case PH_ERR_INVALID_PARAMETER:
		return "PH_ERR_INVALID_PARAMETER";
	case PH_ERR_PARAMETER_OVERFLOW:
		return "PH_ERR_PARAMETER_OVERFLOW";
	case PH_ERR_UNSUPPORTED_PARAMETER:
		return "PH_ERR_UNSUPPORTED_PARAMETER";
	case PH_ERR_OSAL_ERROR:
		return "PH_ERR_OSAL_ERROR";
	case PHAC_DISCLOOP_LPCD_NO_TECH_DETECTED:
		return "PHAC_DISCLOOP_LPCD_NO_TECH_DETECTED";
	case PHAC_DISCLOOP_COLLISION_PENDING:
		return "PHAC_DISCLOOP_COLLISION_PENDING";
	default:
		return "unknown";
	}
}

void nxp_nfc_debug_print_tech(uint8_t tech_type)
{
	switch (tech_type) {
	case PHAC_DISCLOOP_POS_BIT_MASK_A:
		LOG_INF("Resolving Type A");
		break;

	case PHAC_DISCLOOP_POS_BIT_MASK_B:
		LOG_INF("Resolving Type B");
		break;

	case PHAC_DISCLOOP_POS_BIT_MASK_F212:
		LOG_INF("Resolving Type F with baud rate 212");
		break;

	case PHAC_DISCLOOP_POS_BIT_MASK_F424:
		LOG_INF("Resolving Type F with baud rate 424");
		break;

	case PHAC_DISCLOOP_POS_BIT_MASK_V:
		LOG_INF("Resolving Type V");
		break;

	default:
		break;
	}
}

void nxp_nfc_debug_print_buff(uint8_t *buff, uint8_t num)
{
	char hex[128];

	nxp_nfc_debug_format_hex(hex, sizeof(hex), buff, num);
	LOG_INF("%s", hex);
}

void nxp_nfc_debug_print_tag_info(phacDiscLoop_Sw_DataParams_t *p_data_params,
				  uint16_t number_of_tags, uint16_t tags_detected)
{
#if defined(NXPBUILD__PHAC_DISCLOOP_TYPEA_TAGS) || \
    defined(NXPBUILD__PHAC_DISCLOOP_TYPEA_P2P_ACTIVE) || \
    defined(NXPBUILD__PHAC_DISCLOOP_TYPEB_TAGS) || \
    defined(NXPBUILD__PHAC_DISCLOOP_TYPEF_TAGS) || \
    defined(NXPBUILD__PHAC_DISCLOOP_TYPEV_TAGS) || \
    defined(NXPBUILD__PHAC_DISCLOOP_I18000P3M3_TAGS)
	uint8_t b_index;
#endif
#if defined(NXPBUILD__PHAC_DISCLOOP_TYPEA_TAGS) || defined(NXPBUILD__PHAC_DISCLOOP_TYPEA_P2P_ACTIVE)
	uint8_t b_tag_type;
#endif

#if defined(NXPBUILD__PHAC_DISCLOOP_TYPEA_TAGS) || defined(NXPBUILD__PHAC_DISCLOOP_TYPEA_P2P_ACTIVE)
	if (PHAC_DISCLOOP_CHECK_ANDMASK(tags_detected, PHAC_DISCLOOP_POS_BIT_MASK_A)) {
		if (p_data_params->sTypeATargetInfo.bT1TFlag) {
			char uid[32];

			nxp_nfc_debug_format_hex(uid, sizeof(uid),
					     p_data_params->sTypeATargetInfo.aTypeA_I3P3[0].aUid,
					     p_data_params->sTypeATargetInfo.aTypeA_I3P3[0].bUidSize);
			LOG_INF("Technology: Type A, UID: %s, SAK: 0x%02x, Type: Type 1 Tag",
				uid, p_data_params->sTypeATargetInfo.aTypeA_I3P3[0].aSak);
		} else {
			for (b_index = 0; b_index < number_of_tags; b_index++) {
				char uid[32];
				const char *type_str = NULL;

				nxp_nfc_debug_format_hex(uid, sizeof(uid),
					p_data_params->sTypeATargetInfo.aTypeA_I3P3[b_index].aUid,
					p_data_params->sTypeATargetInfo.aTypeA_I3P3[b_index].bUidSize);

				if ((p_data_params->sTypeATargetInfo.aTypeA_I3P3[b_index].aSak &
				     (uint8_t)~0xFB) == 0) {
					b_tag_type = (p_data_params->sTypeATargetInfo.aTypeA_I3P3[b_index].aSak &
						      0x60);
					b_tag_type = b_tag_type >> 5;
					type_str = nxp_nfc_debug_type_a_tag_type_name(b_tag_type);
				}

				if (type_str != NULL) {
					LOG_INF("Technology: Type A, Card: %d, UID: %s, SAK: 0x%02x, Type: %s",
						b_index + 1, uid,
						p_data_params->sTypeATargetInfo.aTypeA_I3P3[b_index].aSak,
						type_str);
				} else {
					LOG_INF("Technology: Type A, Card: %d, UID: %s, SAK: 0x%02x",
						b_index + 1, uid,
						p_data_params->sTypeATargetInfo.aTypeA_I3P3[b_index].aSak);
				}
			}
		}
	}
#endif

#ifdef NXPBUILD__PHAC_DISCLOOP_TYPEB_TAGS
	if (PHAC_DISCLOOP_CHECK_ANDMASK(tags_detected, PHAC_DISCLOOP_POS_BIT_MASK_B)) {
		for (b_index = 0; b_index < number_of_tags; b_index++) {
			char uid[16];

			nxp_nfc_debug_format_hex(uid, sizeof(uid),
					p_data_params->sTypeBTargetInfo.aTypeB_I3P3[b_index].aPupi, 0x04);
			LOG_INF("Technology: Type B, Card: %d, UID: %s", b_index + 1, uid);
		}
	}
#endif /* NXPBUILD__PHAC_DISCLOOP_TYPEB_TAGS */

#ifdef NXPBUILD__PHAC_DISCLOOP_TYPEF_TAGS
	if (PHAC_DISCLOOP_CHECK_ANDMASK(tags_detected, PHAC_DISCLOOP_POS_BIT_MASK_F212) ||
	    PHAC_DISCLOOP_CHECK_ANDMASK(tags_detected, PHAC_DISCLOOP_POS_BIT_MASK_F424)) {
		for (b_index = 0; b_index < number_of_tags; b_index++) {
			char uid[32];
			const char *type_str;
			const char *bitrate_str;

			nxp_nfc_debug_format_hex(uid, sizeof(uid),
					p_data_params->sTypeFTargetInfo.aTypeFTag[b_index].aIDmPMm,
					PHAC_DISCLOOP_FELICA_IDM_LENGTH);

			if ((p_data_params->sTypeFTargetInfo.aTypeFTag[b_index].aIDmPMm[0] == 0x01) &&
			    (p_data_params->sTypeFTargetInfo.aTypeFTag[b_index].aIDmPMm[1] == 0xFE)) {
				type_str = "P2P";
			} else {
				type_str = "Type 3 Tag";
			}

			if (p_data_params->sTypeFTargetInfo.aTypeFTag[b_index].bBaud !=
			    PHAC_DISCLOOP_CON_BITR_212) {
				bitrate_str = "424";
			} else {
				bitrate_str = "212";
			}

			LOG_INF("Technology: Type F, Card: %d, UID: %s, Type: %s, Bit Rate: %s",
				b_index + 1, uid, type_str, bitrate_str);
		}
	}
#endif /* NXPBUILD__PHAC_DISCLOOP_TYPEF_TAGS */

#ifdef NXPBUILD__PHAC_DISCLOOP_TYPEV_TAGS
	if (PHAC_DISCLOOP_CHECK_ANDMASK(tags_detected, PHAC_DISCLOOP_POS_BIT_MASK_V)) {
		for (b_index = 0; b_index < number_of_tags; b_index++) {
			char uid[32];

			nxp_nfc_debug_format_hex(uid, sizeof(uid),
					p_data_params->sTypeVTargetInfo.aTypeV[b_index].aUid, 0x08);
			LOG_INF("Technology: Type V / ISO 15693 / T5T, Card: %d, UID: %s",
				b_index + 1, uid);
		}
	}
#endif /* NXPBUILD__PHAC_DISCLOOP_TYPEV_TAGS */

#ifdef NXPBUILD__PHAC_DISCLOOP_I18000P3M3_TAGS
	if (PHAC_DISCLOOP_CHECK_ANDMASK(tags_detected, PHAC_DISCLOOP_POS_BIT_MASK_18000P3M3)) {
		for (b_index = 0; b_index < number_of_tags; b_index++) {
			char uii[64];
			uint8_t uii_len =
				p_data_params->sI18000p3m3TargetInfo.aI18000p3m3[b_index].wUiiLength / 8;

			nxp_nfc_debug_format_hex(uii, sizeof(uii),
					p_data_params->sI18000p3m3TargetInfo.aI18000p3m3[b_index].aUii,
					uii_len);
			LOG_INF("Technology: ISO 18000p3m3 / EPC Gen2, Card: %d, UII: %s",
				b_index + 1, uii);
		}
	}
#endif /* NXPBUILD__PHAC_DISCLOOP_I18000P3M3_TAGS */
}

void nxp_nfc_debug_print_error_info(phStatus_t status)
{
	const char *comp = nxp_nfc_debug_comp_name(status & 0xFF00);
	const char *err = nxp_nfc_debug_err_name(status & PH_ERR_MASK);

	if (strcmp(comp, "unknown") != 0 && strcmp(err, "unknown") != 0) {
		LOG_ERR("ErrorInfo Comp: %s type: %s (0x%04x)", comp, err, status);
	} else if (strcmp(comp, "unknown") != 0) {
		LOG_ERR("ErrorInfo Comp: %s type: 0x%x (0x%04x)", comp,
			status & PH_ERR_MASK, status);
	} else if (strcmp(err, "unknown") != 0) {
		LOG_ERR("ErrorInfo Comp: 0x%x type: %s (0x%04x)",
			status & PH_COMPID_MASK, err, status);
	} else {
		LOG_ERR("ErrorInfo Comp: 0x%x type: 0x%x (0x%04x)",
			status & PH_COMPID_MASK, status & PH_ERR_MASK, status);
	}
}
