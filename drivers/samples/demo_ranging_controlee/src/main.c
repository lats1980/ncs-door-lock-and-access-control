/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * demo_ranging_controlee: Controlee / Responder, Static STS two-way ranging (DS-TWR, unicast)
 * with the SR150, built on the NXP middleware port in drivers/uwb/nxp.
 * Short MAC addresses: this device 0x2222, peer 0x1111.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* NXP UWB IoT middleware, see drivers/uwb/nxp */
#include "UwbApi.h"
#include "UwbApi_Types.h"
#include "PrintUtility.h"
#include "uwb_types.h"
#include "uci_defs.h"
#include "nxAntennaDefine.h"

LOG_MODULE_REGISTER(demo_ranging_controlee, CONFIG_UWB_NXP_LOG_LEVEL);

#define SESSION_ID 0x11223344

static const uint8_t device_mac_addr[2] = {0x22, 0x22};
static const uint8_t dst_mac_addr[2]    = {0x11, 0x11};

static void ranging_callback(eNotificationType type, void *data)
{
	if (type != UWBD_RANGING_DATA) {
		return;
	}

	phRangingData_t *r = (phRangingData_t *)data;

	LOG_INF("RngData: seq=%u session=0x%08X int=%ums meas=%u", r->seq_ctr, r->sessionHandle,
		r->curr_range_interval, r->no_of_measurements);
	for (uint32_t i = 0; i < r->no_of_measurements && i < MAX_NUM_RESPONDERS; i++) {
		phRangingMesr_t *m = &r->ranging_meas.range_meas_twr[i];

		LOG_INF("  [%u] mac=%02X%02X status=0x%02X nLos=%u dist=%ucm "
			"az=%d.%03ddeg azFOM=%u el=%d.%03ddeg elFOM=%u slot=%u rssi=%d.%d",
			i, m->mac_addr[0], m->mac_addr[1], m->status, m->nLos, m->distance,
			TO_Q_9_7(m->aoa_azimuth), m->aoa_azimuth_FOM, TO_Q_9_7(m->aoa_elevation),
			m->aoa_elevation_FOM, m->slot_index, TO_Q_7_1(m->rssi));
	}
}

int main(void)
{
	tUWBAPI_STATUS status;
	phUwbDevInfo_t dev_info;
	uint32_t session_handle;

	LOG_INF("demo_ranging_controlee starting");

	/* The SR150 firmware image comes from the NXP package */
	status = UwbApi_Init(ranging_callback);
	if (status != UWBAPI_STATUS_OK) {
		LOG_ERR("UwbApi_Init failed: %d", status);
		return -1;
	}

	/* RF_CLK_ACCURACY_CALIB: number of registers (0x03) + CAP1, CAP2, GM CURRENT CONTROL (2 B LE each) */
	uint8_t calib_value[] = {
		0x03,
		AD_U16_TO_AU8_LE(15),
		AD_U16_TO_AU8_LE(15),
		AD_U16_TO_AU8_LE(0x21),
	};

	status = UwbApi_SetCalibration(CH_9, RF_CLK_ACCURACY_CALIB, calib_value, sizeof(calib_value));
	if (status != UWBAPI_STATUS_OK) {
		LOG_ERR("UwbApi_SetCalibration(RF_CLK_ACCURACY_CALIB) failed: %d", status);
	}

	status = UwbApi_GetDeviceInfo(&dev_info);
	if (status != UWBAPI_STATUS_OK) {
		LOG_ERR("UwbApi_GetDeviceInfo failed: %d", status);
		goto exit;
	}
	LOG_INF("UWB device: FW %u.%u.%u, MW %u.%u.%u", dev_info.fwMajor, dev_info.fwMinor,
		dev_info.fwRc, dev_info.mwMajor, dev_info.mwMinor, dev_info.mwRc);

	status = UwbApi_SessionInit(SESSION_ID, UWBD_RANGING_SESSION, &session_handle);
	if (status != UWBAPI_STATUS_OK) {
		LOG_ERR("UwbApi_SessionInit failed: %d", status);
		goto exit;
	}
	LOG_INF("Ranging session created, handle=0x%08X", session_handle);

	/* Application configuration: number of controlees and their address */
	const UWB_AppParams_List_t app_params[] = {
		UWB_SET_APP_PARAM_VALUE(UCI_PARAM_ID_NO_OF_CONTROLEES, 1),
		UWB_SET_APP_PARAM_ARRAY(UCI_PARAM_ID_DST_MAC_ADDRESS, dst_mac_addr, sizeof(dst_mac_addr)),
	};

	status = UwbApi_SetAppConfigMultipleParams(session_handle, ARRAY_SIZE(app_params), app_params);
	if (status != UWBAPI_STATUS_OK) {
		LOG_ERR("SetAppConfigMultipleParams failed: %d", status);
		goto deinit;
	}

	/* Ranging parameters */
	phRangingParams_t ranging_params = {0};

	ranging_params.deviceRole        = kUWB_DeviceRole_Responder;
	ranging_params.deviceType        = kUWB_DeviceType_Controlee;
	ranging_params.multiNodeMode     = kUWB_MultiNodeMode_UniCast;
	ranging_params.macAddrMode       = 0; /* short address */
	ranging_params.scheduledMode     = kUWB_ScheduledMode_TimeScheduled;
	ranging_params.rangingRoundUsage = kUWB_RangingRoundUsage_DS_TWR;
	ranging_params.deviceMacAddr[0]  = device_mac_addr[0];
	ranging_params.deviceMacAddr[1]  = device_mac_addr[1];

	status = UwbApi_SetRangingParams(session_handle, &ranging_params);
	if (status != UWBAPI_STATUS_OK) {
		LOG_ERR("SetRangingParams failed: %d", status);
		goto deinit;
	}

	/* Common application configuration (as demo_set_common_app_config() of the NXP demos) */
	const UWB_AppParams_List_t common_params[] = {
		UWB_SET_APP_PARAM_VALUE(UCI_PARAM_ID_SFD_ID, 2),
		UWB_SET_APP_PARAM_VALUE(UCI_PARAM_ID_SLOTS_PER_RR, 25),
		UWB_SET_APP_PARAM_VALUE(UCI_PARAM_ID_RANGING_DURATION, 200),
		UWB_SET_APP_PARAM_VALUE(UCI_PARAM_ID_STS_CONFIG, kUWB_StsConfig_StaticSts),
		UWB_SET_APP_PARAM_VALUE(UCI_PARAM_ID_NUMBER_OF_STS_SEGMENTS, 1),
		UWB_SET_APP_PARAM_VALUE(UCI_PARAM_ID_RFRAME_CONFIG, 3),
		UWB_SET_APP_PARAM_VALUE(UCI_PARAM_ID_CHANNEL_NUMBER, CH_9),
	};

	status = UwbApi_SetAppConfigMultipleParams(session_handle, ARRAY_SIZE(common_params),
						   common_params);
	if (status != UWBAPI_STATUS_OK) {
		LOG_ERR("SetCommonAppConfig failed: %d", status);
		goto deinit;
	}

	/* Vendor antenna configuration: AoA mode, one antenna pair */
	uint8_t antennae_configuration_rx[] = {kUWBAntCfgRxMode_AoA_Mode, 0x01, 1};
	const UWB_VendorAppParams_List_t vendor_params[] = {
		UWB_SET_VENDOR_APP_PARAM_ARRAY(ANTENNAE_CONFIGURATION_RX, antennae_configuration_rx,
					       sizeof(antennae_configuration_rx)),
	};

	status = UwbApi_SetVendorAppConfigs(session_handle, ARRAY_SIZE(vendor_params), vendor_params);
	if (status != UWBAPI_STATUS_OK) {
		LOG_ERR("SetVendorAppConfigs failed: %d", status);
		goto deinit;
	}

	status = UwbApi_StartRangingSession(session_handle);
	if (status != UWBAPI_STATUS_OK) {
		LOG_ERR("StartRangingSession failed: %d", status);
		goto deinit;
	}
	LOG_INF("Ranging started (session ACTIVE)");

	/* Ranging results are reported by ranging_callback() */
	return 0;

deinit:
	UwbApi_SessionDeinit(session_handle);
exit:
	UwbApi_ShutDown();
	return -1;
}
