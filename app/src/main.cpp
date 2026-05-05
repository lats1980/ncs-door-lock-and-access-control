/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#if defined(CONFIG_CHIP) && !defined(CONFIG_ALIRO_AT_MODULE)
#include "matter/init.h"
#else // CONFIG_CHIP && !CONFIG_ALIRO_AT_MODULE
#include "aliro/init.h"
#include "aliro/lock_sim/lock_sim_instance.h"
#endif // CONFIG_CHIP

#include "aliro/utils.h"

#if !defined(CONFIG_ALIRO_AT_HOST)
#include <crypto/utils.h>
#endif
#include <zephyr/logging/log.h>

#include <cstdlib>

#ifdef CONFIG_DOOR_LOCK_DFU_BLE_SMP
#include "dfu_smp_manager.h"
#endif // CONFIG_DOOR_LOCK_DFU_BLE_SMP

#ifdef CONFIG_DOOR_LOCK_BLE_NUS

#include "bt_nus/bt_nus.h"
#endif // CONFIG_DOOR_LOCK_BLE_NUS

#ifdef CONFIG_DOOR_LOCK_BLE_UWB
#include "access_manager.h"
#include "uwb_impl.h"
#endif // CONFIG_DOOR_LOCK_BLE_UWB

#ifdef CONFIG_ALIRO_AT_MODULE
#include "at_command/at_module.h"
#endif // CONFIG_ALIRO_AT_MODULE

#ifdef CONFIG_CHIP
LOG_MODULE_REGISTER(app, CONFIG_CHIP_APP_LOG_LEVEL);
#else // CONFIG_CHIP
LOG_MODULE_REGISTER(door_lock_app, CONFIG_DOOR_LOCK_APP_LOG_LEVEL);
#endif // CONFIG_CHIP

#if defined(CONFIG_ALIRO_AT_MODULE)
static void s_module_event_cb(enum at_module_event event)
{
	if (event == AT_MODULE_TRANSPORT_CONNECTED) {
		LOG_INF("AT command transport connected");
	} else {
		LOG_INF("AT command transport disconnected");
	}
}
#endif // CONFIG_ALIRO_AT_MODULE

int main()
{
#if !defined(CONFIG_ALIRO_AT_HOST)
	auto error = DoorLock::Crypto::Init();
	VerifyOrDie(error == ALIRO_NO_ERROR, "Failed to initialize Aliro crypto");
#endif

#ifdef CONFIG_DOOR_LOCK_BLE_UWB

	constexpr Aliro::Uwb::UltraWideBandImpl::Callbacks uwbCallbacks{
		.mRangingData =
			[](Aliro::Uwb::UltraWideBandImpl::SessionContextHandle sessionContext,
			   const Aliro::UwbRangingData &uwbData) {
				Aliro::AccessManagerInstance().HandleRangingSessionData(sessionContext, uwbData);
			},
		.mRangingSessionStateChanged =
			[](Aliro::Uwb::UltraWideBandImpl::SessionContextHandle sessionContext,
			   Aliro::RangingSessionState state) {
				Aliro::AccessManagerInstance().HandleRangingSessionStateChanged(sessionContext, state);
			}
	};

	error = Aliro::Uwb::UltraWideBandImpl::Instance().Init(uwbCallbacks);
	if (error == ALIRO_ERROR_NOT_IMPLEMENTED) {
		LOG_INF("UWB is not implemented");
	} else if (error != ALIRO_NO_ERROR) {
		LOG_ERR("Failed to initialize UWB module: %d", error.ToInt());
	}

#endif // CONFIG_DOOR_LOCK_BLE_UWB

#if defined(CONFIG_CHIP) && !defined(CONFIG_ALIRO_AT_MODULE)

	int err = StartMatter();
	VerifyOrDie(err == EXIT_SUCCESS, "Failed to start Matter");

#else // CONFIG_CHIP && !CONFIG_ALIRO_AT_MODULE

	int err = AliroInit();
	VerifyOrDie(err == EXIT_SUCCESS, "Failed to initialize Aliro");

#ifdef CONFIG_ALIRO_AT_MODULE
	err = at_module_init(s_module_event_cb);
	VerifyOrDie(err == 0, "Failed to init AT Module");
#endif // CONFIG_ALIRO_AT_MODULE

#ifdef CONFIG_DOOR_LOCK_DFU_BLE_SMP

	Aliro::Dfu::SmpManager::Instance().Init();
	Aliro::Dfu::SmpManager::Instance().ConfirmNewImage();

#endif // CONFIG_DOOR_LOCK_DFU_BLE_SMP

#ifdef CONFIG_DOOR_LOCK_BLE_NUS
#ifdef CONFIG_ALIRO_AT_MODULE
	Aliro::BtNus::NUSService::Instance().RegisterCommand(
		"AT", strlen("AT"),
		[](void *context) {
			LOG_INF("AT command received over NUS");
		},
		nullptr);
#else
	Aliro::BtNus::NUSService::Instance().RegisterCommand(
		"Unlock", strlen("Unlock"),
		[](void *context) {
			LOG_INF("Unlock command received");
			Aliro::LockSimInstance().Unlock(Aliro::OperationSource::Unspecified);
		},
		nullptr);

	Aliro::BtNus::NUSService::Instance().RegisterCommand(
		"Lock", strlen("Lock"),
		[](void *context) {
			LOG_INF("Lock command received");
			Aliro::LockSimInstance().Lock(Aliro::OperationSource::Unspecified);
		},
		nullptr);
#endif // CONFIG_ALIRO_AT_MODULE
	AliroError nusErr = Aliro::BtNus::NUSService::Instance().Start();
	VerifyOrDie(nusErr == ALIRO_NO_ERROR, "Failed to start NUS service");
#endif // CONFIG_DOOR_LOCK_BLE_NUS

	LOG_INF("Application started");

#endif // CONFIG_CHIP

	return EXIT_SUCCESS;
}
