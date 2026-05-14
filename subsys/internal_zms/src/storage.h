/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file storage.h
 * @brief Low-level ZMS storage backend for Internal ZMS.
 *
 * Wraps Zephyr's ZMS (Zephyr Memory Storage) API to provide basic read,
 * write, and delete operations on an internal RRAM partition used by the
 * Internal ZMS subsystem.
 */

#pragma once

#include <internal_zms/internal_zms.h>

namespace DoorLock::InternalZms::Storage {

/**
 * @brief Initialize the ZMS storage backend.
 *
 * Opens the specified flash partition, queries its sector layout, and
 * mounts the ZMS file system.
 *
 * @param partitionId Flash partition identifier to open and mount.
 *
 * @retval 0 on success.
 * @retval negative errno code on failure.
 */
int Init(uint8_t partitionId);

/**
 * @brief Erase all entries from the ZMS partition.
 *
 * Clears the ZMS file system. After calling this function, @ref Init must be
 * called again before using the storage APIs.
 *
 * @retval 0 on success.
 * @retval negative errno code on failure.
 */
int Clear();

/**
 * @brief Write raw data to the ZMS partition.
 *
 * @param id   ZMS entry identifier.
 * @param data Pointer to the data to store.
 * @param len  Length of @p data in bytes.
 *
 * @retval 0 on success.
 * @retval negative errno code on failure.
 */
int Write(Id id, const void *data, size_t len);

/**
 * @brief Read raw data from the ZMS partition.
 *
 * @param[in]     id   ZMS entry identifier.
 * @param[out]    data Pointer to the buffer that receives the stored data.
 * @param[in,out] len  On input, the capacity of @p data in bytes.
 *                     On output, the number of bytes actually read.
 *
 * @retval 0 on success.
 * @retval negative errno code on failure.
 */
int Read(Id id, void *data, size_t &len);

/**
 * @brief Delete an entry from the ZMS partition.
 *
 * @param id ZMS entry identifier to delete.
 *
 * @retval 0 on success.
 * @retval negative errno code on failure.
 */
int Delete(Id id);

} // namespace DoorLock::InternalZms::Storage
