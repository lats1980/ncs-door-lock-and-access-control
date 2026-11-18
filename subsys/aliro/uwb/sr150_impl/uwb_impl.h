/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include "session_context.h"
#include "uwb.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/slist.h>

extern "C" {
#include <UwbApi.h>
#include <UwbApi_Types.h>
}

#include <cstddef>
#include <errno.h>

namespace Aliro::Uwb {

/**
 * @class UltraWideBandImpl
 * @brief NXP SR150 implementation of the UltraWideBand interface.
 *
 * This class implements the UltraWideBand interface on top of the NXP UWB IoT API (UwbApi) for the SR150:
 * initialization, Aliro ranging session setup (M1..M4) and ranging over an Aliro (CSA) UWB session, and
 * reporting of the ranging results to the Aliro stack.
 */
class UltraWideBandImpl final : public UltraWideBand {
private:
	friend class UltraWideBand;
	friend UltraWideBand &UltraWideBandInstance();
	friend UltraWideBandImpl &UltraWideBandInstanceImpl();

	/**
	 * @brief Gets the instance of the UltraWideBand implementation.
	 *
	 * @return The instance of the UltraWideBand implementation.
	 */
	static UltraWideBandImpl &Instance()
	{
		static UltraWideBandImpl sInstance;
		return sInstance;
	}

	int _Init(const Callbacks &callbacks);
	int _Deinit();
	void _BleTimeSync();
	int _HandleBleMessage(const uint8_t *data, size_t length, SessionContextHandle sessionContextData);
	int _ConfigureRangingSession(SessionIdentifier sessionId, const CryptoTypes::Ursk &ursk,
				     ProtocolVersion protocolVersion, SessionContextHandle sessionContextHandle);
	int _InitiateRangingSession(SessionContextHandle sessionContextData);
	int _TerminateRangingSession(SessionContextHandle sessionContextData);
	int _SuspendRangingSession(SessionContextHandle sessionContextData);
	int _ResumeRangingSession(SessionContextHandle sessionContextData);
	int _StartRadarSession() { return -ENOSYS; }
	void _StopRadarSession() {}
	const char *_GetFirmwareVersion() { return nullptr; }
	bool _IsInitialized() { return mInitialized; }

	SessionContext *FindSession(SessionContextHandle sessionContextData);
	SessionContext *FindSessionByNxpHandle(uint32_t sessionHandle);
	int AddSession(SessionContext *sessionCtx);
	void RemoveSession(SessionContext *sessionCtx);
	void RemoveAllSessions();
	void DestroySession(SessionContext *sessionCtx);

	/**
	 * @brief Reads UWBS capabilities (UwbApi_GetDeviceCapability) into mDevCap.
	 *
	 * @return 0 on success, negative error code otherwise.
	 */
	int GetDeviceCapabilities();

	/** @brief SR150 notification callback; runs in the UWB stack context. */
	static void RangingCallback(eNotificationType type, void *data);

	/**
	 * @brief Logs a ranging block and, if it holds a valid distance, reports it to the Aliro stack.
	 */
	void HandleAliroRangingData(const phAliroRangingData_t &data);

	/* Aliro UWB setup message handling (uwb_impl_setup.cpp). */
	int HandleRangingServiceMessage(uint8_t messageId, const uint8_t *payload, size_t payloadLength,
					SessionContext &sessionCtx);
	int HandleNotificationMessage(uint8_t messageId, const uint8_t *payload, size_t payloadLength,
				      SessionContext &sessionCtx);
	int HandleRangingNotification(const uint8_t *payload, size_t payloadLength, SessionContext &sessionCtx);
	int HandleM2(const uint8_t *payload, size_t payloadLength, SessionContext &sessionCtx);
	int HandleM4(const uint8_t *payload, size_t payloadLength, SessionContext &sessionCtx);

	/**
	 * @brief Handles a Ranging Session Suspend Request: stops ranging and answers with a Suspend Response.
	 *
	 * The SR150 session stays configured so that ranging can be resumed later.
	 */
	int HandleSuspendRequest(const uint8_t *payload, size_t payloadLength, SessionContext &sessionCtx);

	/**
	 * @brief Creates and configures the SR150 CCC session from the negotiated parameters and URSK.
	 *
	 * Ranging is not started; see StartUwbRanging(). On failure the SR150 session is deinitialized again.
	 */
	int SetupUwbSession(SessionContext &sessionCtx);

	/** @brief Starts ranging on a session configured with SetupUwbSession(). */
	int StartUwbRanging(SessionContext &sessionCtx);

	/** @brief Stops ranging (if active) and deinitializes the SR150 session of @p sessionCtx. */
	void StopRanging(SessionContext &sessionCtx);

	/**
	 * @brief Selects the M3 parameters from the M2 values in @p config and the UWBS capabilities.
	 *
	 * @return 0 on success, -ENOTSUP if there is no compatible slot or hopping configuration.
	 */
	int SelectSessionParameters(AliroSessionConfig &config) const;

	/** @brief Logs the cached CCC capabilities. */
	void PrintCccCapabilities() const;

	UltraWideBandImpl() = default;
	~UltraWideBandImpl() = default;

	UltraWideBandImpl(const UltraWideBandImpl &) = delete;
	UltraWideBandImpl &operator=(const UltraWideBandImpl &) = delete;
	UltraWideBandImpl(UltraWideBandImpl &&) = delete;
	UltraWideBandImpl &operator=(UltraWideBandImpl &&) = delete;

	Callbacks mCallbacks{};
	sys_slist_t mActiveSessionsList{};
	k_mutex mMutex{};
	/** UWBS capabilities (CCC fields are used to build the M1 message). */
	phUwbCapInfo_t mDevCap{};
	bool mDevCapValid{ false };
	bool mInitialized = false;
};

/**
 * @brief Get the singleton instance of the UltraWideBand interface.
 *
 * @return Reference to the UltraWideBand instance.
 */
inline UltraWideBand &UltraWideBandInstance()
{
	return UltraWideBandImpl::Instance();
}

/**
 * @brief Get the singleton instance of the UltraWideBand implementation.
 *
 * @return Reference to the UltraWideBandImpl instance.
 */
inline UltraWideBandImpl &UltraWideBandInstanceImpl()
{
	return UltraWideBandImpl::Instance();
}

} // namespace Aliro::Uwb
