/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "nfc_transport_NxpNfcRdLib.h"
#include "nxp_nfc_discovery/nxp_nfc_platform.h"

#include "aliro/aliro.h"
#include "aliro/utils.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(nfc_st_NxpNfcRdLib_impl, CONFIG_DOOR_LOCK_NXPNFCRDLIB_LOG_LEVEL);

namespace Aliro {
/*
******************************************************************************
* Public API
******************************************************************************
*/

AliroError NfcTransportNxpNfcRdLib::Init()
{
    int err;

	err = nxp_nfc_init();
    VerifyOrReturnStatus(err == 0, ALIRO_ERROR_INTERNAL, LOG_ERR("NxpNfcRdLib: nxp_nfc_init failed %d", err));

	err = nxp_nfc_lib_init();
	VerifyOrReturnStatus(err == 0, ALIRO_ERROR_INTERNAL, LOG_ERR("NxpNfcRdLib: nxp_nfc_lib_init failed %d", err));
	
	return ALIRO_NO_ERROR;
}

AliroError NfcTransportNxpNfcRdLib::Start()
{
	int err;

	if (atomic_get(&mStarted)) {
		return ALIRO_NO_ERROR;
	}

	err = nxp_nfc_discovery_start();
	VerifyOrReturnStatus(err == 0, ALIRO_ERROR_INTERNAL, LOG_ERR("NxpNfcRdLib: nxp_nfc_discovery_start failed %d", err));

	atomic_set(&mStarted, true);

	return ALIRO_NO_ERROR;
}

AliroError NfcTransportNxpNfcRdLib::Stop()
{
	atomic_clear(&mStarted);

	return ALIRO_NO_ERROR;	
}

AliroError NfcTransportNxpNfcRdLib::Send(Data data)
{
	return ALIRO_NO_ERROR;
}

AliroError NfcTransportNxpNfcRdLib::Terminate()
{
	return ALIRO_NO_ERROR;
}

} // namespace Aliro
