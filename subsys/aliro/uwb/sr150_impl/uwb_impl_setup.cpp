/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file
 * @brief Handling of the Aliro UWB Ranging Service setup messages (M1..M4) and notifications.
 *
 * The message layer (aliro_uwb_message.cpp) only encodes and decodes messages; the decisions based on the
 * UWBS capabilities and the BLE transmission are done here.
 */

#include "uwb_impl.h"

#include "aliro_uwb_message.h"
#include "session_management.h"

#include <doorlock/utils/utils.h>

#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(UwbImpl);

namespace Aliro::Uwb {

namespace {

/** Lowest RAN multiplier the reader accepts. */
constexpr uint8_t kMinRanMultiplier{ 2 };
/** MAC mode: b0-b5 offset between ranging blocks, b6-b7 number of ranging rounds (0: one round). */
constexpr uint8_t kMacMode{ 0 };

/** Number_Chaps_per_Slot values selectable through the slot bitmask, indexed by bit. */
constexpr uint8_t kChapsPerSlot[] = { 3, 4, 6, 8, 9, 12, 24 };

/**
 * Hopping configurations in order of preference. Bitmasks are in the Aliro wire format
 * (b7: none, b6: continuous, b5: adaptive, b4: default, b3: AES); a configuration needs all its bits.
 */
constexpr uint8_t kHoppingNone = BIT(7);
constexpr uint8_t kHoppingContinuousDefault = BIT(6) | BIT(4);
constexpr uint8_t kPreferredHoppingConfigs[] = { kHoppingNone, kHoppingContinuousDefault };

/** SR150 HOPPING_MODE values. */
constexpr uint32_t kHoppingModeNone = 0x00;
constexpr uint32_t kHoppingModeContinuousModulo = 0x03;

/** Aliro channel bitmask (M2) values. */
constexpr uint8_t kChannelBitmask5 = BIT(0);
constexpr uint8_t kChannelBitmask9 = BIT(1);

/** Time of a chap in RSTU; a slot lasts Number_Chaps_per_Slot chaps. */
constexpr uint32_t kChapDurationRstu = 400;
/** A RAN multiplier unit is 96 ms. */
constexpr uint32_t kRanMultiplierUnitMs = 96;

/** Wrapped RDS: Session handle (4) || Random (12, first 4 bytes 0xB5 = plain text key) || Session key (32). */
constexpr size_t kSessionHandleLength = 4;
constexpr size_t kWrappedRdsRandomLength = 12;
constexpr size_t kWrappedRdsLength = kSessionHandleLength + kWrappedRdsRandomLength + sizeof(CryptoTypes::Ursk);

/*
 * Antenna configuration: AoA mode, number of antenna pairs, antenna pair ID.
 */
constexpr uint8_t kAntennaeConfigurationRx[] = { kUWBAntCfgRxMode_AoA_Mode, 0x02, 1, 2};

/* Short MAC addresses, as used in the NXP demos. */
uint8_t gDeviceMacAddr[] = { 0x22, 0x22 };
uint8_t gDstMacAddr[] = { 0x11, 0x11 };

UWB_AppParams_List_t U32Param(eAppConfig id, uint32_t value)
{
	UWB_AppParams_List_t param{};

	param.param_id = id;
	param.param_type = kUWB_APPPARAMS_Type_u32;
	param.param_value.vu32 = value;
	return param;
}

UWB_AppParams_List_t ArrayParam(eAppConfig id, uint8_t *value, uint16_t length)
{
	UWB_AppParams_List_t param{};

	param.param_id = id;
	param.param_type = kUWB_APPPARAMS_Type_au8;
	param.param_value.au8.param_value = value;
	param.param_value.au8.param_len = length;
	return param;
}

UWB_VendorAppParams_List_t VendorArrayParam(eVendorAppConfig id, const uint8_t *value, uint16_t length)
{
	UWB_VendorAppParams_List_t param{};

	param.param_id = id;
	param.param_type = kUWB_APPPARAMS_Type_au8;
	param.param_value.au8.param_value = const_cast<uint8_t *>(value);
	param.param_value.au8.param_len = length;
	return param;
}

/** Sets the session application configs negotiated over BLE (M2, M3, M4). */
tUWBAPI_STATUS ApplySessionAppConfigs(uint32_t handle, const AliroSessionConfig &config,
				      ProtocolVersion protocolVersion, uint32_t channel, uint32_t hoppingMode)
{
	/*
	 * The SR150 takes the hop mode key as a byte array holding the little-endian form of the 4-byte
	 * integer, while M4 carries it big-endian.
	 */
	uint8_t hopModeKey[sizeof(config.mHopModeKey)];

	for (size_t i = 0; i < sizeof(hopModeKey); i++) {
		hopModeKey[i] = config.mHopModeKey[sizeof(hopModeKey) - 1 - i];
	}

	/* All mandatory app configs must be set before UwbApi_SetRangingParams() for CCC sessions. */
	const UWB_AppParams_List_t appParams[] = {
		U32Param(STS_CONFIG, kUWB_StsConfig_DynamicSts),
		U32Param(CHANNEL_NUMBER, channel),
		U32Param(SLOT_DURATION, config.mSelectedChapsPerSlot * kChapDurationRstu),
		U32Param(RANGING_DURATION, config.mSelectedRanMultiplier * kRanMultiplierUnitMs),
		U32Param(STS_INDEX, config.mStsIndex),
		U32Param(AOA_RESULT_REQ, 0x01),
		U32Param(PREAMBLE_CODE_INDEX, config.mSyncCodeIndex),
		U32Param(SLOTS_PER_RR, config.mSelectedSlotsPerRound),
		U32Param(RESPONDER_SLOT_INDEX, 0x00),
		U32Param(MAX_NUMBER_OF_MEASUREMENTS, 0xFFFF),
		U32Param(HOPPING_MODE, hoppingMode),
		ArrayParam(HOP_MODE_KEY, hopModeKey, sizeof(hopModeKey)),
		U32Param(CCC_CONFIG_QUIRKS, 0x01),
		U32Param(RANGING_PROTOCOL_VER, protocolVersion),
		U32Param(UWB_CONFIG_ID, config.mUwbConfigId),
		U32Param(PULSESHAPE_COMBO, config.mPulseShapeCombo),
		U32Param(NO_OF_CONTROLEES, 1),
		ArrayParam(DST_MAC_ADDRESS, gDstMacAddr, sizeof(gDstMacAddr)),
	};

	return UwbApi_SetAppConfigMultipleParams(handle, ARRAY_SIZE(appParams), appParams);
}

/** Sets the antenna configuration of the session. */
tUWBAPI_STATUS ApplyAntennaConfig(uint32_t handle)
{
	const UWB_VendorAppParams_List_t vendorParams[] = {
		VendorArrayParam(ANTENNAE_CONFIGURATION_RX, kAntennaeConfigurationRx,
				 sizeof(kAntennaeConfigurationRx)),
	};

	return UwbApi_SetVendorAppConfigs(handle, ARRAY_SIZE(vendorParams), vendorParams);
}

/** Sets the URSK as the plain text session key of a wrapped RDS. */
tUWBAPI_STATUS ApplyUrsk(uint32_t handle, const CryptoTypes::Ursk &ursk)
{
	uint8_t wrappedRds[kWrappedRdsLength];
	uint8_t *random = &wrappedRds[kSessionHandleLength];

	sys_put_le32(handle, wrappedRds);
	sys_rand_get(random, kWrappedRdsRandomLength);
	memset(random, 0xB5, 4);
	memcpy(&wrappedRds[kSessionHandleLength + kWrappedRdsRandomLength], ursk.data(), ursk.size());

	const tUWBAPI_STATUS status = UwbApi_SetAppConfigWrappedRDS(handle, wrappedRds, sizeof(wrappedRds));

	/* Do not leave the key on the stack. */
	for (volatile uint8_t *p = wrappedRds; p < wrappedRds + sizeof(wrappedRds); p++) {
		*p = 0;
	}

	return status;
}

/** Sets the ranging parameters: the reader is the Responder, i.e. the CCC Controlee. */
tUWBAPI_STATUS ApplyRangingParams(uint32_t handle)
{
	phRangingParams_t rangingParams{};

	rangingParams.deviceRole = kUWB_DeviceRole_Responder;
	rangingParams.multiNodeMode = kUWB_MultiNodeMode_UniCast;
	rangingParams.macAddrMode = 0x0; /* 2 byte addresses. */
	memcpy(rangingParams.deviceMacAddr, gDeviceMacAddr, sizeof(gDeviceMacAddr));
	rangingParams.deviceType = kUWB_DeviceType_Controlee;
	rangingParams.rangingRoundUsage = kUWB_RangingRoundUsage_DS_TWR;
	rangingParams.scheduledMode = kUWB_ScheduledMode_TimeScheduled;

	return UwbApi_SetRangingParams(handle, &rangingParams);
}

} // namespace

int UltraWideBandImpl::SelectSessionParameters(AliroSessionConfig &config) const
{
	/* The SR150 reports the CCC slot and hopping bitmasks in the Aliro wire format. */
	const uint8_t readerSlotBitmask = mDevCap.slotBitmask;
	const uint8_t readerHoppingBitmask = mDevCap.hoppingConfigBitmask;

	/* RAN multiplier: the largest of the User Device value and the reader minimum. */
	config.mSelectedRanMultiplier = MAX(config.mRanMultiplier, kMinRanMultiplier);
	config.mSelectedSlotsPerRound = Message::kNumberSlotsPerRoundDefault;
	config.mMacMode = kMacMode;

	/* Number_Chaps_per_Slot: the smallest value supported by both. */
	const uint8_t slotCommon = config.mSlotBitmask & readerSlotBitmask;
	size_t slotBit = 0;

	while (slotBit < ARRAY_SIZE(kChapsPerSlot) && !(slotCommon & BIT(slotBit))) {
		slotBit++;
	}
	VerifyOrReturnValue(slotBit < ARRAY_SIZE(kChapsPerSlot), -ENOTSUP,
			    LOG_ERR("No common slot duration (device 0x%02x, reader 0x%02x)", config.mSlotBitmask,
				    readerSlotBitmask));
	config.mSelectedChapsPerSlot = kChapsPerSlot[slotBit];

	/* Hopping: the first preferred configuration supported by both. */
	const uint8_t hoppingCommon = config.mHoppingConfigBitmask & readerHoppingBitmask;

	for (const uint8_t hopping : kPreferredHoppingConfigs) {
		if ((hoppingCommon & hopping) == hopping) {
			config.mSelectedHoppingBitmask = hopping;
			return 0;
		}
	}

	LOG_ERR("No common hopping config (device 0x%02x, reader 0x%02x)", config.mHoppingConfigBitmask,
		readerHoppingBitmask);
	return -ENOTSUP;
}

int UltraWideBandImpl::HandleM2(const uint8_t *payload, size_t payloadLength, SessionContext &sessionCtx)
{
	VerifyOrReturnValue(sessionCtx.mSetupState == SetupState::M1Sent, -EPERM,
			    LOG_ERR("Invalid setup state for M2: %u", static_cast<unsigned int>(sessionCtx.mSetupState)));
	VerifyOrReturnValue(mDevCapValid, -EINVAL, LOG_ERR("UWB capabilities are not available."));
	VerifyOrReturnValue(mCallbacks.mBleMessageTransmit, -EINVAL,
			    LOG_ERR("BLE message transmit callback is not set."));

	/* Work on a copy so a failed M2 leaves the session config untouched. */
	AliroSessionConfig config = sessionCtx.mAliroConfig;

	int err = Message::ParseM2(payload, payloadLength, sessionCtx.mSessionId, config);
	VerifyOrReturnValue(err == 0, err, LOG_ERR("Failed to parse M2: %d", err));

	LOG_INF("M2: configId=0x%04x pulseShape=0x%02x channels=0x%02x syncCodes=0x%08x ranMult=%u slots=0x%02x "
		"hopping=0x%02x",
		config.mUwbConfigId, config.mPulseShapeCombo, config.mChannelBitmask, config.mSyncCodeIndexBitmask,
		config.mRanMultiplier, config.mSlotBitmask, config.mHoppingConfigBitmask);

	err = SelectSessionParameters(config);
	VerifyOrReturnValue(err == 0, err);

	Message::M3Params params{};
	params.mRanMultiplier = config.mSelectedRanMultiplier;
	params.mNumberChapsPerSlot = config.mSelectedChapsPerSlot;
	params.mNumberSlotsPerRound = config.mSelectedSlotsPerRound;
	/* The SYNC code index bitmask is a UCI field, i.e. little-endian. */
	params.mSyncCodeIndexBitmask = sys_get_le32(mDevCap.syncCodeIndexBitmask);
	params.mHoppingConfigBitmask = config.mSelectedHoppingBitmask;
	params.mMacMode = config.mMacMode;

	uint8_t m3[Message::kM3MaxLength];
	const size_t m3Length = Message::BuildM3(params, m3, sizeof(m3));
	VerifyOrReturnValue(m3Length > 0, -EINVAL, LOG_ERR("Failed to build M3 message."));

	sessionCtx.mAliroConfig = config;
	sessionCtx.mSetupState = SetupState::M2Received;

	LOG_INF("Sending M3: ranMult=%u chaps=%u hopping=0x%02x", params.mRanMultiplier, params.mNumberChapsPerSlot,
		params.mHoppingConfigBitmask);
	LOG_HEXDUMP_INF(m3, m3Length, "M3");

	mCallbacks.mBleMessageTransmit(sessionCtx.mSessionContextData, m3, m3Length);
	sessionCtx.mSetupState = SetupState::M3Sent;

	return 0;
}

int UltraWideBandImpl::HandleM4(const uint8_t *payload, size_t payloadLength, SessionContext &sessionCtx)
{
	VerifyOrReturnValue(sessionCtx.mSetupState == SetupState::M3Sent, -EPERM,
			    LOG_ERR("Invalid setup state for M4: %u", static_cast<unsigned int>(sessionCtx.mSetupState)));
	VerifyOrReturnValue(mDevCapValid, -EINVAL, LOG_ERR("UWB capabilities are not available."));

	/* Work on a copy so a failed M4 leaves the session config untouched. */
	AliroSessionConfig config = sessionCtx.mAliroConfig;

	const int err = Message::ParseM4(payload, payloadLength, sessionCtx.mSessionId, config);
	VerifyOrReturnValue(err == 0, err, LOG_ERR("Failed to parse M4: %d", err));

	/* The selected SYNC code index must be one the reader advertised in M3 (bit n is index n + 1). */
	const uint32_t readerSyncCodes = sys_get_le32(mDevCap.syncCodeIndexBitmask);
	VerifyOrReturnValue(config.mSyncCodeIndex >= 1 && config.mSyncCodeIndex <= 32 &&
				    (readerSyncCodes & BIT(config.mSyncCodeIndex - 1)),
			    -EINVAL, LOG_ERR("Invalid SYNC code index %u (reader bitmask 0x%08x)", config.mSyncCodeIndex,
					     readerSyncCodes));

	LOG_INF("M4: stsIndex=0x%08x uwbTime=%llu us syncCodeIndex=%u", config.mStsIndex,
		static_cast<unsigned long long>(config.mUwbTimeUs), config.mSyncCodeIndex);
	LOG_HEXDUMP_INF(config.mHopModeKey, sizeof(config.mHopModeKey), "M4 hop mode key");

	sessionCtx.mAliroConfig = config;

	/* Create and configure the SR150 session, then start ranging. The SR150 follows the block counter and STS of
	 * the User Device by itself, so UWB Time0 and a BLE/UWB time synchronization are not needed. */
	const int setupErr = SetupUwbSession(sessionCtx);
	VerifyOrReturnValue(setupErr == 0, setupErr, LOG_ERR("Failed to set up UWB session: %d", setupErr));

	const int startErr = StartUwbRanging(sessionCtx);
	if (startErr != 0) {
		/* Do not leave a half started session in the UWBS. */
		StopRanging(sessionCtx);
		return startErr;
	}

	sessionCtx.mSetupState = SetupState::M4Received;
	return 0;
}

int UltraWideBandImpl::SetupUwbSession(SessionContext &sessionCtx)
{
	const AliroSessionConfig &config = sessionCtx.mAliroConfig;

	VerifyOrReturnValue(sessionCtx.mNxpSessionHandle == 0, -EALREADY, LOG_ERR("SR150 session already exists."));

	/* Channel: M3 carries no channel, so the User Device selects exactly one in M2. */
	uint32_t channel;

	if (config.mChannelBitmask == kChannelBitmask5) {
		channel = CH_5;
	} else if (config.mChannelBitmask == kChannelBitmask9) {
		channel = CH_9;
	} else {
		LOG_ERR("Unsupported channel bitmask 0x%02x", config.mChannelBitmask);
		return -EINVAL;
	}
	VerifyOrReturnValue(mDevCap.channelBitmask & config.mChannelBitmask, -ENOTSUP,
			    LOG_ERR("Channel bitmask 0x%02x not supported by the reader (0x%02x)",
				    config.mChannelBitmask, mDevCap.channelBitmask));

	uint32_t hoppingMode;

	if (config.mSelectedHoppingBitmask == kHoppingNone) {
		hoppingMode = kHoppingModeNone;
	} else if (config.mSelectedHoppingBitmask == kHoppingContinuousDefault) {
		hoppingMode = kHoppingModeContinuousModulo;
	} else {
		LOG_ERR("Unsupported hopping bitmask 0x%02x", config.mSelectedHoppingBitmask);
		return -ENOTSUP;
	}

	uint32_t nxpHandle = 0;
	tUWBAPI_STATUS status = UwbApi_SessionInit(sessionCtx.mSessionId, UWBD_CSA_SESSION, &nxpHandle);
	VerifyOrReturnValue(status == UWBAPI_STATUS_OK, ConvertUwbApiStatus(status),
			    LOG_ERR("UwbApi_SessionInit failed: %d", status));
	sessionCtx.mNxpSessionHandle = nxpHandle;

	/* Releases the SR150 session again if a configuration step fails. */
	const auto failed = [&](const char *step, tUWBAPI_STATUS stepStatus) {
		LOG_ERR("%s failed: %d", step, stepStatus);
		StopRanging(sessionCtx);
		return ConvertUwbApiStatus(stepStatus);
	};

	status = ApplySessionAppConfigs(nxpHandle, config, sessionCtx.mProtocolVersion, channel, hoppingMode);
	if (status != UWBAPI_STATUS_OK) {
		return failed("ApplySessionAppConfigs", status);
	}

	status = ApplyAntennaConfig(nxpHandle);
	if (status != UWBAPI_STATUS_OK) {
		return failed("ApplyAntennaConfig", status);
	}

	status = ApplyUrsk(nxpHandle, sessionCtx.mUrsk);
	if (status != UWBAPI_STATUS_OK) {
		return failed("ApplyUrsk", status);
	}

	status = ApplyRangingParams(nxpHandle);
	if (status != UWBAPI_STATUS_OK) {
		return failed("ApplyRangingParams", status);
	}

	LOG_INF("UWB session configured (sessionId=0x%08X, nxpHandle=0x%08X, ch=%u, slot=%uRSTU, interval=%ums)",
		sessionCtx.mSessionId, nxpHandle, channel, config.mSelectedChapsPerSlot * kChapDurationRstu,
		config.mSelectedRanMultiplier * kRanMultiplierUnitMs);
	return 0;
}

int UltraWideBandImpl::StartUwbRanging(SessionContext &sessionCtx)
{
	VerifyOrReturnValue(sessionCtx.mNxpSessionHandle != 0, -EINVAL, LOG_ERR("SR150 session does not exist."));

	const tUWBAPI_STATUS status = UwbApi_StartRangingSession(sessionCtx.mNxpSessionHandle);
	VerifyOrReturnValue(status == UWBAPI_STATUS_OK, ConvertUwbApiStatus(status),
			    LOG_ERR("UwbApi_StartRangingSession failed: %d", status));

	sessionCtx.mRangingSessionState = RangingSessionState::Ranging;
	LOG_INF("UWB ranging started (sessionId=0x%08X, nxpHandle=0x%08X)", sessionCtx.mSessionId,
		sessionCtx.mNxpSessionHandle);

	if (mCallbacks.mRangingSessionStateChanged) {
		mCallbacks.mRangingSessionStateChanged(sessionCtx.mSessionContextData, RangingSessionState::Ranging);
	}
	return 0;
}

void UltraWideBandImpl::StopRanging(SessionContext &sessionCtx)
{
	if (sessionCtx.mNxpSessionHandle == 0) {
		return;
	}

	const bool wasRanging = sessionCtx.mRangingSessionState == RangingSessionState::Ranging;

	if (wasRanging) {
		const tUWBAPI_STATUS status = UwbApi_StopRangingSession(sessionCtx.mNxpSessionHandle);
		if (status != UWBAPI_STATUS_OK) {
			LOG_WRN("UwbApi_StopRangingSession failed: %d", status);
		}
	}

	const tUWBAPI_STATUS status = UwbApi_SessionDeinit(sessionCtx.mNxpSessionHandle);
	if (status != UWBAPI_STATUS_OK) {
		LOG_WRN("UwbApi_SessionDeinit failed: %d", status);
	}

	sessionCtx.mNxpSessionHandle = 0;
	sessionCtx.mRangingSessionState = RangingSessionState::Initialized;

	/* Lets the Aliro stack revoke the open permission of a session that was ranging. */
	if (wasRanging && mCallbacks.mRangingSessionStateChanged) {
		mCallbacks.mRangingSessionStateChanged(sessionCtx.mSessionContextData, RangingSessionState::Destroyed);
	}
}

int UltraWideBandImpl::HandleSuspendRequest(const uint8_t *payload, size_t payloadLength, SessionContext &sessionCtx)
{
	VerifyOrReturnValue(sessionCtx.mRangingSessionState == RangingSessionState::Ranging, -EPERM,
			    LOG_ERR("Invalid ranging session state for Suspend Request: %u",
				    static_cast<unsigned int>(sessionCtx.mRangingSessionState)));
	VerifyOrReturnValue(mCallbacks.mBleMessageTransmit, -EINVAL,
			    LOG_ERR("BLE message transmit callback is not set."));

	const int err = Message::ParseSuspendRequest(payload, payloadLength, sessionCtx.mSessionId);
	VerifyOrReturnValue(err == 0, err, LOG_ERR("Failed to parse Suspend Request: %d", err));

	LOG_INF("Suspending ranging (sessionId=0x%08X, nxpHandle=0x%08X)", sessionCtx.mSessionId,
		sessionCtx.mNxpSessionHandle);

	/* Only stop ranging: the session is kept configured so that it can be resumed. */
	const tUWBAPI_STATUS stopStatus = UwbApi_StopRangingSession(sessionCtx.mNxpSessionHandle);
	const bool accepted = stopStatus == UWBAPI_STATUS_OK;

	if (accepted) {
		sessionCtx.mRangingSessionState = RangingSessionState::RangingSuspended;
	} else {
		LOG_ERR("UwbApi_StopRangingSession failed: %d, rejecting the Suspend Request", stopStatus);
	}

	uint8_t response[Message::kSuspendResponseLength];
	const size_t responseLength = Message::BuildSuspendResponse(
		accepted ? Message::RangingServiceStatus::Accept : Message::RangingServiceStatus::Reject, response,
		sizeof(response));
	VerifyOrReturnValue(responseLength > 0, -EINVAL, LOG_ERR("Failed to build Suspend Response."));

	LOG_INF("Sending Suspend Response (%s)", accepted ? "accept" : "reject");
	LOG_HEXDUMP_INF(response, responseLength, "Suspend Response");
	mCallbacks.mBleMessageTransmit(sessionCtx.mSessionContextData, response, responseLength);

	if (accepted && mCallbacks.mRangingSessionStateChanged) {
		mCallbacks.mRangingSessionStateChanged(sessionCtx.mSessionContextData,
						       RangingSessionState::RangingSuspended);
	}

	return 0;
}

int UltraWideBandImpl::HandleRangingServiceMessage(uint8_t messageId, const uint8_t *payload, size_t payloadLength,
						   SessionContext &sessionCtx)
{
	switch (static_cast<Message::RangingServiceMessage>(messageId)) {
	case Message::RangingServiceMessage::SetupM2:
		return HandleM2(payload, payloadLength, sessionCtx);
	case Message::RangingServiceMessage::SetupM4:
		return HandleM4(payload, payloadLength, sessionCtx);
	case Message::RangingServiceMessage::SuspendRequest:
		return HandleSuspendRequest(payload, payloadLength, sessionCtx);
	default:
		/* TODO: resume. */
		LOG_INF("UWB Ranging Service message 0x%02x not implemented (payload=%zu)", messageId,
			payloadLength);
		return 0;
	}
}

int UltraWideBandImpl::HandleRangingNotification(const uint8_t *payload, size_t payloadLength,
						 SessionContext &sessionCtx)
{
	using Attribute = Message::NotificationMessageRangingAttribute;

	uint32_t attributes = 0;
	const int err = Message::ParseRangingNotification(payload, payloadLength, attributes);
	VerifyOrReturnValue(err == 0, err, LOG_ERR("Failed to parse ranging notification: %d", err));

	if (attributes & BIT(static_cast<uint8_t>(Attribute::InitiateRangingSession))) {
		LOG_INF("Initiate ranging session");
		const int initErr = _InitiateRangingSession(sessionCtx.mSessionContextData);
		VerifyOrReturnValue(initErr == 0, initErr);
	}

	constexpr uint32_t kNotImplemented = BIT(static_cast<uint8_t>(Attribute::InitiateRangingSessionResume)) |
					     BIT(static_cast<uint8_t>(Attribute::InitiateRangingSessionSetupLater)) |
					     BIT(static_cast<uint8_t>(Attribute::InitiateRangingSessionResumeLater)) |
					     BIT(static_cast<uint8_t>(Attribute::RangingSessionSuspended));

	/* TODO: process these attributes. SecureRangingOverUWBRadioFailed and unknown IDs are ignored. */
	if (attributes & kNotImplemented) {
		LOG_INF("Ranging notification attributes 0x%08x not implemented", attributes & kNotImplemented);
	}

	return 0;
}

int UltraWideBandImpl::HandleNotificationMessage(uint8_t messageId, const uint8_t *payload, size_t payloadLength,
						 SessionContext &sessionCtx)
{
	switch (static_cast<Message::NotificationMessage>(messageId)) {
	case Message::NotificationMessage::Ranging:
		return HandleRangingNotification(payload, payloadLength, sessionCtx);
	default:
		LOG_INF("Notification message 0x%02x not supported (payload=%zu)", messageId, payloadLength);
		return 0;
	}
}

} // namespace Aliro::Uwb
