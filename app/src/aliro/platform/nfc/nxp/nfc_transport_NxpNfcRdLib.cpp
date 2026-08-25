/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "nfc_transport_NxpNfcRdLib.h"
#include "nxp_nfc_discovery/nxp_nfc_isodep.h"
#include "nxp_nfc_discovery/nxp_nfc_platform.h"

extern "C" {
#include <phNfcLib.h>
#include <ph_Status.h>
#include <phacDiscLoop.h>
#include <phpalI14443p4.h>
#include <phpalI14443p4a.h>
}

#ifdef bool
#undef bool
#endif

#include "aliro/aliro.h"
#include "aliro/utils.h"

#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(nfc_st_NxpNfcRdLib_impl, CONFIG_DOOR_LOCK_NXPNFCRDLIB_LOG_LEVEL);

namespace {

constexpr uint8_t kSelResponseBitmaskB6B5 = 0x60U;

phpalI14443p4_Sw_DataParams_t *GetPalI14443p4()
{
	return static_cast<phpalI14443p4_Sw_DataParams_t *>(phNfcLib_GetDataParams(PH_COMP_PAL_ISO14443P4));
}

phpalI14443p4a_Sw_DataParams_t *GetPalI14443p4a()
{
	return static_cast<phpalI14443p4a_Sw_DataParams_t *>(phNfcLib_GetDataParams(PH_COMP_PAL_ISO14443P4A));
}

bool IsType4ADetected(phacDiscLoop_Sw_DataParams_t *discLoopParams)
{
	const uint8_t selResponse = discLoopParams->sTypeATargetInfo.aTypeA_I3P3[0].aSak;
	const uint8_t selResponseB6B5 = (selResponse & kSelResponseBitmaskB6B5) >> 5U;

	if ((selResponse & (1U << 2U)) != 0) {
		return false;
	}

	return (selResponseB6B5 == PHAC_DISCLOOP_TYPEA_TYPE4A_TAG_CONFIG_MASK) ||
	       (selResponseB6B5 == PHAC_DISCLOOP_TYPEA_TYPE_NFC_DEP_TYPE4A_TAG_CONFIG_MASK);
}

phStatus_t ConfigureIsodepProtocol(phacDiscLoop_Sw_DataParams_t *discLoopParams)
{
	phpalI14443p4_Sw_DataParams_t *palI14443p4 = GetPalI14443p4();
	phpalI14443p4a_Sw_DataParams_t *palI14443p4a = GetPalI14443p4a();
	phStatus_t status;
	uint8_t cidEnable;
	uint8_t cid;
	uint8_t nadSupported;
	uint8_t fwi;
	uint8_t fsdi;
	uint8_t fsci;
	const uint8_t selResponse = discLoopParams->sTypeATargetInfo.aTypeA_I3P3[0].aSak;
	const uint8_t selResponseB6B5 = (selResponse & kSelResponseBitmaskB6B5) >> 5U;

	status = phpalI14443p4a_GetProtocolParams(palI14443p4a, &cidEnable, &cid, &nadSupported, &fwi, &fsdi,
						  &fsci);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("NxpNfcRdLib: GetProtocolParams failed: 0x%04x", status);
		return status;
	}

	status = phpalI14443p4_SetProtocol(palI14443p4, 0, 0, 0, 0, fwi, fsdi, fsci);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("NxpNfcRdLib: SetProtocol failed: 0x%04x", status);
		return status;
	}

	status = phpalI14443p4_SetConfig(palI14443p4, PHPAL_I14443P4_CONFIG_MAXRETRYCOUNT, 2U);
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("NxpNfcRdLib: SetConfig MAXRETRYCOUNT failed: 0x%04x", status);
		return status;
	}

	if (selResponseB6B5 == PHAC_DISCLOOP_TYPEA_TYPE_NFC_DEP_TYPE4A_TAG_CONFIG_MASK) {
		uint8_t ratsResponse[64];

		status = phpalI14443p4a_Rats(palI14443p4a, fsdi, 0, ratsResponse);
		if (status != PH_ERR_SUCCESS) {
			LOG_ERR("NxpNfcRdLib: RATS failed: 0x%04x", status);
			return status;
		}
	}

	return PH_ERR_SUCCESS;
}

phStatus_t IsodepExchange(phpalI14443p4_Sw_DataParams_t *palI14443p4, const uint8_t *txData, uint16_t txLen,
			  uint8_t *rxBuffer, size_t rxBufferSize, uint16_t *rxLen)
{
	phStatus_t status;
	uint8_t *tempRxBuffer;
	uint16_t chainedBlockRxSize = 0;
	uint16_t totalRxSize = 0;

	*rxLen = 0;

	status = phpalI14443p4_Exchange(palI14443p4, PH_EXCHANGE_DEFAULT, const_cast<uint8_t *>(txData), txLen,
					&tempRxBuffer, &chainedBlockRxSize);

	while ((status == PH_ADD_COMPCODE(PH_ERR_SUCCESS_CHAINING, PH_COMP_PAL_ISO14443P4)) ||
	       (status == PH_ADD_COMPCODE(PH_ERR_SUCCESS, PH_COMP_PAL_ISO14443P4))) {
		if ((totalRxSize + chainedBlockRxSize) > rxBufferSize) {
			LOG_ERR("NxpNfcRdLib: ISO-DEP RX buffer overflow");
			return PH_ERR_BUFFER_OVERFLOW;
		}

		memcpy(rxBuffer + totalRxSize, tempRxBuffer, chainedBlockRxSize);
		totalRxSize += chainedBlockRxSize;

		if (status == PH_ADD_COMPCODE(PH_ERR_SUCCESS, PH_COMP_PAL_ISO14443P4)) {
			break;
		}

		status = phpalI14443p4_Exchange(palI14443p4, PH_EXCHANGE_RXCHAINING,
						 const_cast<uint8_t *>(txData), txLen, &tempRxBuffer,
						 &chainedBlockRxSize);
	}

	if (status == PH_ADD_COMPCODE(PH_ERR_SUCCESS, PH_COMP_PAL_ISO14443P4)) {
		*rxLen = totalRxSize;
	}

	return status;
}

} // namespace

extern "C" {

void nxp_nfc_isodep_on_activated(phacDiscLoop_Sw_DataParams_t *disc_loop_params)
{
	Aliro::NfcTransportNxpNfcRdLib::Instance().OnType4AActivated(disc_loop_params);
}

int nxp_nfc_isodep_session_active(void)
{
	return Aliro::NfcTransportNxpNfcRdLib::Instance().IsIsodepSessionActive() ? 1 : 0;
}

void nxp_nfc_isodep_run_session(phacDiscLoop_Sw_DataParams_t *disc_loop_params)
{
	ARG_UNUSED(disc_loop_params);
	Aliro::NfcTransportNxpNfcRdLib::Instance().RunIsodepSession();
}

void nxp_nfc_isodep_cleanup(phacDiscLoop_Sw_DataParams_t *disc_loop_params)
{
	ARG_UNUSED(disc_loop_params);
	Aliro::NfcTransportNxpNfcRdLib::Instance().CleanupIsodepSession();
}

} // extern "C"

namespace Aliro {

void NfcTransportNxpNfcRdLib::CaptureRxData(uint16_t currentDataLen)
{
	if (currentDataLen > 0) {
		LOG_HEXDUMP_DBG(mRxBuffer.data(), currentDataLen, "NxpNfcRdLib: RX data:");
		AliroStack::Instance().HandleSessionData(ConnectionHandle::Nfc(),
							 { .mData = mRxBuffer.data(),
							   .mLength = currentDataLen });
		return;
	}

	atomic_clear(&mSessionActive);
	AliroStack::Instance().DestroySession(ConnectionHandle::Nfc());
}

void NfcTransportNxpNfcRdLib::RequestSessionTermination()
{
	atomic_set(&mTerminateRequested, true);
	k_sem_give(&mTxSem);
}

void NfcTransportNxpNfcRdLib::OnType4AActivated(void *discLoopParams)
{
	auto *disc = static_cast<phacDiscLoop_Sw_DataParams_t *>(discLoopParams);

	if (!IsType4ADetected(disc)) {
		return;
	}

	if (ConfigureIsodepProtocol(disc) != PH_ERR_SUCCESS) {
		return;
	}

	LOG_HEXDUMP_DBG(disc->sTypeATargetInfo.aTypeA_I3P3[0].aUid,
			disc->sTypeATargetInfo.aTypeA_I3P3[0].bUidSize,
			"NxpNfcRdLib: NFC-A Passive ISO-DEP device found. UID:");

	atomic_clear(&mTerminateRequested);
	atomic_clear(&mPendingSend);
	atomic_set(&mSessionActive, true);

	AliroStack::Instance().CreateSession(ConnectionHandle::Nfc());
}

bool NfcTransportNxpNfcRdLib::IsIsodepSessionActive() const
{
	return atomic_get(&mSessionActive) != 0;
}

void NfcTransportNxpNfcRdLib::RunIsodepSession()
{
	std::array<uint8_t, kIsodepApduMaxLen> txBuffer{};
	uint16_t txLen = 0;
	phpalI14443p4_Sw_DataParams_t *palI14443p4 = GetPalI14443p4();

	while (atomic_get(&mSessionActive) && !atomic_get(&mTerminateRequested)) {
		if (k_sem_take(&mTxSem, K_MSEC(100)) != 0) {
			continue;
		}

		if (atomic_get(&mTerminateRequested)) {
			break;
		}

		if (!atomic_get(&mPendingSend)) {
			continue;
		}

		k_mutex_lock(&mMutex, K_FOREVER);
		txLen = mTxLen;
		memcpy(txBuffer.data(), mTxBuffer.data(), txLen);
		atomic_clear(&mPendingSend);
		k_mutex_unlock(&mMutex);

		LOG_HEXDUMP_DBG(txBuffer.data(), txLen, "NxpNfcRdLib: TX data:");
		memset(mRxBuffer.data(), 0, mRxBuffer.size());
		uint16_t rxLen = 0;
		const phStatus_t status =
			IsodepExchange(palI14443p4, txBuffer.data(), txLen, mRxBuffer.data(), mRxBuffer.size(), &rxLen);

		if (status != PH_ADD_COMPCODE(PH_ERR_SUCCESS, PH_COMP_PAL_ISO14443P4)) {
			LOG_ERR("NxpNfcRdLib: ISO-DEP exchange failed: 0x%04x", status);
			atomic_clear(&mSessionActive);
			AliroStack::Instance().DestroySession(ConnectionHandle::Nfc());
			break;
		}

		CaptureRxData(rxLen);
	}
}

void NfcTransportNxpNfcRdLib::CleanupIsodepSession()
{
	phpalI14443p4_Sw_DataParams_t *palI14443p4 = GetPalI14443p4();
	phStatus_t status;

	if (atomic_get(&mSessionActive)) {
		status = phpalI14443p4_Deselect(palI14443p4);
		if ((status & PH_ERR_MASK) != PH_ERR_SUCCESS) {
			LOG_WRN("NxpNfcRdLib: ISO-DEP deselect failed: 0x%04x", status);
		}
	}

	(void)phpalI14443p4_ResetProtocol(palI14443p4);

	atomic_clear(&mSessionActive);
	atomic_clear(&mPendingSend);
	atomic_clear(&mTerminateRequested);
}

/*
******************************************************************************
* Public API
******************************************************************************
*/

AliroError NfcTransportNxpNfcRdLib::Init()
{
	int err;

	err = nxp_nfc_init();
	VerifyOrReturnStatus(err == 0, ALIRO_ERROR_INTERNAL, LOG_ERR("NxpNfcRdLib: nxp_nfc_init failed %d", err));

	err = nxp_nfc_lib_init();
	VerifyOrReturnStatus(err == 0, ALIRO_ERROR_INTERNAL,
			     LOG_ERR("NxpNfcRdLib: nxp_nfc_lib_init failed %d", err));

	k_mutex_init(&mMutex);
	k_sem_init(&mTxSem, 0, 10);

	return ALIRO_NO_ERROR;
}

AliroError NfcTransportNxpNfcRdLib::Start()
{
	int err;

	if (atomic_get(&mStarted)) {
		return ALIRO_NO_ERROR;
	}

	err = nxp_nfc_discovery_start();
	VerifyOrReturnStatus(err == 0, ALIRO_ERROR_INTERNAL,
			     LOG_ERR("NxpNfcRdLib: nxp_nfc_discovery_start failed %d", err));

	atomic_set(&mStarted, true);

	return ALIRO_NO_ERROR;
}

AliroError NfcTransportNxpNfcRdLib::Stop()
{
	RequestSessionTermination();
	atomic_clear(&mStarted);

	return ALIRO_NO_ERROR;
}

AliroError NfcTransportNxpNfcRdLib::Send(Data data)
{
	VerifyOrReturnStatus(atomic_get(&mSessionActive), ALIRO_INVALID_STATE,
			     LOG_WRN("NxpNfcRdLib: NFC not activated, no data transfer possible"));
	VerifyOrReturnStatus(data.mLength <= mTxBuffer.size(), ALIRO_INVALID_ARGUMENT,
			     LOG_ERR("NxpNfcRdLib: TX data too large (%zu)", data.mLength));

	k_mutex_lock(&mMutex, K_FOREVER);
	memcpy(mTxBuffer.data(), data.mData, data.mLength);
	mTxLen = static_cast<uint16_t>(data.mLength);
	atomic_set(&mPendingSend, true);
	k_mutex_unlock(&mMutex);

	k_sem_give(&mTxSem);

	return ALIRO_NO_ERROR;
}

AliroError NfcTransportNxpNfcRdLib::Terminate()
{
	RequestSessionTermination();
	return ALIRO_NO_ERROR;
}

} // namespace Aliro
