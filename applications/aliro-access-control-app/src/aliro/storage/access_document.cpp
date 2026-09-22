/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "access_document.h"

#include "access_manager_impl.h"
#include "aliro/utils.h"
#if defined(CONFIG_DOOR_LOCK_INTERNAL_ZMS)
#include "internal_zms_ids.h"

#include <internal_zms/internal_zms.h>
#elif defined(CONFIG_DOOR_LOCK_EXTERNAL_NVS)
#include "external_nvs_ids.h"

#include <external_nvs/external_nvs.h>
#else
#error "Access Document storage requires CONFIG_DOOR_LOCK_INTERNAL_ZMS or CONFIG_DOOR_LOCK_EXTERNAL_NVS"
#endif // CONFIG_DOOR_LOCK_INTERNAL_ZMS || CONFIG_DOOR_LOCK_EXTERNAL_NVS

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(access_document, CONFIG_DOOR_LOCK_APP_LOG_LEVEL);

#if defined(CONFIG_DOOR_LOCK_INTERNAL_ZMS)
namespace Nvs = DoorLock::InternalZms;
namespace NvsIds = DoorLock::Storage::InternalZmsIds;
#elif defined(CONFIG_DOOR_LOCK_EXTERNAL_NVS)
namespace Nvs = DoorLock::ExternalNvs;
namespace NvsIds = DoorLock::Storage::ExternalNvsIds;
#endif // CONFIG_DOOR_LOCK_INTERNAL_ZMS || CONFIG_DOOR_LOCK_EXTERNAL_NVS

namespace Aliro {

namespace {

#ifdef CONFIG_DOOR_LOCK_STORAGE_MIGRATE_ACCESS_DOCUMENT_V1
constexpr int kAccessDocumentV1Found{ 1 };

struct AccessDocumentV1 {
	uint32_t mVersion;
	size_t mCredentialIssuerKeyIndex;
	Aliro::Timestamp mSignedTimestamp;
	ValidityIteration mAccessIteration;
	Aliro::CryptoTypes::PublicKey mPublicKey;
	size_t mAccessDocumentSize;
	std::array<uint8_t, AccessDocument::kAccessDocumentSize> mAccessDocument;
};
#endif // CONFIG_DOOR_LOCK_STORAGE_MIGRATE_ACCESS_DOCUMENT_V1

Nvs::Id GetStorageId(size_t index)
{
	return NvsIds::kAccessDocumentRangeStart + static_cast<Nvs::Id>(index);
}

bool IsIndexInRange(size_t index)
{
	return index < NvsIds::kAccessDocumentRangeSize;
}

int ReadAccessDocumentHelper(size_t index, AccessDocument &ad)
{
	const auto id = GetStorageId(index);
	size_t len = sizeof(AccessDocument);
	const auto error = Nvs::Read(id, &ad, len);

	if (error != 0) {
		return error;
	}

#ifdef CONFIG_DOOR_LOCK_STORAGE_MIGRATE_ACCESS_DOCUMENT_V1
	if (len == sizeof(AccessDocumentV1) && ad.mVersion == 1) {
		return kAccessDocumentV1Found;
	}
#endif // CONFIG_DOOR_LOCK_STORAGE_MIGRATE_ACCESS_DOCUMENT_V1

	if (len != sizeof(AccessDocument)) {
		LOG_ERR("Invalid Access Document size at index: %zu, expected: %zu, got: %zu", index,
			sizeof(AccessDocument), len);
		return -EIO;
	}

	if (ad.mVersion != AccessDocument::kVersion) {
		LOG_ERR("Unsupported Access Document version at index %zu: %u", index, ad.mVersion);
		return -EIO;
	}

	return 0;
}

} // namespace

AliroError LoadAccessDocuments()
{
	AccessDocument ad;
	for (size_t index = 0; index < CONFIG_DOOR_LOCK_STORAGE_MAX_STORED_ACCESS_DOCUMENTS; index++) {
		const auto error = ReadAccessDocumentHelper(index, ad);
		if (error == -ENOENT) {
			continue;
		}

#ifdef CONFIG_DOOR_LOCK_STORAGE_MIGRATE_ACCESS_DOCUMENT_V1
		if (error == kAccessDocumentV1Found) {
			LOG_INF("Removing version 1 Access Document at index: %zu", index);
			ReturnErrorOnFailure(AccessManagerInstance().AddPublicKey(
				CryptoTypes::PublicKey{}, AccessManager::PublicKeyType::AccessDocument, index));
			ReturnErrorOnFailure(AccessManagerInstance().RemovePublicKey(
				AccessManager::PublicKeyType::AccessDocument, index));
			continue;
		}
#endif // CONFIG_DOOR_LOCK_STORAGE_MIGRATE_ACCESS_DOCUMENT_V1

		VerifyOrReturnStatus(error == 0, AliroError::FromInt(error),
				     LOG_ERR("Failed to read Access Document at index: %zu, error code: %d", index,
					     error));

		ReturnErrorOnFailure(AccessManagerInstance().AddPublicKey(
			ad.mPublicKey, AccessManager::PublicKeyType::AccessDocument, index));

		LOG_DBG("Loaded AD at index: %zu, Version: %u, CI index: %u, Timestamp: %.*s, Access Iteration: %" PRIu64,
			index, ad.mVersion, ad.mCredentialIssuerKeyIndex, ad.mSignedTimestamp.size(),
			ad.mSignedTimestamp.data(), ad.mAccessIteration);
	}

	return ALIRO_NO_ERROR;
}

AliroError StoreAccessDocument(size_t index, const AccessDocument &ad)
{
	VerifyOrReturnStatus(IsIndexInRange(index), ALIRO_INVALID_ARGUMENT,
			     LOG_ERR("Access Document index out of range: %zu", index));

	const auto id = GetStorageId(index);
	const auto error = Nvs::Write(id, &ad, sizeof(AccessDocument));
	VerifyOrReturnStatus(error == 0, AliroError::FromInt(error),
			     LOG_ERR("Failed to store Access Document at index: %zu", index));

	return ALIRO_NO_ERROR;
}

AliroError ReadAccessDocument(size_t index, AccessDocument &ad)
{
	VerifyOrReturnStatus(IsIndexInRange(index), ALIRO_INVALID_ARGUMENT,
			     LOG_ERR("Access Document index out of range: %zu", index));

	const auto error = ReadAccessDocumentHelper(index, ad);
	VerifyOrReturnStatus(error == 0, AliroError::FromInt(error),
			     LOG_ERR("Failed to get Access Document at index: %zu", index));

	return ALIRO_NO_ERROR;
}

AliroError ClearAccessDocument(size_t index)
{
	VerifyOrReturnStatus(IsIndexInRange(index), ALIRO_INVALID_ARGUMENT,
			     LOG_ERR("Access Document index out of range: %zu", index));

	const auto id = GetStorageId(index);
	const auto error = Nvs::Delete(id);
	VerifyOrReturnStatus(error == 0, AliroError::FromInt(error),
			     LOG_ERR("Failed to clear Access Document at index: %zu", index));

	return ALIRO_NO_ERROR;
}

} // namespace Aliro
