/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "bolt_lock_manager.h"
#include "app/task_executor.h"

#if !defined(CONFIG_ALIRO_AT_HOST)
#include "aliro/access_manager/access_manager.h"
#include "aliro/aliro.h"

#ifdef CONFIG_DOOR_LOCK_STEP_UP_PHASE
#include "validity_iterations.h"
#endif // CONFIG_DOOR_LOCK_STEP_UP_PHASE
#endif // CONFIG_ALIRO_AT_HOST

#include "app_task.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace chip;

BoltLockManager BoltLockManager::sLock;

#ifdef CONFIG_ALIRO_AT_HOST
#define DL_LOCKSTATE_URC    "+LOCKSTATE: "
static void lockstate_handler(const uint8_t *data, size_t datalen);
DL_AT_RSP(lockstate, DL_LOCKSTATE_URC, lockstate_handler);
static void lockstate_handler(const uint8_t *data, size_t datalen)
{
	LOG_INF("Received AT response: %.*s", datalen, data);
	if (datalen < strlen(DL_LOCKSTATE_URC) + 1) {
		LOG_ERR("Invalid LOCKSTATE response length: %d", datalen);
		return;
	}

	int lockState = atoi((const char *)data + strlen(DL_LOCKSTATE_URC));
	LOG_INF("Received LOCKSTATE response: %d", lockState);
	Nrf::PostTask([lockState] { BoltLockMgr().UpdateState((Aliro::ReaderStateByte)lockState); });
}
#endif // CONFIG_ALIRO_AT_HOST

namespace {

[[maybe_unused]] Aliro::OperationSource ToAliroOperationSource(BoltLockManager::OperationSource operationSource)
{
	switch (operationSource) {
	case BoltLockManager::OperationSource::kManual:
	case BoltLockManager::OperationSource::kKeypad:
	case BoltLockManager::OperationSource::kButton:
	case BoltLockManager::OperationSource::kRfid:
	case BoltLockManager::OperationSource::kBiometric:
		return Aliro::OperationSource::Manual;
	case BoltLockManager::OperationSource::kRemote:
		return Aliro::OperationSource::Matter;
	case BoltLockManager::OperationSource::kAuto:
		return Aliro::OperationSource::Auto;
	case BoltLockManager::OperationSource::kSchedule:
		return Aliro::OperationSource::Schedule;
	default:
		return Aliro::OperationSource::Unspecified;
	}
}

} // namespace

void BoltLockManager::Init(StateChangeCallback callback)
{
	mStateChangeCallback = callback;
#if defined(CONFIG_ALIRO_AT_HOST)
#else
	mLockSim.Init([](Aliro::OperationSource, Aliro::ReaderStateByte state) {
		LOG_INF("LockSim state changed: %" PRIu8, (uint8_t)state);
		Nrf::PostTask([state] { BoltLockMgr().UpdateState(state); });
	});

	// Set Aliro AccessManager application callbacks
	Aliro::AccessManagerInstance().SetApplicationCallbacks({
		.mUnlockIndicatorClb =
			[](Aliro::OperationSource source) {
				Nrf::PostTask([source] {
					LOG_INF("Unlock callback called with source: %" PRIu8, (uint8_t)source);
					if (!BoltLockMgr().Unlock(source)) {
#ifdef CONFIG_DOOR_LOCK_BLE_UWB
						// The lock is already unlocked, so we can send the Unsecured state
						Aliro::AliroStack::Instance().SendReaderStatusChangedMessage(
							source, Aliro::ReaderStateByte::Unsecured);
#endif // CONFIG_DOOR_LOCK_BLE_UWB
					}
				});
			},
		.mLockIndicatorClb =
			[](Aliro::OperationSource source) {
					LOG_INF("Lock callback called with source: %" PRIu8, (uint8_t)source);
					Nrf::PostTask([source] { BoltLockMgr().Lock(source);
				});
			},
	});
#endif
	auto addPublicKey = [](uint16_t credentialIndex, CredentialTypeEnum credentialType,
			       chip::ByteSpan credentialData) {
					LOG_INF("addPublicKey callback: index=%d, type=%d, size=%u", credentialIndex, static_cast<uint8_t>(credentialType), (unsigned)credentialData.size());
#if defined(CONFIG_ALIRO_AT_HOST)
		uint8_t type = 0;
		if (credentialType == CredentialTypeEnum::kAliroNonEvictableEndpointKey) {
			type = 0; /* AccessCredential */
		} else if (credentialType == CredentialTypeEnum::kAliroCredentialIssuerKey) {
			type = 1; /* CredentialIssuer */
		} else if (credentialType == CredentialTypeEnum::kAliroEvictableEndpointKey) {
			type = 2; /* AccessDocument */
		} else {
			LOG_ERR("addPublicKey: unsupported credential type %u", static_cast<uint8_t>(credentialType));
			return;
		}
		if (!at_amcmgr_add_public_key(credentialIndex - 1, type, credentialData.data())) {
			LOG_ERR("addPublicKey: AT+AMCMGR=1 failed");
		}
#else
		Aliro::CryptoTypes::PublicKey publicKey{};
		std::copy_n(credentialData.data(), publicKey.size(), publicKey.data());

		// The credential index is 1-based, so we need to subtract 1 to get the 0-based index
		size_t keyIndex = credentialIndex - 1;

		if (credentialType == CredentialTypeEnum::kAliroNonEvictableEndpointKey) {
			Aliro::AccessManagerInstance().AddPublicKey(
				publicKey, Aliro::AccessManager::PublicKeyType::AccessCredential, keyIndex);
		} else if (credentialType == CredentialTypeEnum::kAliroEvictableEndpointKey) {
			Aliro::AccessManagerInstance().AddPublicKey(
				publicKey, Aliro::AccessManager::PublicKeyType::AccessDocument, keyIndex);
		} else if (credentialType == CredentialTypeEnum::kAliroCredentialIssuerKey) {
			Aliro::AccessManagerInstance().AddPublicKey(
				publicKey, Aliro::AccessManager::PublicKeyType::CredentialIssuer, keyIndex);
		}
#endif
	};

	auto removePublicKey = []([[maybe_unused]] uint16_t credentialIndex, CredentialTypeEnum credentialType,
				  chip::ByteSpan credentialData) {
					LOG_INF("removePublicKey callback called with credentialIndex=%u, credentialType=%u", credentialIndex,
						 (uint8_t)credentialType);
#if defined(CONFIG_ALIRO_AT_HOST)
		uint8_t type = 0;
		if (credentialType == CredentialTypeEnum::kAliroNonEvictableEndpointKey) {
			type = 0; /* AccessCredential */
		} else if (credentialType == CredentialTypeEnum::kAliroCredentialIssuerKey) {
			type = 1; /* CredentialIssuer */
		} else if (credentialType == CredentialTypeEnum::kAliroEvictableEndpointKey) {
			type = 2; /* AccessDocument */
		} else {
			LOG_ERR("removePublicKey: unsupported credential type %u", static_cast<uint8_t>(credentialType));
			return;
		}
		if (!at_amcmgr_remove_public_key(credentialIndex, type)) {
			LOG_ERR("removePublicKey: AT+AMCMGR=0 failed");
		}
#else
		Aliro::CryptoTypes::PublicKey publicKey{};
		std::copy_n(credentialData.data(), publicKey.size(), publicKey.data());

		// The credential index is 1-based, so we need to subtract 1 to get the 0-based index
		size_t keyIndex = credentialIndex - 1;

		if (credentialType == CredentialTypeEnum::kAliroNonEvictableEndpointKey) {
			Aliro::AccessManagerInstance().RemovePublicKey(
				Aliro::AccessManager::PublicKeyType::AccessCredential, keyIndex);
		} else if (credentialType == CredentialTypeEnum::kAliroEvictableEndpointKey) {
			Aliro::AccessManagerInstance().RemovePublicKey(
				Aliro::AccessManager::PublicKeyType::AccessDocument, keyIndex);
		} else if (credentialType == CredentialTypeEnum::kAliroCredentialIssuerKey) {
			Aliro::AccessManagerInstance().RemovePublicKey(
				Aliro::AccessManager::PublicKeyType::CredentialIssuer, keyIndex);

#ifdef CONFIG_DOOR_LOCK_STEP_UP_PHASE
LOG_INF("Clearing validity iterations for removed key with index %u and type %u", credentialIndex, (uint8_t)credentialType);
			Aliro::ClearValidityIterations(credentialIndex);
#endif // CONFIG_DOOR_LOCK_STEP_UP_PHASE
		}
#endif
	};

	AccessMgr::Instance().Init(addPublicKey, removePublicKey, nullptr, addPublicKey);

	/* Set the default state */
	Nrf::GetBoard().GetLED(Nrf::DeviceLeds::LED2).Set(IsLocked());
}

bool BoltLockManager::GetUser(uint16_t userIndex, EmberAfPluginDoorLockUserInfo &user)
{
	return AccessMgr::Instance().GetUserInfo(userIndex, user);
}

bool BoltLockManager::SetUser(uint16_t userIndex, FabricIndex creator, FabricIndex modifier, const CharSpan &userName,
			      uint32_t uniqueId, UserStatusEnum userStatus, UserTypeEnum userType,
			      CredentialRuleEnum credentialRule, const CredentialStruct *credentials,
			      size_t totalCredentials)
{
	return AccessMgr::Instance().SetUser(userIndex, creator, modifier, userName, uniqueId, userStatus, userType,
					     credentialRule, credentials, totalCredentials);
}

bool BoltLockManager::GetCredential(uint16_t credentialIndex, CredentialTypeEnum credentialType,
				    EmberAfPluginDoorLockCredentialInfo &credential)
{
	return AccessMgr::Instance().GetCredential(credentialIndex, credentialType, credential);
}

bool BoltLockManager::SetCredential(uint16_t credentialIndex, FabricIndex creator, FabricIndex modifier,
				    DlCredentialStatus credentialStatus, CredentialTypeEnum credentialType,
				    const ByteSpan &secret)
{
	return AccessMgr::Instance().SetCredential(credentialIndex, creator, modifier, credentialStatus, credentialType,
						   secret);
}

#ifdef CONFIG_LOCK_SCHEDULES

DlStatus BoltLockManager::GetWeekDaySchedule(uint8_t weekdayIndex, uint16_t userIndex,
					     EmberAfPluginDoorLockWeekDaySchedule &schedule)
{
	return AccessMgr::Instance().GetWeekDaySchedule(weekdayIndex, userIndex, schedule);
}

DlStatus BoltLockManager::SetWeekDaySchedule(uint8_t weekdayIndex, uint16_t userIndex, DlScheduleStatus status,
					     DaysMaskMap daysMask, uint8_t startHour, uint8_t startMinute,
					     uint8_t endHour, uint8_t endMinute)
{
	return AccessMgr::Instance().SetWeekDaySchedule(weekdayIndex, userIndex, status, daysMask, startHour,
							startMinute, endHour, endMinute);
}

DlStatus BoltLockManager::GetYearDaySchedule(uint8_t yearDayIndex, uint16_t userIndex,
					     EmberAfPluginDoorLockYearDaySchedule &schedule)
{
	return AccessMgr::Instance().GetYearDaySchedule(yearDayIndex, userIndex, schedule);
}

DlStatus BoltLockManager::SetYearDaySchedule(uint8_t yeardayIndex, uint16_t userIndex, DlScheduleStatus status,
					     uint32_t localStartTime, uint32_t localEndTime)
{
	return AccessMgr::Instance().SetYearDaySchedule(yeardayIndex, userIndex, status, localStartTime, localEndTime);
}

DlStatus BoltLockManager::GetHolidaySchedule(uint8_t holidayIndex, EmberAfPluginDoorLockHolidaySchedule &schedule)
{
	return AccessMgr::Instance().GetHolidaySchedule(holidayIndex, schedule);
}

DlStatus BoltLockManager::SetHolidaySchedule(uint8_t holidayIndex, DlScheduleStatus status, uint32_t localStartTime,
					     uint32_t localEndTime, OperatingModeEnum operatingMode)
{
	return AccessMgr::Instance().SetHolidaySchedule(holidayIndex, status, localStartTime, localEndTime,
							operatingMode);
}

#endif /* CONFIG_LOCK_SCHEDULES */

bool BoltLockManager::ValidatePIN(const Optional<chip::ByteSpan> &pinCode, OperationErrorEnum &err,
				  Nullable<ValidatePINResult> &result)
{
	return AccessMgr::Instance().ValidatePIN(pinCode, err, result);
}

void BoltLockManager::SetRequirePIN(bool require)
{
	return AccessMgr::Instance().SetRequirePIN(require);
}
bool BoltLockManager::GetRequirePIN()
{
	return AccessMgr::Instance().GetRequirePIN();
}

void BoltLockManager::Lock(const OperationSource source, const Nullable<chip::FabricIndex> &fabricIdx,
			   const Nullable<chip::NodeId> &nodeId, const Nullable<ValidatePINResult> &validatePINResult)
{
	VerifyOrReturn(mStateData.mState != Aliro::ReaderStateByte::Secured);
	mStateData = { Aliro::ReaderStateByte::EnteringSecured,
		       source,
		       ToAliroOperationSource(source),
		       fabricIdx,
		       nodeId,
		       validatePINResult };
#if defined(CONFIG_ALIRO_AT_HOST)
	at_aliro_lock(ToAliroOperationSource(source));
#else
	mLockSim.Lock(ToAliroOperationSource(source));
#endif
}

void BoltLockManager::Unlock(const OperationSource source, const Nullable<chip::FabricIndex> &fabricIdx,
			     const Nullable<chip::NodeId> &nodeId, const Nullable<ValidatePINResult> &validatePINResult)
{
	VerifyOrReturn(mStateData.mState != Aliro::ReaderStateByte::Unsecured);
	mStateData = { Aliro::ReaderStateByte::EnteringUnsecured,
		       source,
		       ToAliroOperationSource(source),
		       fabricIdx,
		       nodeId,
		       validatePINResult };

#if defined(CONFIG_ALIRO_AT_HOST)
	at_aliro_unlock(ToAliroOperationSource(source));
#else
	mLockSim.Unlock(ToAliroOperationSource(source));
#endif
}

bool BoltLockManager::Lock(Aliro::OperationSource source)
{
	VerifyOrReturnValue(mStateData.mState != Aliro::ReaderStateByte::Secured, false);
	mStateData = { Aliro::ReaderStateByte::EnteringSecured,
		       OperationSource::kAliro,
		       source,
		       NullNullable,
		       NullNullable,
		       NullNullable };

#if defined(CONFIG_ALIRO_AT_HOST)
	at_aliro_lock(source);
#else
	mLockSim.Lock(source);
#endif
	return true;
}

bool BoltLockManager::Unlock(Aliro::OperationSource source)
{
	VerifyOrReturnValue(mStateData.mState != Aliro::ReaderStateByte::Unsecured, false);
	mStateData = { Aliro::ReaderStateByte::EnteringUnsecured,
		       OperationSource::kAliro,
		       source,
		       NullNullable,
		       NullNullable,
		       NullNullable };

#if defined(CONFIG_ALIRO_AT_HOST)
	at_aliro_unlock(source);
#else
	mLockSim.Unlock(source);
#endif
	return true;
}

void BoltLockManager::UpdateState(Aliro::ReaderStateByte state)
{
	mStateData.mState = state;

	if (mStateChangeCallback != nullptr) {
		mStateChangeCallback(mStateData);
	}
}

void BoltLockManager::FactoryReset()
{
	AccessMgr::Instance().FactoryReset();
}
