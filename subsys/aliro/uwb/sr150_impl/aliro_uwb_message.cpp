/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "aliro_uwb_message.h"

#include <doorlock/utils/utils.h>

#include <errno.h>
#include <cstring>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(UwbImpl);

namespace Aliro::Uwb::Message {

namespace {

/** Reads a big-endian unsigned integer of @p length bytes (<= 8). */
uint64_t ReadBigEndian(const uint8_t *value, size_t length)
{
	uint64_t result = 0;

	for (size_t i = 0; i < length; i++) {
		result = (result << 8) | value[i];
	}
	return result;
}

/** Writes @p value as a big-endian unsigned integer of @p size bytes (<= 8) to @p out. */
void EncodeBigEndian(uint8_t *out, uint64_t value, size_t size)
{
	for (size_t i = 0; i < size; i++) {
		out[i] = static_cast<uint8_t>(value >> (8 * (size - 1 - i)));
	}
}

static_assert(kHopModeKeyLength == sizeof(AliroSessionConfig::mHopModeKey),
	      "The Hop Mode Key attribute must match the session config");

/**
 * @brief Parses one Ranging Service attribute into @p config.
 *
 * @return 0 on success, -EINVAL if the attribute length is wrong or the value is invalid.
 */
int ParseRangingServiceAttribute(RangingServiceAttribute id, const uint8_t *value, size_t length,
				 uint32_t expectedSessionId, AliroSessionConfig &config)
{
	/* Attribute value sizes are fixed by the Aliro specification. */
	const auto requireLength = [&](size_t expected) {
		if (length != expected) {
			LOG_ERR("Attribute 0x%02x: invalid length %zu (expected %zu)", static_cast<uint8_t>(id),
				length, expected);
			return false;
		}
		return true;
	};

	switch (id) {
	case RangingServiceAttribute::ConfigurationIdentifier:
		VerifyOrReturnValue(requireLength(kConfigIdLength), -EINVAL);
		config.mUwbConfigId = static_cast<uint16_t>(ReadBigEndian(value, length));
		break;
	case RangingServiceAttribute::PulseShapeCombo:
		VerifyOrReturnValue(requireLength(kU8ValueLength), -EINVAL);
		config.mPulseShapeCombo = value[0];
		break;
	case RangingServiceAttribute::SessionIdentifier: {
		VerifyOrReturnValue(requireLength(kSessionIdLength), -EINVAL);
		const auto sessionId = static_cast<uint32_t>(ReadBigEndian(value, length));
		VerifyOrReturnValue(sessionId == expectedSessionId, -EINVAL,
				    LOG_ERR("Bad session id 0x%08X (expected 0x%08X)", sessionId,
					    expectedSessionId));
		break;
	}
	case RangingServiceAttribute::ChannelBitmask:
		VerifyOrReturnValue(requireLength(kU8ValueLength), -EINVAL);
		config.mChannelBitmask = value[0];
		break;
	case RangingServiceAttribute::RanMultiplier:
		VerifyOrReturnValue(requireLength(kU8ValueLength), -EINVAL);
		config.mRanMultiplier = value[0];
		break;
	case RangingServiceAttribute::SlotBitmask:
		VerifyOrReturnValue(requireLength(kU8ValueLength), -EINVAL);
		config.mSlotBitmask = value[0];
		break;
	case RangingServiceAttribute::SyncCodeIndexBitmask:
		VerifyOrReturnValue(requireLength(kSyncCodeIndexBitmaskLength), -EINVAL);
		config.mSyncCodeIndexBitmask = static_cast<uint32_t>(ReadBigEndian(value, length));
		break;
	case RangingServiceAttribute::SyncCodeIndex:
		VerifyOrReturnValue(requireLength(kU8ValueLength), -EINVAL);
		config.mSyncCodeIndex = value[0];
		break;
	case RangingServiceAttribute::HoppingConfigurationBitmask:
		VerifyOrReturnValue(requireLength(kU8ValueLength), -EINVAL);
		config.mHoppingConfigBitmask = value[0];
		break;
	case RangingServiceAttribute::StsIndex0:
		VerifyOrReturnValue(requireLength(kStsIndexLength), -EINVAL);
		config.mStsIndex = static_cast<uint32_t>(ReadBigEndian(value, length));
		break;
	case RangingServiceAttribute::UwbTime0:
		VerifyOrReturnValue(requireLength(kUwbTimeLength), -EINVAL);
		config.mUwbTimeUs = ReadBigEndian(value, length);
		break;
	case RangingServiceAttribute::HopModeKey:
		VerifyOrReturnValue(requireLength(kHopModeKeyLength), -EINVAL);
		std::memcpy(config.mHopModeKey, value, sizeof(config.mHopModeKey));
		break;
	default:
		/* Vendor specific, status and unknown attributes are not used for now. */
		LOG_DBG("Ignoring attribute 0x%02x", static_cast<uint8_t>(id));
		break;
	}

	return 0;
}

/**
 * @brief Calls @p handler(id, value, length) for every attribute of a TLV list.
 *
 * Stops at the first error: a malformed list or a non-zero value returned by @p handler.
 */
template <typename Handler>
int ForEachAttribute(const uint8_t *payload, size_t payloadLength, Handler &&handler)
{
	size_t offset = 0;

	while (offset < payloadLength) {
		VerifyOrReturnValue(payloadLength - offset >= kAttributeHeaderLength, -EINVAL,
				    LOG_ERR("Truncated attribute header"));

		const uint8_t id = payload[offset];
		const size_t valueLength = payload[offset + 1];

		offset += kAttributeHeaderLength;
		VerifyOrReturnValue(payloadLength - offset >= valueLength, -EINVAL,
				    LOG_ERR("Truncated attribute 0x%02x value", id));

		const int err = handler(id, &payload[offset], valueLength);
		if (err != 0) {
			return err;
		}
		offset += valueLength;
	}

	return 0;
}

constexpr uint32_t AttributeBit(uint8_t id)
{
	return id < 32 ? BIT(id) : 0;
}

constexpr uint32_t AttributeBit(RangingServiceAttribute id)
{
	return AttributeBit(static_cast<uint8_t>(id));
}

/** Attributes that must be present in M4 (Vendor Specific is optional). */
constexpr uint32_t kM4RequiredMask = AttributeBit(RangingServiceAttribute::StsIndex0) |
				     AttributeBit(RangingServiceAttribute::UwbTime0) |
				     AttributeBit(RangingServiceAttribute::HopModeKey) |
				     AttributeBit(RangingServiceAttribute::SyncCodeIndex);

/** Attributes that must be present in M2 (Vendor Specific is optional). */
constexpr uint32_t kM2RequiredMask = AttributeBit(RangingServiceAttribute::ConfigurationIdentifier) |
				     AttributeBit(RangingServiceAttribute::PulseShapeCombo) |
				     AttributeBit(RangingServiceAttribute::ChannelBitmask) |
				     AttributeBit(RangingServiceAttribute::SyncCodeIndexBitmask) |
				     AttributeBit(RangingServiceAttribute::RanMultiplier) |
				     AttributeBit(RangingServiceAttribute::SlotBitmask) |
				     AttributeBit(RangingServiceAttribute::HoppingConfigurationBitmask);

/**
 * @brief Minimal bounds-checked writer for Aliro UWB messages.
 */
class Writer {
public:
	Writer(uint8_t *buffer, size_t capacity) : mBuffer(buffer), mCapacity(capacity) {}

	size_t Length() const { return mLength; }

	bool AddHeader(Protocol protocol, RangingServiceMessage message, uint16_t payloadLength)
	{
		if (!HasRoom(kHeaderLength)) {
			return false;
		}

		mBuffer[mLength + kHeaderProtocolOffset] = static_cast<uint8_t>(protocol);
		mBuffer[mLength + kHeaderMessageIdOffset] = static_cast<uint8_t>(message);
		EncodeBigEndian(&mBuffer[mLength + kHeaderPayloadLengthOffset], payloadLength, kPayloadLengthSize);
		mLength += kHeaderLength;
		return true;
	}

	bool AddAttribute(RangingServiceAttribute id, const uint8_t *value, size_t length)
	{
		if (length > UINT8_MAX || !HasRoom(kAttributeHeaderLength + length)) {
			return false;
		}

		mBuffer[mLength++] = static_cast<uint8_t>(id);
		mBuffer[mLength++] = static_cast<uint8_t>(length);
		if (length > 0) {
			std::memcpy(&mBuffer[mLength], value, length);
			mLength += length;
		}
		return true;
	}

	bool AddU8(RangingServiceAttribute id, uint8_t value) { return AddAttribute(id, &value, sizeof(value)); }

	bool AddU32(RangingServiceAttribute id, uint32_t value)
	{
		uint8_t buf[sizeof(value)];

		EncodeBigEndian(buf, value, sizeof(buf));
		return AddAttribute(id, buf, sizeof(buf));
	}

	bool AddU16Array(RangingServiceAttribute id, const uint16_t *values, size_t count)
	{
		if (!values || count == 0 || count > kM1MaxConfigIds) {
			return false;
		}

		uint8_t buf[kConfigIdLength * kM1MaxConfigIds];
		for (size_t i = 0; i < count; i++) {
			EncodeBigEndian(&buf[kConfigIdLength * i], values[i], kConfigIdLength);
		}
		return AddAttribute(id, buf, kConfigIdLength * count);
	}

private:
	bool HasRoom(size_t bytes) const { return mBuffer && mLength + bytes <= mCapacity; }

	uint8_t *mBuffer;
	size_t mCapacity;
	size_t mLength{ 0 };
};

} // namespace

size_t BuildM1(const M1Params &params, uint8_t *buffer, size_t bufferSize)
{
	if (!buffer || !params.mConfigIds || params.mConfigIdCount == 0 ||
	    params.mConfigIdCount > kM1MaxConfigIds || !params.mPulseShapeCombos ||
	    params.mPulseShapeComboCount == 0 || params.mPulseShapeComboCount > kM1MaxPulseShapeCombos) {
		return 0;
	}

	const size_t payloadLength = M1PayloadLength(params.mConfigIdCount, params.mPulseShapeComboCount);

	Writer writer(buffer, bufferSize);

	const bool ok = writer.AddHeader(Protocol::UwbRangingService, RangingServiceMessage::SetupM1,
					 static_cast<uint16_t>(payloadLength)) &&
			writer.AddU16Array(RangingServiceAttribute::ConfigurationIdentifier, params.mConfigIds,
					   params.mConfigIdCount) &&
			writer.AddAttribute(RangingServiceAttribute::PulseShapeCombo, params.mPulseShapeCombos,
					    params.mPulseShapeComboCount) &&
			writer.AddU8(RangingServiceAttribute::ChannelBitmask, params.mChannelBitmask) &&
			writer.AddU32(RangingServiceAttribute::SessionIdentifier, params.mSessionId);

	if (!ok || writer.Length() != kHeaderLength + payloadLength) {
		return 0;
	}

	return writer.Length();
}

int ParseHeader(const uint8_t *data, size_t length, Header &header)
{
	VerifyOrReturnValue(data && length >= kHeaderLength, -EINVAL,
			    LOG_ERR("BLE message too short (length=%zu)", length));

	const size_t payloadLength = ReadBigEndian(&data[kHeaderPayloadLengthOffset], kPayloadLengthSize);

	VerifyOrReturnValue(length == kHeaderLength + payloadLength, -EINVAL,
			    LOG_ERR("BLE message length mismatch (length=%zu, payload=%zu)", length, payloadLength));

	header.mProtocol = static_cast<Protocol>(data[kHeaderProtocolOffset]);
	header.mMessageId = data[kHeaderMessageIdOffset];
	header.mPayload = data + kHeaderLength;
	header.mPayloadLength = payloadLength;
	return 0;
}

namespace {

/** Parses a setup message into @p config, requiring all attributes in @p requiredMask. */
int ParseSetupMessage(const uint8_t *payload, size_t payloadLength, uint32_t expectedSessionId, uint32_t requiredMask,
		      AliroSessionConfig &config)
{
	/* Parse into a copy so a malformed message leaves @p config untouched. */
	AliroSessionConfig parsed = config;
	uint32_t receivedMask = 0;

	const int err = ForEachAttribute(payload, payloadLength, [&](uint8_t id, const uint8_t *value, size_t length) {
		receivedMask |= AttributeBit(id);
		return ParseRangingServiceAttribute(static_cast<RangingServiceAttribute>(id), value, length,
						    expectedSessionId, parsed);
	});
	VerifyOrReturnValue(err == 0, err);

	VerifyOrReturnValue((receivedMask & requiredMask) == requiredMask, -EINVAL,
			    LOG_ERR("Setup message is missing attributes (received mask 0x%08x, required 0x%08x)",
				    receivedMask, requiredMask));

	config = parsed;
	return 0;
}

} // namespace

int ParseM2(const uint8_t *payload, size_t payloadLength, uint32_t expectedSessionId, AliroSessionConfig &config)
{
	return ParseSetupMessage(payload, payloadLength, expectedSessionId, kM2RequiredMask, config);
}

int ParseSuspendRequest(const uint8_t *payload, size_t payloadLength, uint32_t expectedSessionId)
{
	AliroSessionConfig unused{};

	return ParseSetupMessage(payload, payloadLength, expectedSessionId,
				 AttributeBit(RangingServiceAttribute::SessionIdentifier), unused);
}

int ParseM4(const uint8_t *payload, size_t payloadLength, uint32_t expectedSessionId, AliroSessionConfig &config)
{
	return ParseSetupMessage(payload, payloadLength, expectedSessionId, kM4RequiredMask, config);
}

int ParseRangingNotification(const uint8_t *payload, size_t payloadLength, uint32_t &attributeMask)
{
	uint32_t mask = 0;

	const int err = ForEachAttribute(payload, payloadLength, [&](uint8_t id, const uint8_t *, size_t) {
		mask |= AttributeBit(id);
		return 0;
	});
	VerifyOrReturnValue(err == 0, err);

	attributeMask = mask;
	return 0;
}

size_t BuildM3(const M3Params &params, uint8_t *buffer, size_t bufferSize)
{
	Writer writer(buffer, bufferSize);

	const bool ok = writer.AddHeader(Protocol::UwbRangingService, RangingServiceMessage::SetupM3,
					 kM3PayloadLength) &&
			writer.AddU8(RangingServiceAttribute::RanMultiplier, params.mRanMultiplier) &&
			writer.AddU8(RangingServiceAttribute::NumberChapsPerSlot, params.mNumberChapsPerSlot) &&
			writer.AddU8(RangingServiceAttribute::NumberRespondersNodes, params.mNumberRespondersNodes) &&
			writer.AddU8(RangingServiceAttribute::NumberSlotsPerRound, params.mNumberSlotsPerRound) &&
			writer.AddU32(RangingServiceAttribute::SyncCodeIndexBitmask, params.mSyncCodeIndexBitmask) &&
			writer.AddU8(RangingServiceAttribute::HoppingConfigurationBitmask,
				     params.mHoppingConfigBitmask) &&
			writer.AddU8(RangingServiceAttribute::MacMode, params.mMacMode);

	if (!ok || writer.Length() != kM3MaxLength) {
		return 0;
	}

	return writer.Length();
}

size_t BuildSuspendResponse(RangingServiceStatus status, uint8_t *buffer, size_t bufferSize)
{
	Writer writer(buffer, bufferSize);

	const bool ok = writer.AddHeader(Protocol::UwbRangingService, RangingServiceMessage::SuspendResponse,
					 kSuspendResponsePayloadLength) &&
			writer.AddU8(RangingServiceAttribute::Status, static_cast<uint8_t>(status));

	if (!ok || writer.Length() != kSuspendResponseLength) {
		return 0;
	}

	return writer.Length();
}

} // namespace Aliro::Uwb::Message
