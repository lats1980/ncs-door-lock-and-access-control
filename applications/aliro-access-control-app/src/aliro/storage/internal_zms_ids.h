/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include <internal_zms/internal_zms.h>

namespace DoorLock::Storage::InternalZmsIds {

#if CONFIG_DOOR_LOCK_STORAGE_MAX_STORED_ACCESS_DOCUMENTS > 0

constexpr InternalZms::Id kAccessDocumentRangeStart{ 0 };
constexpr InternalZms::Id kAccessDocumentRangeSize{ CONFIG_DOOR_LOCK_STORAGE_MAX_STORED_ACCESS_DOCUMENTS };

#endif // CONFIG_DOOR_LOCK_STORAGE_MAX_STORED_ACCESS_DOCUMENTS > 0

} // namespace DoorLock::Storage::InternalZmsIds
