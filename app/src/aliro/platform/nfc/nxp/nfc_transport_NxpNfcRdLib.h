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

namespace Aliro {

/**
 * @class NfcTransportNxpNfcRdLib
 * @brief NFC transport implementation using the NXP NFC Reader Library.
 */
class NfcTransportNxpNfcRdLib {
public:
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

private:
	NfcTransportNxpNfcRdLib() = default;
	NfcTransportNxpNfcRdLib(const NfcTransportNxpNfcRdLib &) = delete;
	NfcTransportNxpNfcRdLib(NfcTransportNxpNfcRdLib &&) = delete;
	~NfcTransportNxpNfcRdLib() = default;
	NfcTransportNxpNfcRdLib &operator=(const NfcTransportNxpNfcRdLib &) = delete;
	NfcTransportNxpNfcRdLib &operator=(NfcTransportNxpNfcRdLib &&) = delete;

	atomic_t mStarted{ false };
};

} // namespace Aliro
