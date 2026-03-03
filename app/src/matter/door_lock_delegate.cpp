/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "door_lock_delegate.h"

#include "lib/support/CodeUtils.h"
#include <algorithm>
#include <aliro/aliro.h>
#include <aliro/init.h>
#include <aliro/interface.h>
#include <platform/CHIPDeviceLayer.h>

#if defined(CONFIG_ALIRO_AT_HOST)
#include "at_command/at_host.h"
#else
#include "reader.h"
#endif

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace chip::app::Clusters::DoorLock;

namespace {
static_assert(sizeof(Aliro::CryptoTypes::PrivateKey) == kAliroSigningKeySize,
	      "Aliro::CryptoTypes::PrivateKey size mismatch");
static_assert(sizeof(Aliro::CryptoTypes::PublicKey) == kAliroReaderVerificationKeySize,
	      "Aliro::CryptoTypes::PublicKey size mismatch");
static_assert(sizeof(Aliro::Identifier) == kAliroReaderGroupIdentifierSize + kAliroReaderGroupSubIdentifierSize,
	      "Aliro::Identifier size mismatch");
static_assert(sizeof(Aliro::CryptoTypes::GroupResolvingKey) == kAliroGroupResolvingKeySize,
	      "Aliro::CryptoTypes::GroupResolvingKey size mismatch");
static_assert(sizeof(Aliro::ProtocolVersion) == kAliroProtocolVersionSize, "Aliro::ProtocolVersion size mismatch");

CHIP_ERROR EncodeProtocolVersion(size_t index, chip::MutableByteSpan &protocolVersion,
				 const Aliro::ProtocolVersion *versions, size_t versionCount)
{
	VerifyOrReturnError(versions != nullptr, CHIP_ERROR_INVALID_ARGUMENT, LOG_ERR("Versions list is nullptr"));

	if (index > versionCount - 1) {
		protocolVersion.reduce_size(0);
		return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
	}

	if (protocolVersion.size() != kAliroProtocolVersionSize) {
		protocolVersion.reduce_size(0);
		return CHIP_ERROR_INVALID_ARGUMENT;
	}

	// Per Aliro spec, protocol version encoding is big-endian
	chip::Encoding::BigEndian::Put16(protocolVersion.data(), versions[index]);

	return CHIP_NO_ERROR;
}
} // namespace

CHIP_ERROR DoorLockDelegate::Init()
{
	CHIP_ERROR err = chip::DeviceLayer::SystemLayer().ScheduleLambda([]() {
	LOG_INF("DoorLockDelegate initialization");
#if defined(CONFIG_ALIRO_AT_HOST)
		if (at_reader_private_key_is_set()) {
			LOG_INF("Reader private key is set, starting Aliro");
			if (!at_reader_start()) {
				LOG_ERR("Failed to start reader");
			}
		} else {
			LOG_INF("Reader private key is NOT set, skipping Aliro start");
		}
#else
		const auto rc = DoorLock::Storage::Reader::Init();
		VerifyOrReturn(rc == ALIRO_NO_ERROR, LOG_ERR("Failed to load Reader data"));

		VerifyOrReturn(DoorLock::Storage::Reader::IsPrivateKeySet(), /* device not provisioned */);

		int err = AliroStart();
		if (err != EXIT_SUCCESS) {
			LOG_ERR("Failed to start Aliro");
		}
#endif
	});
	VerifyOrReturnError(err == CHIP_NO_ERROR, err, LOG_ERR("Failed to schedule lambda"));

	return CHIP_NO_ERROR;
}

CHIP_ERROR DoorLockDelegate::GetAliroReaderVerificationKey(chip::MutableByteSpan &verificationKey)
{
	LOG_INF("GetAliroReaderVerificationKey");
	VerifyOrReturnError(verificationKey.size() == kAliroReaderVerificationKeySize, CHIP_ERROR_INVALID_ARGUMENT);
#if defined(CONFIG_ALIRO_AT_HOST)
	if (!at_reader_private_key_is_set()) {
		verificationKey.reduce_size(0);
		// We have to return CHIP_NO_ERROR here.
		return CHIP_NO_ERROR;
	}
	if (at_reader_public_key_get(verificationKey.data(), verificationKey.size()) != 0) {
		verificationKey.reduce_size(0);
		return CHIP_ERROR_INTERNAL;
	}
	LOG_HEXDUMP_INF(verificationKey.data(), verificationKey.size(), "Reader Verification Key");
#else
	if (!DoorLock::Storage::Reader::IsPrivateKeySet()) {
		verificationKey.reduce_size(0);
		// We have to return CHIP_NO_ERROR here.
		return CHIP_NO_ERROR;
	}

	Aliro::CryptoTypes::PublicKey publicKey{};
	const auto status = DoorLock::Storage::Reader::GetPublicKey(publicKey);
	if (status != ALIRO_NO_ERROR) {
		verificationKey.reduce_size(0);
		return CHIP_ERROR_INTERNAL;
	}

	std::copy_n(publicKey.begin(), kAliroReaderVerificationKeySize, verificationKey.data());
	LOG_HEXDUMP_INF(verificationKey.data(), verificationKey.size(), "Reader Verification Key");
#endif
	return CHIP_NO_ERROR;
}

CHIP_ERROR DoorLockDelegate::GetAliroReaderGroupIdentifier(chip::MutableByteSpan &groupIdentifier)
{
	LOG_INF("GetAliroReaderGroupIdentifier");
	VerifyOrReturnError(groupIdentifier.size() == kAliroReaderGroupIdentifierSize, CHIP_ERROR_INVALID_ARGUMENT);
#if defined(CONFIG_ALIRO_AT_HOST)
	Aliro::Identifier identifier{};
	if (!at_reader_identifier_get(identifier.data(), identifier.size())) {
		groupIdentifier.reduce_size(0);
		// We have to return CHIP_NO_ERROR here.
		return CHIP_NO_ERROR;
	}
	std::copy_n(identifier.data(), kAliroReaderGroupIdentifierSize, groupIdentifier.data());
	LOG_HEXDUMP_INF(groupIdentifier.data(), groupIdentifier.size(), "Reader Group Identifier");
#else

	if (!DoorLock::Storage::Reader::IsIdentifierSet()) {
		groupIdentifier.reduce_size(0);
		// We have to return CHIP_NO_ERROR here.
		return CHIP_NO_ERROR;
	}

	Aliro::Identifier identifier{};
	const auto status = DoorLock::Storage::Reader::GetIdentifier(identifier);
	if (status != ALIRO_NO_ERROR) {
		groupIdentifier.reduce_size(0);
		return CHIP_ERROR_INTERNAL;
	}

	std::copy_n(identifier.data(), kAliroReaderGroupIdentifierSize, groupIdentifier.data());
#endif
	return CHIP_NO_ERROR;
}

CHIP_ERROR DoorLockDelegate::GetAliroReaderGroupSubIdentifier(chip::MutableByteSpan &groupSubIdentifier)
{
	LOG_INF("GetAliroReaderGroupSubIdentifier");
	VerifyOrReturnError(groupSubIdentifier.size() == kAliroReaderGroupSubIdentifierSize,
			    CHIP_ERROR_INVALID_ARGUMENT);
#if defined(CONFIG_ALIRO_AT_HOST)
	Aliro::Identifier identifier{};
	if (!at_reader_identifier_get(identifier.data(), identifier.size())) {
		groupSubIdentifier.reduce_size(0);
		// We have to return CHIP_NO_ERROR here.
		return CHIP_NO_ERROR;
	}
	std::copy_n(identifier.data() + kAliroReaderGroupIdentifierSize, kAliroReaderGroupSubIdentifierSize,
		    groupSubIdentifier.data());
#else
	if (!DoorLock::Storage::Reader::IsIdentifierSet()) {
		groupSubIdentifier.reduce_size(0);
		// We have to return CHIP_NO_ERROR here.
		return CHIP_NO_ERROR;
	}

	Aliro::Identifier identifier{};
	const auto status = DoorLock::Storage::Reader::GetIdentifier(identifier);
	if (status != ALIRO_NO_ERROR) {
		groupSubIdentifier.reduce_size(0);
		return CHIP_ERROR_INTERNAL;
	}

	std::copy_n(identifier.data() + kAliroReaderGroupIdentifierSize, kAliroReaderGroupSubIdentifierSize,
		    groupSubIdentifier.data());
#endif
	return CHIP_NO_ERROR;
}

CHIP_ERROR
DoorLockDelegate::GetAliroExpeditedTransactionSupportedProtocolVersionAtIndex(size_t index,
									      chip::MutableByteSpan &protocolVersion)
{
	LOG_INF("GetAliroExpeditedTransactionSupportedProtocolVersionAtIndex");
#if defined(CONFIG_ALIRO_AT_HOST)
	uint8_t versions_buf[64];
	size_t version_count = sizeof(versions_buf) / 2;

	if (at_aliro_expedited_version_get(versions_buf, sizeof(versions_buf), &version_count) != 0) {
		protocolVersion.reduce_size(0);
		return CHIP_ERROR_INTERNAL;
	}
	return EncodeProtocolVersion(index, protocolVersion,
				    reinterpret_cast<const Aliro::ProtocolVersion *>(versions_buf),
				    version_count);
#else
	size_t versionCount{};
	const auto *versions = Aliro::AliroStack::Instance().GetExpeditedStandardProtocolVersions(versionCount);
	LOG_HEXDUMP_INF(versions, versionCount * sizeof(Aliro::ProtocolVersion), "Expedited Transaction Supported Protocol Versions");
	return EncodeProtocolVersion(index, protocolVersion, versions, versionCount);
#endif
}

CHIP_ERROR DoorLockDelegate::GetAliroGroupResolvingKey(chip::MutableByteSpan &groupResolvingKey)
{
	LOG_INF("GetAliroGroupResolvingKey");

	VerifyOrReturnError(groupResolvingKey.size() == kAliroGroupResolvingKeySize, CHIP_ERROR_INVALID_ARGUMENT);

#ifdef CONFIG_DOOR_LOCK_BLE_UWB
#if defined(CONFIG_ALIRO_AT_HOST)
#else
	if (!DoorLock::Storage::Reader::IsGroupResolvingKeySet()) {
		groupResolvingKey.reduce_size(0);
		return CHIP_ERROR_NOT_FOUND;
	}

	Aliro::CryptoTypes::GroupResolvingKey key{};
	const auto status = DoorLock::Storage::Reader::GetGroupResolvingKey(key);
	if (status != ALIRO_NO_ERROR) {
		groupResolvingKey.reduce_size(0);
		return CHIP_ERROR_INTERNAL;
	}
	std::copy_n(key.data(), key.size(), groupResolvingKey.data());

	return CHIP_NO_ERROR;
#endif // CONFIG_ALIRO_AT_HOST
#else // CONFIG_DOOR_LOCK_BLE_UWB

	groupResolvingKey.reduce_size(0);
	return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;

#endif // CONFIG_DOOR_LOCK_BLE_UWB
}

CHIP_ERROR DoorLockDelegate::GetAliroSupportedBLEUWBProtocolVersionAtIndex(size_t index,
									   chip::MutableByteSpan &protocolVersion)
{
	LOG_INF("GetAliroSupportedBLEUWBProtocolVersionAtIndex");

#if CONFIG_DOOR_LOCK_BLE_UWB
#if defined(CONFIG_ALIRO_AT_HOST)
#else
	size_t versionCount{};
	const auto *versions = Aliro::AliroStack::Instance().GetBleUwbProtocolVersions(versionCount);

	return EncodeProtocolVersion(index, protocolVersion, versions, versionCount);
#endif // CONFIG_ALIRO_AT_HOST
#else // CONFIG_DOOR_LOCK_BLE_UWB

	protocolVersion.reduce_size(0);
	return CHIP_ERROR_NOT_IMPLEMENTED;

#endif // CONFIG_DOOR_LOCK_BLE_UWB
}

uint8_t DoorLockDelegate::GetAliroBLEAdvertisingVersion()
{
	LOG_INF("GetAliroBLEAdvertisingVersion");

#ifdef CONFIG_DOOR_LOCK_BLE_UWB
#if defined(CONFIG_ALIRO_AT_HOST)
#else
	return Aliro::AliroStack::Instance().GetBleAdvertisingVersion();
#endif
#else // CONFIG_DOOR_LOCK_BLE_UWB

	return 0;

#endif // CONFIG_DOOR_LOCK_BLE_UWB
}

uint16_t DoorLockDelegate::GetNumberOfAliroCredentialIssuerKeysSupported()
{
	LOG_INF("GetNumberOfAliroCredentialIssuerKeysSupported");

	return CONFIG_DOOR_LOCK_ACCESS_MANAGER_CREDENTIAL_ISSUER_MAX_STORED_KEYS;
}

uint16_t DoorLockDelegate::GetNumberOfAliroEndpointKeysSupported()
{
	LOG_INF("GetNumberOfAliroEndpointKeysSupported");

	return CONFIG_DOOR_LOCK_ACCESS_MANAGER_ACCESS_CREDENTIAL_MAX_STORED_KEYS;
}

CHIP_ERROR DoorLockDelegate::SetAliroReaderConfig(const chip::ByteSpan &signingKey,
						  const chip::ByteSpan &verificationKey,
						  const chip::ByteSpan &groupIdentifier,
						  const chip::Optional<chip::ByteSpan> &groupResolvingKey)
{
	LOG_INF("SetAliroReaderConfig");
	VerifyOrReturnError(signingKey.size() == kAliroSigningKeySize, CHIP_ERROR_INVALID_ARGUMENT);
	VerifyOrReturnError(verificationKey.size() == kAliroReaderVerificationKeySize, CHIP_ERROR_INVALID_ARGUMENT);
	VerifyOrReturnError(groupIdentifier.size() == kAliroReaderGroupIdentifierSize, CHIP_ERROR_INVALID_ARGUMENT);
#ifdef CONFIG_DOOR_LOCK_BLE_UWB
	VerifyOrReturnError(groupResolvingKey.HasValue() &&
				    groupResolvingKey.Value().size() == kAliroGroupResolvingKeySize,
			    CHIP_ERROR_INVALID_ARGUMENT);

#endif // CONFIG_DOOR_LOCK_BLE_UWB

#if defined(CONFIG_ALIRO_AT_HOST)
	if (at_reader_identifier_create(groupIdentifier.data(), groupIdentifier.size()) != 0) {
		LOG_ERR("Failed to create reader identifier");
		return CHIP_ERROR_INTERNAL;
	}
#ifdef CONFIG_DOOR_LOCK_BLE_UWB
	//TODO: set group resolving key via AT command
#endif
	if (!at_reader_private_key_set(signingKey.data(), signingKey.size())) {
		LOG_ERR("Failed to set reader private key");
//TODO: clear group identifier as well?
/*
		if(!at_reader_identifier_clear()) {
			LOG_ERR("Failed to clear reader identifier after private key set failure");
		}
*/
		return CHIP_ERROR_INTERNAL;
	}
	if (!at_reader_start()) {
		LOG_ERR("Failed to start reader");
		if(!at_reader_private_key_set(nullptr, 0)) {
			LOG_ERR("Failed to clear reader private key after reader start failure");
		}
	//TODO: clear group identifier as well?
	/*
		if(!at_reader_identifier_clear()) {
			LOG_ERR("Failed to clear reader identifier after reader start failure");
		}
	*/
		return CHIP_ERROR_INTERNAL;
	}
#else // CONFIG_ALIRO_AT_HOST

	Aliro::CryptoTypes::PrivateKey privateKey{};
	Aliro::Identifier identifier{};
	Aliro::CryptoTypes::GroupResolvingKey groupResKey{};

	std::copy(signingKey.begin(), signingKey.end(), privateKey.data());
	std::copy_n(groupIdentifier.data(), kAliroReaderGroupIdentifierSize, identifier.data());

	AliroError err = Aliro::Interface::Crypto::GenerateRandom(identifier.data() + kAliroReaderGroupIdentifierSize,
								  kAliroReaderGroupSubIdentifierSize);
	VerifyOrReturnError(err == ALIRO_NO_ERROR, CHIP_ERROR_INTERNAL);

	VerifyOrReturnError(DoorLock::Storage::Reader::SetIdentifier(identifier) == ALIRO_NO_ERROR,
			    CHIP_ERROR_INTERNAL);

	if (groupResolvingKey.HasValue()) {
		std::copy(groupResolvingKey.Value().begin(), groupResolvingKey.Value().end(), groupResKey.data());
	}

	VerifyOrReturnError(DoorLock::Storage::Reader::SetPrivateKey(privateKey) == ALIRO_NO_ERROR,
			    CHIP_ERROR_INTERNAL);

#ifdef CONFIG_DOOR_LOCK_BLE_UWB

	VerifyOrReturnError(DoorLock::Storage::Reader::SetGroupResolvingKey(groupResKey) == ALIRO_NO_ERROR,
			    CHIP_ERROR_INTERNAL);

#endif // CONFIG_DOOR_LOCK_BLE_UWB

	VerifyOrReturnError(AliroStart() == EXIT_SUCCESS, CHIP_ERROR_INTERNAL, LOG_ERR("Failed to start Aliro"););
#endif
	return CHIP_NO_ERROR;
}

CHIP_ERROR DoorLockDelegate::ClearAliroReaderConfig()
{
	LOG_INF("ClearAliroReaderConfig");
#if defined(CONFIG_ALIRO_AT_HOST)
#else
	VerifyOrReturnError(AliroStop() == EXIT_SUCCESS, CHIP_ERROR_INTERNAL, LOG_ERR("Failed to stop Aliro"));

	VerifyOrReturnError(DoorLock::Storage::Reader::ClearAll() == ALIRO_NO_ERROR, CHIP_ERROR_INTERNAL);
#endif
	return CHIP_NO_ERROR;
}
