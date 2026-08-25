/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include "aliro/errors.h"
#include "aliro/types.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include <array>
#include <cstdint>

namespace Aliro {

/**
 * @class NfcTransportNxpNfcRdLib
 * @brief NFC transport implementation using the NXP NFC Reader Library.
 */
class NfcTransportNxpNfcRdLib {
public:
	static constexpr size_t kIsodepApduMaxLen = 512;

	/**
	 * @brief Gets the singleton instance.
	 * @return Reference to the singleton instance.
	 */
	static NfcTransportNxpNfcRdLib &Instance()
	{
		static NfcTransportNxpNfcRdLib sInstance;
		return sInstance;
	}

	/**
	 * @brief Initializes the NFC transport.
	 * @return ALIRO_NO_ERROR on success, error code otherwise.
	 */
	AliroError Init();

	/**
	 * @brief Starts NFC polling/discovery.
	 * @return ALIRO_NO_ERROR on success, error code otherwise.
	 */
	AliroError Start();

	/**
	 * @brief Stops NFC polling.
	 * @return ALIRO_NO_ERROR on success, error code otherwise.
	 */
	AliroError Stop();

	/**
	 * @brief Sends data to the detected NFC card.
	 *
	 * @param data The data to send.
	 * @return ALIRO_NO_ERROR on success, error code otherwise.
	 */
	AliroError Send(Data data);

	/**
	 * @brief Terminates the current NFC session and restarts polling.
	 *
	 * @return ALIRO_NO_ERROR on success, error code otherwise.
	 */
	AliroError Terminate();

	/**
	 * @brief Configure ISO-DEP and create an Aliro session for a Type 4A tag.
	 *
	 * Called from the discovery thread when a Type 4A tag is activated.
	 */
	void OnType4AActivated(void *discLoopParams);

	/** @return true while an ISO-DEP Aliro session is active. */
	bool IsIsodepSessionActive() const;

	/**
	 * @brief Run ISO-DEP exchanges until the session ends.
	 *
	 * Must be called from the NFC discovery thread.
	 */
	void RunIsodepSession();

	/**
	 * @brief Tear down ISO-DEP and prepare for the next discovery cycle.
	 */
	void CleanupIsodepSession();

private:
	NfcTransportNxpNfcRdLib() = default;
	NfcTransportNxpNfcRdLib(const NfcTransportNxpNfcRdLib &) = delete;
	NfcTransportNxpNfcRdLib(NfcTransportNxpNfcRdLib &&) = delete;
	~NfcTransportNxpNfcRdLib() = default;
	NfcTransportNxpNfcRdLib &operator=(const NfcTransportNxpNfcRdLib &) = delete;
	NfcTransportNxpNfcRdLib &operator=(NfcTransportNxpNfcRdLib &&) = delete;

	void CaptureRxData(uint16_t currentDataLen);
	void RequestSessionTermination();

	std::array<uint8_t, kIsodepApduMaxLen> mRxBuffer{};
	std::array<uint8_t, kIsodepApduMaxLen> mTxBuffer{};
	uint16_t mTxLen{ 0 };

	struct k_mutex mMutex{};
	struct k_sem mTxSem{};

	atomic_t mStarted{ false };            /* Discovery polling started via Start(). */
	atomic_t mSessionActive{ false };      /* Type 4A ISO-DEP session active after activation. */
	atomic_t mPendingSend{ false };        /* TX APDU queued, awaiting discovery-thread exchange. */
	atomic_t mTerminateRequested{ false }; /* Stop() or Terminate() requested; ends session loop. */
};

} // namespace Aliro
