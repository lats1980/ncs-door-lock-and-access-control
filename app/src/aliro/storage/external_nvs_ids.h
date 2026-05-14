/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include <cstdint>

namespace DoorLock::Storage::ExternalNvsIds {

#if CONFIG_DOOR_LOCK_STORAGE_MAX_STORED_ACCESS_DOCUMENTS > 0

constexpr uint16_t kAccessDocumentRangeStart{ 0 };
constexpr uint16_t kAccessDocumentRangeSize{ CONFIG_DOOR_LOCK_STORAGE_MAX_STORED_ACCESS_DOCUMENTS };

#endif // CONFIG_DOOR_LOCK_STORAGE_MAX_STORED_ACCESS_DOCUMENTS > 0

} // namespace DoorLock::Storage::ExternalNvsIds
