/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "uwb_impl.h"

#include "aliro_uwb_message.h"
#include "session_management.h"

#include <doorlock/utils/memory.h>
#include <doorlock/utils/utils.h>

#include <errno.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

extern "C" {
#include <UwbApi.h>
#include <UwbApi_Types.h>
#include <uci_defs.h>
#include <uwb_types.h>
#include <nxAntennaDefine.h>
}

LOG_MODULE_REGISTER(UwbImpl, CONFIG_DOOR_LOCK_ALIRO_UWB_LOG_LEVEL);

namespace Aliro::Uwb {

namespace {

/** Distances above this are not reported to the Aliro stack. */
constexpr uint16_t kUwbMaximumReportableDistanceCm{ 500 };
/** Distance reported by the UWBS when a ranging block has no result. */
constexpr uint16_t kUwbInvalidDistanceCm{ 0xFFFF };

/**
 * @brief Returns the number of valid pulse shape combos in the SR150 capabilities.
 *
 * The NXP API copies the combos into a zero-padded 9 byte array without keeping the received
 * length, so trailing zero bytes are treated as padding. At least one combo is always reported
 * since combo 0x00 is mandatory.
 */
size_t GetPulseShapeComboCount(const phUwbCapInfo_t &devCap)
{
	size_t count = sizeof(devCap.supportedPulseShapeCombo);

	while (count > 1 && devCap.supportedPulseShapeCombo[count - 1] == 0) {
		count--;
	}

	return count;
}

} // namespace

int UltraWideBandImpl::_Init(const Callbacks &callbacks)
{
	tUWBAPI_STATUS status;
	phUwbDevInfo_t devInfo;

	if (mInitialized) {
		LOG_WRN("SR150 UWB is already initialized");
		return 0;
	}

	mCallbacks = callbacks;
	VerifyOrReturnValue(k_mutex_init(&mMutex) == 0, -EIO, LOG_ERR("Failed to initialize session mutex"));
	sys_slist_init(&mActiveSessionsList);

	/* The SR150 firmware image comes from the NXP package (drivers/uwb/nxp). */
	status = UwbApi_Init(UltraWideBandImpl::RangingCallback);
	if (status != UWBAPI_STATUS_OK) {
		LOG_ERR("UwbApi_Init failed: %d", status);
		return -EIO;
	}

	uint8_t calibValue[] = {
		0x03,                   /* Number of registers (must be 0x03) */
		AD_U16_TO_AU8_LE(15),   /* CAP1 */
		AD_U16_TO_AU8_LE(15),   /* CAP2 */
		AD_U16_TO_AU8_LE(0x21), /* GM CURRENT CONTROL */
	};

	status = UwbApi_SetCalibration(CH_9, RF_CLK_ACCURACY_CALIB, calibValue, sizeof(calibValue));
	if (status != UWBAPI_STATUS_OK) {
		LOG_ERR("UwbApi_SetCalibration(RF_CLK_ACCURACY_CALIB) failed: %d", status);
	}

	status = UwbApi_GetDeviceInfo(&devInfo);
	if (status != UWBAPI_STATUS_OK) {
		LOG_ERR("UwbApi_GetDeviceInfo failed: %d", status);
		UwbApi_ShutDown();
		return -EIO;
	}

	LOG_INF("UWB device: FW %u.%u.%u, MW %u.%u.%u", devInfo.fwMajor, devInfo.fwMinor, devInfo.fwRc,
		devInfo.mwMajor, devInfo.mwMinor, devInfo.mwRc);

	if (GetDeviceCapabilities() != 0) {
		UwbApi_ShutDown();
		return -EIO;
	}

	mInitialized = true;
	return 0;
}

int UltraWideBandImpl::GetDeviceCapabilities()
{
	mDevCapValid = false;
	mDevCap = {};

	const tUWBAPI_STATUS status = UwbApi_GetDeviceCapability(&mDevCap);
	VerifyOrReturnValue(status == UWBAPI_STATUS_OK, ConvertUwbApiStatus(status),
			    LOG_ERR("UwbApi_GetDeviceCapability failed: %d", status));

	mDevCapValid = true;
    LOG_INF("[CAP] FiRa PHY ver: %u.%u - %u.%u, MAC ver: %u.%u - %u.%u",
            mDevCap.firaPhyLowerRangeMajorVersion,
            mDevCap.firaPhyLowerRangeMinorMaintenanceVersion,
            mDevCap.firaPhyHigherRangeMajorVersion,
            mDevCap.firaPhyHigherRangeMinorMaintenanceVersion,
            mDevCap.firaMacLowerRangeMajorVersion,
            mDevCap.firaMacLowerRangeMinorMaintenanceVersion,
            mDevCap.firaMacHigherRangeMajorVersion,
            mDevCap.firaMacHigherRangeMinorMaintenanceVersion);
    LOG_INF("[CAP] deviceTypes=0x%02x deviceRoles=0x%04x rangingMethod=0x%04x stsConfig=0x%02x multiNode=0x%02x",
            mDevCap.deviceTypes,
            mDevCap.deviceRoles,
            mDevCap.rangingMethod,
            mDevCap.stsConfig,
            mDevCap.multiNodeMode);
    LOG_INF("[CAP] rangingTimeStruct=0x%02x scheduledMode=0x%02x hopping=0x%02x blockStriding=0x%02x initTime=0x%02x",
            mDevCap.rangingTimeStruct,
            mDevCap.scheduledMode,
            mDevCap.hoppingMode,
            mDevCap.blockStriding,
            mDevCap.uwbInitiationTime);
    LOG_INF("[CAP] channels=0x%02x rframeConfig=0x%02x ccConstraint=0x%02x bprfSets=0x%02x aoa=0x%02x extMac=0x%02x",
            mDevCap.channels,
            mDevCap.rframeConfig,
            mDevCap.ccConstraintLength,
            mDevCap.bprfParameterSets,
            mDevCap.aoaSupport,
            mDevCap.extendedMacAddress);
    LOG_INF("[CAP] suspendRanging=0x%02x sessionKeyLen=0x%02x maxMsgSize=%u maxDataPayload=%u",
            mDevCap.suspendRanging,
            mDevCap.sessionKeyLen,
            mDevCap.maxMessageSize,
            mDevCap.maxDataPacketPayloadSize);
    LOG_HEXDUMP_INF(mDevCap.hprfParameterSets, sizeof(mDevCap.hprfParameterSets), "[CAP] hprfParameterSets");

#if UWBIOT_UWBD_SR1XXT_SR2XXT
    LOG_INF("[CAP][CCC] slotBitmask=0x%02x channelBitmask=0x%02x hoppingConfigBitmask=0x%02x",
            mDevCap.slotBitmask,
            mDevCap.channelBitmask,
            mDevCap.hoppingConfigBitmask);
    LOG_INF("[CAP][CCC] supportedProtocolVersion=0x%04x supportedUWBConfigID=0x%04x",
            mDevCap.supportedProtocolVersion,
            mDevCap.supportedUWBConfigID);
    LOG_HEXDUMP_INF(mDevCap.syncCodeIndexBitmask, sizeof(mDevCap.syncCodeIndexBitmask), "[CAP][CCC] syncCodeIndexBitmask");
    LOG_HEXDUMP_INF(mDevCap.supportedPulseShapeCombo, sizeof(mDevCap.supportedPulseShapeCombo), "[CAP][CCC] supportedPulseShapeCombo");
    LOG_INF("[CAP] maxUciPayloadLength=%u inbandDataBlockSize=%u inbandDataMaxBlock=%u tagBlockSkipping=%u psduLenSupport=%u",
            mDevCap.maxUciPayloadLength,
            mDevCap.inbandDataBlockSize,
            mDevCap.inbandDataMaxBlock,
            mDevCap.tagBlockSkipping,
            mDevCap.psduLengthSupport);
#endif
	return 0;
}

int UltraWideBandImpl::_Deinit()
{
	if (!mInitialized) {
		return 0;
	}

	RemoveAllSessions();
	mDevCapValid = false;

	if (UwbApi_ShutDown() != UWBAPI_STATUS_OK) {
		LOG_ERR("UwbApi_ShutDown failed");
		return -EIO;
	}

	mInitialized = false;
	return 0;
}

void UltraWideBandImpl::
	_BleTimeSync() { /* Time synchronization is not needed: the SR150 follows the block counter of the User Device. */ };

int UltraWideBandImpl::_HandleBleMessage(const uint8_t *data, size_t length, SessionContextHandle sessionContextData)
{
	VerifyOrReturnValue(data && length > 0, -EINVAL, LOG_ERR("Invalid BLE message data."));

	LOG_INF("Received BLE message (length=%zu, session handle=%p)", length, sessionContextData.GetRaw());
	LOG_HEXDUMP_INF(data, length, "BLE message data");

	auto *sessionCtx = FindSession(sessionContextData);
	VerifyOrReturnValue(sessionCtx, -ENOENT,
			    LOG_ERR("Session context not found for handle: %p", sessionContextData.GetRaw()));

	Message::Header header;
	const int err = Message::ParseHeader(data, length, header);
	VerifyOrReturnValue(err == 0, err);

	switch (header.mProtocol) {
	case Message::Protocol::UwbRangingService:
		return HandleRangingServiceMessage(header.mMessageId, header.mPayload, header.mPayloadLength,
						   *sessionCtx);
	case Message::Protocol::Notification:
		return HandleNotificationMessage(header.mMessageId, header.mPayload, header.mPayloadLength,
						 *sessionCtx);
	default:
		LOG_WRN("Ignoring message of protocol 0x%02x", static_cast<uint8_t>(header.mProtocol));
		return 0;
	}
}

int UltraWideBandImpl::_ConfigureRangingSession(SessionIdentifier sessionId, const CryptoTypes::Ursk &ursk,
					      ProtocolVersion protocolVersion,
					      SessionContextHandle sessionContextHandle)
{
	using DoorLock::Utils::new_nothrow;

	VerifyOrReturnValue(mInitialized, -EBUSY, LOG_ERR("UWB is not initialized."));

	auto *existingSession = FindSession(sessionContextHandle);
	VerifyOrReturnValue(!existingSession, -EBUSY,
			    LOG_ERR("Session context already exists for handle: %p", sessionContextHandle.GetRaw()));

	auto *newSessionCtx =
		new_nothrow<SessionContext>(sessionId, sessionContextHandle, ursk, protocolVersion);
	VerifyOrReturnValue(newSessionCtx, -ENOMEM, LOG_ERR("Memory allocation failed for session context."));

	int err = 0;
	err = AddSession(newSessionCtx);
	if (err != 0) {
		LOG_ERR("Failed to add session to the active sessions list: %d", err);
		goto exit;
	}

	newSessionCtx->mRangingSessionState = RangingSessionState::Initialized;
	LOG_INF("UWB session configured (sessionId=0x%08X, handle=%p)", sessionId, sessionContextHandle.GetRaw());
	return 0;

exit:
	DestroySession(newSessionCtx);
	delete newSessionCtx;

	return err;
}

int UltraWideBandImpl::_InitiateRangingSession(SessionContextHandle sessionContextData)
{
	auto *sessionCtx = FindSession(sessionContextData);

	VerifyOrReturnValue(sessionCtx, -ENOENT,
			    LOG_ERR("Session context not found for handle: %p", sessionContextData.GetRaw()));
	VerifyOrReturnValue(sessionCtx->mSetupState == SetupState::Created, -EALREADY,
			    LOG_ERR("Session setup already started (state=%u)",
				    static_cast<unsigned int>(sessionCtx->mSetupState)));
	VerifyOrReturnValue(mDevCapValid, -EINVAL, LOG_ERR("UWB capabilities are not available."));
	VerifyOrReturnValue(mCallbacks.mBleMessageTransmit, -EINVAL,
			    LOG_ERR("BLE message transmit callback is not set."));

	LOG_INF("Initiating UWB session (sessionId=0x%08X, handle=%p)", sessionCtx->mSessionId,
		sessionContextData.GetRaw());

	/* The SR150 reports a single UWB configuration ID. */
	const uint16_t configIds[] = { mDevCap.supportedUWBConfigID };
	const size_t pulseShapeComboCount = GetPulseShapeComboCount(mDevCap);

	Message::M1Params params{};
	params.mConfigIds = configIds;
	params.mConfigIdCount = ARRAY_SIZE(configIds);
	params.mPulseShapeCombos = mDevCap.supportedPulseShapeCombo;
	params.mPulseShapeComboCount = pulseShapeComboCount;
	params.mChannelBitmask = mDevCap.channelBitmask;
	params.mSessionId = sessionCtx->mSessionId;

	uint8_t m1[Message::kM1MaxLength];
	const size_t m1Length = Message::BuildM1(params, m1, sizeof(m1));
	VerifyOrReturnValue(m1Length > 0, -EINVAL, LOG_ERR("Failed to build M1 message."));

	LOG_INF("Sending M1 (sessionId=0x%08X, handle=%p)", sessionCtx->mSessionId, sessionContextData.GetRaw());
	LOG_HEXDUMP_INF(m1, m1Length, "M1");

	sessionCtx->mSetupState = SetupState::M1Sent;
	mCallbacks.mBleMessageTransmit(sessionContextData, m1, m1Length);

	return 0;
}

int UltraWideBandImpl::_TerminateRangingSession(SessionContextHandle sessionContextData)
{
	LOG_INF("Terminated UWB session (handle=%p)", sessionContextData.GetRaw());
	auto *sessionCtx = FindSession(sessionContextData);
	VerifyOrReturnValue(sessionCtx, -ENOENT,
			    LOG_ERR("Session context not found for handle: %p", sessionContextData.GetRaw()));

	RemoveSession(sessionCtx);

	return 0;
}

int UltraWideBandImpl::_SuspendRangingSession([[maybe_unused]] SessionContextHandle)
{
	LOG_ERR("SuspendRangingSession is not implemented yet.");
	return -ENOSYS;
}

int UltraWideBandImpl::_ResumeRangingSession([[maybe_unused]] SessionContextHandle)
{
	LOG_ERR("ResumeRangingSession is not implemented yet.");
	return -ENOSYS;
}

void UltraWideBandImpl::RangingCallback(eNotificationType type, void *data)
{
	switch (type) {
	case UWBD_RANGING_ALIRO_DATA:
		UltraWideBandInstanceImpl().HandleAliroRangingData(*static_cast<const phAliroRangingData_t *>(data));
		break;
	default:
		LOG_INF("Ignoring UWB notification of type %d", static_cast<int>(type));
		break;
	}
}

void UltraWideBandImpl::HandleAliroRangingData(const phAliroRangingData_t &d)
{
	LOG_INF("Aliro RngData: handle=0x%08X status=0x%02X sts=0x%08X block=%u round=%u dist=%ucm", d.sessionHandle,
		d.rangingStatus, d.stsIndex, d.blockIndex, d.rangingRoundIndex, d.distance);
	LOG_INF("  AoA az=%d (FOM %u) el=%d (FOM %u) fomAnchor=%u fomInitiator=%u", d.aoa_azimuth, d.aoa_azimuth_FOM,
		d.aoa_elevation, d.aoa_elevation_FOM, d.uncertanityAnchorFom, d.uncertanityInitiatorFom);
	LOG_INF("  Antenna mode=%u pair1=%u pair2=%u", d.antenna_pair_info.configMode, d.antenna_pair_info.antPairId1,
		d.antenna_pair_info.antPairId2);
	for (uint8_t i = 0; i < d.noOfPdoaMeasures && i < MAX_NO_OF_PDOA_MEASUREMENTS; i++) {
		LOG_INF("  PDoA[%u] pdoa=%d idx=%u", i, d.pdoaMeasurements[i].pdoa, d.pdoaMeasurements[i].pdoaIndex);
	}
	for (uint8_t i = 0; i < d.noOfRssiMeasurements && i < MAX_NO_OF_CCC_RSSI_MEASUREMENTS; i++) {
		LOG_INF("  RSSI[%u] rx1=%d rx2=%d (Q8.8)", i, d.cccRssiMeasurements[i].rssi_rx1,
			d.cccRssiMeasurements[i].rssi_rx2);
	}

	/* Only blocks with a complete ranging exchange carry a usable distance. */
	if (d.rangingStatus != CCC_STATUS_SUCCESS || d.distance == kUwbInvalidDistanceCm) {
		LOG_INF("Ignoring ranging block %u without a valid result (status 0x%02X)", d.blockIndex,
			d.rangingStatus);
		return;
	}

	if (d.distance > kUwbMaximumReportableDistanceCm) {
		LOG_INF("Ignoring measured distance %ucm", d.distance);
		return;
	}

	const auto *sessionCtx = FindSessionByNxpHandle(d.sessionHandle);
	VerifyOrReturn(sessionCtx, LOG_WRN("No session for SR150 handle 0x%08X", d.sessionHandle));
	VerifyOrReturn(mCallbacks.mRangingData);

	/* The Aliro stack expects the distance in cm as a big-endian 16-bit value. */
	uint8_t distance[sizeof(uint16_t)];

	sys_put_be16(d.distance, distance);
	mCallbacks.mRangingData(sessionCtx->mSessionContextData,
				UwbRangingData{ .mData = distance, .mLength = sizeof(distance) });
}

} // namespace Aliro::Uwb
