/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <door_lock_delegate/door_lock_delegate.h>

#include <aliro/aliro.h>
#ifdef CONFIG_ALIRO_AT_HOST
#include <aliro_at/at_host.h>
#else
#include <reader_storage/reader.h>
#endif

#include <lib/support/CodeUtils.h>

#include <psa/crypto.h>
#include <zephyr/logging/log.h>

#include <algorithm>

LOG_MODULE_REGISTER(door_lock_delegate, CONFIG_DOOR_LOCK_ALIRO_DOOR_LOCK_DELEGATE_LOG_LEVEL);

using namespace chip::app::Clusters::DoorLock;

namespace DoorLock {

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

CHIP_ERROR DoorLockDelegate::GetAliroReaderVerificationKey(chip::MutableByteSpan &verificationKey)
{
	LOG_DBG("GetAliroReaderVerificationKey");

	VerifyOrReturnError(verificationKey.size() == kAliroReaderVerificationKeySize, CHIP_ERROR_INVALID_ARGUMENT);
#ifdef CONFIG_ALIRO_AT_HOST
	if (!at_reader_private_key_is_set()) {
		verificationKey.reduce_size(0);
		return CHIP_NO_ERROR;
	}
	if (at_reader_public_key_get(verificationKey.data(), verificationKey.size()) != 0) {
		verificationKey.reduce_size(0);
		return CHIP_ERROR_INTERNAL;
	}
#else
	if (!DoorLock::ReaderStorage::IsPrivateKeySet()) {
		verificationKey.reduce_size(0);
		// We have to return CHIP_NO_ERROR here.
		return CHIP_NO_ERROR;
	}

	Aliro::CryptoTypes::PublicKey publicKey{};
	const auto status = DoorLock::ReaderStorage::GetPublicKey(publicKey);
	if (status != ALIRO_NO_ERROR) {
		verificationKey.reduce_size(0);
		return CHIP_ERROR_INTERNAL;
	}

	std::copy_n(publicKey.begin(), kAliroReaderVerificationKeySize, verificationKey.data());
#endif
	return CHIP_NO_ERROR;
}

CHIP_ERROR DoorLockDelegate::GetAliroReaderGroupIdentifier(chip::MutableByteSpan &groupIdentifier)
{
	LOG_DBG("GetAliroReaderGroupIdentifier");

	VerifyOrReturnError(groupIdentifier.size() == kAliroReaderGroupIdentifierSize, CHIP_ERROR_INVALID_ARGUMENT);
#ifdef CONFIG_ALIRO_AT_HOST
	Aliro::Identifier identifier{};
	if (at_reader_identifier_get(identifier.data(), identifier.size()) != 0) {
		groupIdentifier.reduce_size(0);
		return CHIP_NO_ERROR;
	}
	std::copy_n(identifier.data(), kAliroReaderGroupIdentifierSize, groupIdentifier.data());
#else
	if (!DoorLock::ReaderStorage::IsIdentifierSet()) {
		groupIdentifier.reduce_size(0);
		// We have to return CHIP_NO_ERROR here.
		return CHIP_NO_ERROR;
	}

	Aliro::Identifier identifier{};
	const auto status = DoorLock::ReaderStorage::GetIdentifier(identifier);
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
	LOG_DBG("GetAliroReaderGroupSubIdentifier");
	VerifyOrReturnError(groupSubIdentifier.size() == kAliroReaderGroupSubIdentifierSize,
			    CHIP_ERROR_INVALID_ARGUMENT);
#ifdef CONFIG_ALIRO_AT_HOST
	Aliro::Identifier identifier{};
	if (at_reader_identifier_get(identifier.data(), identifier.size()) != 0) {
		groupSubIdentifier.reduce_size(0);
		return CHIP_NO_ERROR;
	}
	std::copy_n(identifier.data() + kAliroReaderGroupIdentifierSize, kAliroReaderGroupSubIdentifierSize,
		    groupSubIdentifier.data());
#else

	if (!DoorLock::ReaderStorage::IsIdentifierSet()) {
		groupSubIdentifier.reduce_size(0);
		// We have to return CHIP_NO_ERROR here.
		return CHIP_NO_ERROR;
	}

	Aliro::Identifier identifier{};
	const auto status = DoorLock::ReaderStorage::GetIdentifier(identifier);
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
	LOG_DBG("GetAliroExpeditedTransactionSupportedProtocolVersionAtIndex");
#ifdef CONFIG_ALIRO_AT_HOST
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

	return EncodeProtocolVersion(index, protocolVersion, versions, versionCount);
#endif
}

CHIP_ERROR DoorLockDelegate::GetAliroGroupResolvingKey(chip::MutableByteSpan &groupResolvingKey)
{
	LOG_DBG("GetAliroGroupResolvingKey");

	VerifyOrReturnError(groupResolvingKey.size() == kAliroGroupResolvingKeySize, CHIP_ERROR_INVALID_ARGUMENT);

#ifdef CONFIG_DOOR_LOCK_ALIRO_DOOR_LOCK_DELEGATE_BLE_UWB_SUPPORT

#ifdef CONFIG_ALIRO_AT_HOST
	groupResolvingKey.reduce_size(0);
	return CHIP_NO_ERROR;
#else
	if (!DoorLock::ReaderStorage::IsGroupResolvingKeySet()) {
		groupResolvingKey.reduce_size(0);
		// We have to return CHIP_NO_ERROR here.
		return CHIP_NO_ERROR;
	}

	Aliro::CryptoTypes::GroupResolvingKey key{};
	const auto status = DoorLock::ReaderStorage::GetGroupResolvingKey(key);
	if (status != ALIRO_NO_ERROR) {
		groupResolvingKey.reduce_size(0);
		return CHIP_ERROR_INTERNAL;
	}
	std::copy_n(key.data(), key.size(), groupResolvingKey.data());

	return CHIP_NO_ERROR;
#endif // CONFIG_ALIRO_AT_HOST

#else // CONFIG_DOOR_LOCK_ALIRO_DOOR_LOCK_DELEGATE_BLE_UWB_SUPPORT

	groupResolvingKey.reduce_size(0);
	return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;

#endif // CONFIG_DOOR_LOCK_ALIRO_DOOR_LOCK_DELEGATE_BLE_UWB_SUPPORT
}

CHIP_ERROR DoorLockDelegate::GetAliroSupportedBLEUWBProtocolVersionAtIndex(size_t index,
									   chip::MutableByteSpan &protocolVersion)
{
	LOG_DBG("GetAliroSupportedBLEUWBProtocolVersionAtIndex");

#if CONFIG_DOOR_LOCK_ALIRO_DOOR_LOCK_DELEGATE_BLE_UWB_SUPPORT

#ifdef CONFIG_ALIRO_AT_HOST
	protocolVersion.reduce_size(0);
	return CHIP_ERROR_NOT_IMPLEMENTED;
#else
	size_t versionCount{};
	const auto *versions = Aliro::AliroStack::Instance().GetBleUwbProtocolVersions(versionCount);

	return EncodeProtocolVersion(index, protocolVersion, versions, versionCount);
#endif

#else // CONFIG_DOOR_LOCK_ALIRO_DOOR_LOCK_DELEGATE_BLE_UWB_SUPPORT

	protocolVersion.reduce_size(0);
	return CHIP_ERROR_NOT_IMPLEMENTED;

#endif // CONFIG_DOOR_LOCK_ALIRO_DOOR_LOCK_DELEGATE_BLE_UWB_SUPPORT
}

uint8_t DoorLockDelegate::GetAliroBLEAdvertisingVersion()
{
	LOG_DBG("GetAliroBLEAdvertisingVersion");

#ifdef CONFIG_DOOR_LOCK_ALIRO_DOOR_LOCK_DELEGATE_BLE_UWB_SUPPORT

#ifdef CONFIG_ALIRO_AT_HOST
	return 0;
#else
	return Aliro::AliroStack::Instance().GetBleAdvertisingVersion();
#endif

#else // CONFIG_DOOR_LOCK_ALIRO_DOOR_LOCK_DELEGATE_BLE_UWB_SUPPORT

	return 0;

#endif // CONFIG_DOOR_LOCK_ALIRO_DOOR_LOCK_DELEGATE_BLE_UWB_SUPPORT
}

uint16_t DoorLockDelegate::GetNumberOfAliroCredentialIssuerKeysSupported()
{
	LOG_DBG("GetNumberOfAliroCredentialIssuerKeysSupported");

	return CONFIG_DOOR_LOCK_ACCESS_MANAGER_CREDENTIAL_ISSUER_MAX_STORED_KEYS;
}

uint16_t DoorLockDelegate::GetNumberOfAliroEndpointKeysSupported()
{
	LOG_DBG("GetNumberOfAliroEndpointKeysSupported");

	return CONFIG_DOOR_LOCK_ACCESS_MANAGER_ACCESS_CREDENTIAL_MAX_STORED_KEYS;
}

CHIP_ERROR DoorLockDelegate::SetAliroReaderConfig(const chip::ByteSpan &signingKey,
						  const chip::ByteSpan &verificationKey,
						  const chip::ByteSpan &groupIdentifier,
						  const chip::Optional<chip::ByteSpan> &groupResolvingKey)
{
	LOG_DBG("SetAliroReaderConfig");

	VerifyOrReturnError(signingKey.size() == kAliroSigningKeySize, CHIP_ERROR_INVALID_ARGUMENT);
	VerifyOrReturnError(verificationKey.size() == kAliroReaderVerificationKeySize, CHIP_ERROR_INVALID_ARGUMENT);
	VerifyOrReturnError(groupIdentifier.size() == kAliroReaderGroupIdentifierSize, CHIP_ERROR_INVALID_ARGUMENT);
#ifdef CONFIG_ALIRO_AT_HOST
#ifdef CONFIG_DOOR_LOCK_ALIRO_DOOR_LOCK_DELEGATE_BLE_UWB_SUPPORT
	VerifyOrReturnError(groupResolvingKey.HasValue() &&
				    groupResolvingKey.Value().size() == kAliroGroupResolvingKeySize,
			    CHIP_ERROR_INVALID_ARGUMENT);
	/* Group resolving key is provisioned on the reader MCU via AT (not implemented yet). */
#endif
	Aliro::Identifier identifier{};
	std::copy_n(groupIdentifier.data(), kAliroReaderGroupIdentifierSize, identifier.data());
	const psa_status_t randomStatus = psa_generate_random(
		identifier.data() + kAliroReaderGroupIdentifierSize, kAliroReaderGroupSubIdentifierSize);
	VerifyOrReturnError(randomStatus == PSA_SUCCESS, CHIP_ERROR_INTERNAL);
	if (at_reader_identifier_create(identifier.data(), identifier.size()) != 0) {
		LOG_ERR("Failed to create reader identifier");
		return CHIP_ERROR_INTERNAL;
	}
	if (!at_reader_private_key_set(signingKey.data(), signingKey.size())) {
		LOG_ERR("Failed to set reader private key");
		return CHIP_ERROR_INTERNAL;
	}
	if (!at_reader_start()) {
		LOG_ERR("Failed to start reader");
		if (!at_reader_private_key_set(nullptr, 0)) {
			LOG_ERR("Failed to clear reader private key after reader start failure");
		}
		return CHIP_ERROR_INTERNAL;
	}
#else
#ifdef CONFIG_DOOR_LOCK_ALIRO_DOOR_LOCK_DELEGATE_BLE_UWB_SUPPORT

	VerifyOrReturnError(groupResolvingKey.HasValue() &&
				    groupResolvingKey.Value().size() == kAliroGroupResolvingKeySize,
			    CHIP_ERROR_INVALID_ARGUMENT);

#endif // CONFIG_DOOR_LOCK_ALIRO_DOOR_LOCK_DELEGATE_BLE_UWB_SUPPORT

	Aliro::CryptoTypes::PrivateKey privateKey{};
	Aliro::Identifier identifier{};
	Aliro::CryptoTypes::GroupResolvingKey groupResKey{};

	std::copy(signingKey.begin(), signingKey.end(), privateKey.data());
	std::copy_n(groupIdentifier.data(), kAliroReaderGroupIdentifierSize, identifier.data());

	const auto status = psa_generate_random(identifier.data() + kAliroReaderGroupIdentifierSize,
						kAliroReaderGroupSubIdentifierSize);
	VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

	VerifyOrReturnError(DoorLock::ReaderStorage::SetIdentifier(identifier) == ALIRO_NO_ERROR, CHIP_ERROR_INTERNAL);

	if (groupResolvingKey.HasValue()) {
		std::copy(groupResolvingKey.Value().begin(), groupResolvingKey.Value().end(), groupResKey.data());
	}

	VerifyOrReturnError(DoorLock::ReaderStorage::SetPrivateKey(privateKey) == ALIRO_NO_ERROR, CHIP_ERROR_INTERNAL);

#ifdef CONFIG_DOOR_LOCK_ALIRO_DOOR_LOCK_DELEGATE_BLE_UWB_SUPPORT

	VerifyOrReturnError(DoorLock::ReaderStorage::SetGroupResolvingKey(groupResKey) == ALIRO_NO_ERROR,
			    CHIP_ERROR_INTERNAL);

#endif // CONFIG_DOOR_LOCK_ALIRO_DOOR_LOCK_DELEGATE_BLE_UWB_SUPPORT
#endif // CONFIG_ALIRO_AT_HOST
	OnAliroReaderConfigSet();

	return CHIP_NO_ERROR;
}

CHIP_ERROR DoorLockDelegate::ClearAliroReaderConfig()
{
	LOG_DBG("ClearAliroReaderConfig");

#ifdef CONFIG_ALIRO_AT_HOST
	if (!at_reader_stop()) {
		LOG_ERR("Failed to stop reader");
		return CHIP_ERROR_INTERNAL;
	}
	if (!at_reader_clear(AT_READER_CLEAR_ALL)) {
		LOG_ERR("Failed to clear reader configuration");
		return CHIP_ERROR_INTERNAL;
	}
#else
	OnAliroReaderConfigClear();
	VerifyOrReturnError(DoorLock::ReaderStorage::ClearAll() == ALIRO_NO_ERROR, CHIP_ERROR_INTERNAL);
#endif
	return CHIP_NO_ERROR;
}

} // namespace DoorLock
