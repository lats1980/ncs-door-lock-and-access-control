/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include "aliro/connection_handle.h"
#include "aliro/protocol_version.h"
#include "aliro/types.h"

#include <cstdint>

#include <zephyr/sys/slist.h>

namespace Aliro::Uwb {

using SessionContextHandle = Aliro::ConnectionHandle;

/**
 * @brief Progress of the Aliro UWB ranging session setup message exchange (M1..M4).
 *
 * Tracks the last setup message sent or received for a session.
 */
enum class SetupState : uint8_t {
	/** Session created, no setup message exchanged yet. */
	Created,
	/** M1 has been sent over BLE. */
	M1Sent,
	/** M2 has been received and parsed into SessionContext::mAliroConfig. */
	M2Received,
	/** M3 has been sent over BLE. */
	M3Sent,
	/** M4 has been received and the UWB ranging session has been started. */
	M4Received,
};

/**
 * @brief Aliro UWB session parameters negotiated over BLE (M1..M4).
 *
 */
struct AliroSessionConfig {
	/** M2: UWB Configuration Identifier selected by the User Device. */
	uint16_t mUwbConfigId{};
	/** M2: Pulse Shape Combo. */
	uint8_t mPulseShapeCombo{};
	/** M2: Channel Bitmask supported by the User Device. */
	uint8_t mChannelBitmask{};
	/** M2: SYNC Code Index Bitmask supported by the User Device. */
	uint32_t mSyncCodeIndexBitmask{};
	/** M2: RAN Multiplier (ranging block interval = multiplier * 96 ms). */
	uint8_t mRanMultiplier{};
	/** M2: Slot Bitmask (Number_Chaps_per_Slot) supported by the User Device. */
	uint8_t mSlotBitmask{};
	/** M2: Hopping Configuration Bitmask supported by the User Device. */
	uint8_t mHoppingConfigBitmask{};

	/** M3: selected RAN multiplier. */
	uint8_t mSelectedRanMultiplier{};
	/** M3: selected Number_Chaps_per_Slot. */
	uint8_t mSelectedChapsPerSlot{};
	/** M3: selected Number_Slots_per_Round. */
	uint8_t mSelectedSlotsPerRound{};
	/** M3: selected hopping configuration bitmask (wire format). */
	uint8_t mSelectedHoppingBitmask{};
	/** M3: MAC mode sent to the User Device. */
	uint8_t mMacMode{};

	/** M4: selected SYNC Code Index. */
	uint8_t mSyncCodeIndex{};
	/** M4: starting STS Index (STS Index0). */
	uint32_t mStsIndex{};
	/** M4: starting UWB time in microseconds (UWB Time0). */
	uint64_t mUwbTimeUs{};
	/** M4: Hop Mode Key. */
	uint8_t mHopModeKey[4]{};
};

/**
 * @brief UWB session context stored in the active-sessions list.
 */
struct SessionContext {
	SessionContext(uint32_t sessionId, SessionContextHandle sessionContextData,
		       const CryptoTypes::Ursk &ursk, ProtocolVersion protocolVersion)
		: mSessionId(sessionId), mSessionContextData(sessionContextData), mUrsk(ursk),
		  mProtocolVersion(protocolVersion)
	{
	}

	sys_snode_t mSessionContextNode{};
	uint32_t mSessionId{};
	SessionContextHandle mSessionContextData;
	RangingSessionState mRangingSessionState{ RangingSessionState::Uninitialized };
	SetupState mSetupState{ SetupState::Created };
	AliroSessionConfig mAliroConfig{};
	CryptoTypes::Ursk mUrsk{};
	ProtocolVersion mProtocolVersion{};

	/** NXP UCI session handle; valid after UwbApi_SessionInit(). */
	uint32_t mNxpSessionHandle{ 0 };
};

} // namespace Aliro::Uwb
