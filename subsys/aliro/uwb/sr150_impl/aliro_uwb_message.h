/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include "session_context.h"

#include <cstddef>
#include <cstdint>

/**
 * @file
 * @brief Aliro UWB Ranging Service message encoding (platform independent).
 *
 * Message layout (all multi-byte values are big-endian):
 *
 *   | Protocol (1) | Message ID (1) | Payload length (2) | Attributes... |
 *
 * Attribute layout:
 *
 *   | Attribute ID (1) | Length (1) | Value (Length) |
 */

namespace Aliro::Uwb::Message {

/* Message header: | Protocol (1) | Message ID (1) | Payload length (2) | */
constexpr size_t kHeaderProtocolOffset{ 0 };
constexpr size_t kHeaderMessageIdOffset{ 1 };
constexpr size_t kHeaderPayloadLengthOffset{ 2 };
constexpr size_t kPayloadLengthSize{ 2 };
constexpr size_t kHeaderLength{ kHeaderPayloadLengthOffset + kPayloadLengthSize }; /* 4 bytes. */

/* Attribute header: | Attribute ID (1) | Length (1) | */
constexpr size_t kAttributeHeaderLength{ 2 };

/* Sizes of the attribute values, fixed by the Aliro specification. */
constexpr size_t kU8ValueLength{ 1 };          /* Pulse shape combo, bitmasks, multipliers, counts, MAC mode, status. */
constexpr size_t kConfigIdLength{ 2 };         /* UWB Configuration Identifier. */
constexpr size_t kSessionIdLength{ 4 };        /* UWB Session Identifier. */
constexpr size_t kSyncCodeIndexBitmaskLength{ 4 };
constexpr size_t kStsIndexLength{ 4 };         /* STS Index0. */
constexpr size_t kUwbTimeLength{ 8 };          /* UWB Time0. */
constexpr size_t kHopModeKeyLength{ 4 };

/** Size of an attribute (attribute header and value) with a value of @p valueLength bytes. */
constexpr size_t AttributeSize(size_t valueLength)
{
	return kAttributeHeaderLength + valueLength;
}

enum class Protocol : uint8_t {
	Ap = 0x00,
	UwbRangingService = 0x01,
	Notification = 0x02,
	SupplementaryService = 0x03,
	ThirdPartyApp = 0x04,
};

enum class RangingServiceMessage : uint8_t {
	SetupM1 = 0x00,
	SetupM2 = 0x01,
	SetupM3 = 0x02,
	SetupM4 = 0x03,
	SuspendRequest = 0x04,
	SuspendResponse = 0x05,
	ResumeRequest = 0x06,
	ResumeResponse = 0x07,
};

/** Values of the Status attribute in Suspend/Resume responses. */
enum class RangingServiceStatus : uint8_t {
	Accept = 0x00,
	Reject = 0x01,
};

enum class RangingServiceAttribute : uint8_t {
	ConfigurationIdentifier = 0x00,
	PulseShapeCombo = 0x01,
	SessionIdentifier = 0x02,
	ChannelBitmask = 0x03,
	RanMultiplier = 0x04,
	SlotBitmask = 0x05,
	SyncCodeIndexBitmask = 0x06,
	SyncCodeIndex = 0x07,
	HoppingConfigurationBitmask = 0x08,
	NumberChapsPerSlot = 0x09,
	NumberRespondersNodes = 0x0A,
	NumberSlotsPerRound = 0x0B,
	StsIndex0 = 0x0C,
	UwbTime0 = 0x0D,
	HopModeKey = 0x0E,
	MacMode = 0x0F,
	VendorSpecific = 0x10,
	Status = 0x11,
};

enum class NotificationMessage : uint8_t {
	Event = 0x00,
	Ranging = 0x01,
	ReaderStatusChanged = 0x02,
	ReaderStatusAccessProtocolCompleted = 0x03,
	RKERequest = 0x04,
	InitiateAccessProtocol = 0x05,
	InitiateAccessProtocolRKE = 0x06,
};

enum class NotificationMessageRangingAttribute : uint8_t {
	InitiateRangingSession = 0x00,
	InitiateRangingSessionResume = 0x01,
	InitiateRangingSessionSetupLater = 0x02,
	InitiateRangingSessionResumeLater = 0x03,
	SecureRangingOverUWBRadioFailed = 0x04,
	RangingSessionSuspended = 0x05,
};

/** Upper bound on the number of UWB configuration IDs / pulse shape combos carried in M1. */
constexpr size_t kM1MaxConfigIds{ 8 };
constexpr size_t kM1MaxPulseShapeCombos{ 9 };

/**
 * Payload length of an M1 message with the given number of configuration IDs and pulse shape combos:
 * UWB Configuration Identifiers, Pulse Shape Combos, Channel Bitmask and UWB Session Identifier.
 */
constexpr size_t M1PayloadLength(size_t configIdCount, size_t pulseShapeComboCount)
{
	return AttributeSize(configIdCount * kConfigIdLength) + AttributeSize(pulseShapeComboCount * kU8ValueLength) +
	       AttributeSize(kU8ValueLength) + AttributeSize(kSessionIdLength);
}

/** Upper bound on the size of an encoded M1 message. */
constexpr size_t kM1MaxLength{ kHeaderLength + M1PayloadLength(kM1MaxConfigIds, kM1MaxPulseShapeCombos) };

/**
 * @brief Parameters carried in the Ranging Session Setup M1 message.
 */
struct M1Params {
	/** Supported UWB configuration identifiers. */
	const uint16_t *mConfigIds{ nullptr };
	size_t mConfigIdCount{ 0 };
	/** Supported pulse shape combinations (one byte per combination). */
	const uint8_t *mPulseShapeCombos{ nullptr };
	size_t mPulseShapeComboCount{ 0 };
	/** Supported channel bitmask. */
	uint8_t mChannelBitmask{ 0 };
	/** UWB session identifier. */
	uint32_t mSessionId{ 0 };
};

/** Fixed M3 values: the reader supports a single responder node and 12 slots per round. */
constexpr uint8_t kNumberRespondersNodes{ 1 };
constexpr uint8_t kNumberSlotsPerRoundDefault{ 12 };

/** Payload length of an M3 message, which always carries the same 7 attributes. */
constexpr size_t kM3PayloadLength{ AttributeSize(kU8ValueLength) +                /* RAN Multiplier. */
				   AttributeSize(kU8ValueLength) +                /* Number Chaps per Slot. */
				   AttributeSize(kU8ValueLength) +                /* Number Responders Nodes. */
				   AttributeSize(kU8ValueLength) +                /* Number Slots per Round. */
				   AttributeSize(kSyncCodeIndexBitmaskLength) +   /* SYNC Code Index Bitmask. */
				   AttributeSize(kU8ValueLength) +                /* Hopping Configuration Bitmask. */
				   AttributeSize(kU8ValueLength) };               /* MAC Mode. */

/** Size of an encoded M3 message. */
constexpr size_t kM3MaxLength{ kHeaderLength + kM3PayloadLength };

/**
 * @brief Parameters carried in the Ranging Session Setup M3 message.
 */
struct M3Params {
	uint8_t mRanMultiplier{ 0 };
	uint8_t mNumberChapsPerSlot{ 0 };
	uint8_t mNumberRespondersNodes{ kNumberRespondersNodes };
	uint8_t mNumberSlotsPerRound{ kNumberSlotsPerRoundDefault };
	uint32_t mSyncCodeIndexBitmask{ 0 };
	uint8_t mHoppingConfigBitmask{ 0 };
	uint8_t mMacMode{ 0 };
};

/**
 * @brief Encodes the Ranging Session Setup M3 message.
 *
 * Attribute order: RAN Multiplier, Number Chaps per Slot, Number Responders Nodes, Number Slots per
 * Round, SYNC Code Index Bitmask, Hopping Configuration Bitmask, MAC Mode.
 *
 * @return Number of bytes written, or 0 if the buffer is too small.
 */
size_t BuildM3(const M3Params &params, uint8_t *buffer, size_t bufferSize);

/**
 * @brief Encodes the Ranging Session Setup M1 message.
 *
 * Attribute order: UWB Configuration Identifier, Pulse Shape Combo, Channel Bitmask,
 * UWB Session Identifier.
 *
 * @param params Values to encode.
 * @param buffer Output buffer.
 * @param bufferSize Size of @p buffer in bytes (kM1MaxLength is always sufficient).
 *
 * @return Number of bytes written, or 0 if the parameters are invalid or the buffer is too small.
 */
size_t BuildM1(const M1Params &params, uint8_t *buffer, size_t bufferSize);

/** Parsed message header. The payload points into the buffer given to ParseHeader(). */
struct Header {
	Protocol mProtocol{ Protocol::Ap };
	uint8_t mMessageId{ 0 };
	const uint8_t *mPayload{ nullptr };
	size_t mPayloadLength{ 0 };
};

/**
 * @brief Parses and validates the header of a message received over BLE.
 *
 * @retval 0 Success.
 * @retval -EINVAL The message is shorter than a header or its length does not match the payload length.
 */
int ParseHeader(const uint8_t *data, size_t length, Header &header);

/**
 * @brief Parses the payload of Ranging Session Setup M2 into @p config.
 *
 * Only the M2 fields of @p config are written, and only if the whole message is valid.
 *
 * @param expectedSessionId The UWB session identifier of the session; checked if M2 carries one.
 *
 * @retval 0 Success.
 * @retval -EINVAL Malformed attribute, session identifier mismatch or a mandatory attribute is missing.
 */
int ParseM2(const uint8_t *payload, size_t payloadLength, uint32_t expectedSessionId, AliroSessionConfig &config);

/**
 * @brief Parses the payload of Ranging Session Setup M4 into @p config.
 *
 * Writes the M4 fields of @p config (STS Index0, UWB Time0, Hop Mode Key, SYNC Code Index), only if the whole
 * message is valid.
 *
 * @param expectedSessionId The UWB session identifier of the session; checked if M4 carries one.
 *
 * @retval 0 Success.
 * @retval -EINVAL Malformed attribute, session identifier mismatch or a mandatory attribute is missing.
 */
int ParseM4(const uint8_t *payload, size_t payloadLength, uint32_t expectedSessionId, AliroSessionConfig &config);

/**
 * @brief Parses the payload of a Ranging Session Suspend Request.
 *
 * The request must carry the UWB Session Identifier of the session.
 *
 * @param expectedSessionId The UWB session identifier of the session.
 *
 * @retval 0 Success.
 * @retval -EINVAL Malformed attribute, session identifier mismatch or missing.
 */
int ParseSuspendRequest(const uint8_t *payload, size_t payloadLength, uint32_t expectedSessionId);

/** Payload length of a Suspend Response message: the Status attribute. */
constexpr size_t kSuspendResponsePayloadLength{ AttributeSize(kU8ValueLength) };

/** Size of an encoded Suspend Response message. */
constexpr size_t kSuspendResponseLength{ kHeaderLength + kSuspendResponsePayloadLength };

/**
 * @brief Encodes the Ranging Session Suspend Response message.
 *
 * @param status Whether the suspend request is accepted.
 * @param buffer Output buffer.
 * @param bufferSize Size of @p buffer in bytes (kSuspendResponseLength is always sufficient).
 *
 * @return Number of bytes written, or 0 if the buffer is too small.
 */
size_t BuildSuspendResponse(RangingServiceStatus status, uint8_t *buffer, size_t bufferSize);

/**
 * @brief Parses the attributes of a Ranging notification.
 *
 * @param[out] attributeMask Bit n is set when NotificationMessageRangingAttribute n is present.
 *
 * @retval 0 Success.
 * @retval -EINVAL Malformed attribute list.
 */
int ParseRangingNotification(const uint8_t *payload, size_t payloadLength, uint32_t &attributeMask);

} // namespace Aliro::Uwb::Message
