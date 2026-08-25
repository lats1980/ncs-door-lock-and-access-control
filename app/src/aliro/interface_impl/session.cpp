/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "aliro/interface.h"

#include "aliro/access_manager/access_manager.h"
#ifdef CONFIG_NFC_DRIVER_STM
#include "aliro/platform/nfc/stm/nfc_transport_rfal.h"
#elif defined(CONFIG_NFC_DRIVER_NXP)
#include "aliro/platform/nfc/nxp/nfc_transport_NxpNfcRdLib.h"
#else
#error "NFC HAL driver not selected (NFC_DRIVER_STM or NFC_DRIVER_NXP)."
#endif

#ifdef CONFIG_NCS_ALIRO_BLE_UWB
#include "aliro/platform/ble/ble_manager.h"
#endif // CONFIG_NCS_ALIRO_BLE_UWB

namespace Aliro::Interface::Session {

AliroError Send(ConnectionHandle handle, Data data)
{
	if (handle.IsNfc()) {
#ifdef CONFIG_NFC_DRIVER_STM		
		return NfcTransportRfal::Instance().Send(data);
#elif defined(CONFIG_NFC_DRIVER_NXP)
	    return NfcTransportNxpNfcRdLib::Instance().Send(data);
#else
        #error "NFC HAL driver not selected (NFC_DRIVER_STM or NFC_DRIVER_NXP)."
#endif
	}
#ifdef CONFIG_NCS_ALIRO_BLE_UWB
	else if (handle.IsBle()) {
		return BleManager::Instance().Send(handle, data);
	}
#endif // CONFIG_NCS_ALIRO_BLE_UWB

	return ALIRO_INVALID_ARGUMENT;
}

void HandleTermination(ConnectionHandle handle)
{
	if (handle.IsNfc()) {
#ifdef CONFIG_NFC_DRIVER_STM		
		NfcTransportRfal::Instance().Terminate();
#elif defined(CONFIG_NFC_DRIVER_NXP)
	    NfcTransportNxpNfcRdLib::Instance().Terminate();
#else
        #error "NFC HAL driver not selected (NFC_DRIVER_STM or NFC_DRIVER_NXP)."
#endif
	}
#ifdef CONFIG_NCS_ALIRO_BLE_UWB
	else if (handle.IsBle()) {
		BleManager::Instance().Terminate(handle);
	}
#endif // CONFIG_NCS_ALIRO_BLE_UWB

	AccessManagerInstance().HandleSessionTermination(handle);
}

#ifdef CONFIG_NCS_ALIRO_BLE_UWB

AliroError StartRangingSession(ConnectionHandle handle, uint32_t rangingSessionId, const CryptoTypes::Ursk &ursk,
			       ProtocolVersion protocolVersion)
{
	return AccessManagerInstance().StartRangingSession(rangingSessionId, ursk, protocolVersion, handle);
}

#endif // CONFIG_NCS_ALIRO_BLE_UWB

} // namespace Aliro::Interface::Session
