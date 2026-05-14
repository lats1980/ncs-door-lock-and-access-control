/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "storage.h"

#include <stdint.h>
#include <zephyr/fs/zms.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_DECLARE(internal_zms, CONFIG_DOOR_LOCK_INTERNAL_ZMS_LOG_LEVEL);

namespace DoorLock::InternalZms::Storage {

namespace {

zms_fs zms;

} // namespace

int Init(uint8_t partitionId)
{
	const flash_area *fa;

	auto rc = flash_area_open(partitionId, &fa);
	if (rc) {
		LOG_ERR("flash_area_open failed: %d", rc);
		return rc;
	}
	LOG_DBG("flash_area_open successful, offset: 0x%lx, size: 0x%x, device: %s", fa->fa_off, fa->fa_size,
		fa->fa_dev->name);

	flash_sector hw_flash_sector;
	uint32_t sector_cnt{ 1 };

	rc = flash_area_get_sectors(partitionId, &sector_cnt, &hw_flash_sector);
	if (rc != 0 && rc != -ENOMEM) {
		LOG_ERR("flash_area_get_sectors failed: %d", rc);
		return rc;
	}

	sector_cnt = fa->fa_size / hw_flash_sector.fs_size;
	LOG_DBG("flash_area_get_sectors successful, sector_size: 0x%x, sector_count: %u", hw_flash_sector.fs_size,
		sector_cnt);

	zms.offset = fa->fa_off;
	zms.sector_size = hw_flash_sector.fs_size;
	zms.sector_count = sector_cnt;
	zms.flash_device = fa->fa_dev;

	rc = zms_mount(&zms);
	if (rc) {
		LOG_ERR("zms_mount failed: %d", rc);
		return rc;
	}

	return 0;
}

int Clear()
{
	auto rc = zms_clear(&zms);
	if (rc) {
		LOG_ERR("zms_clear failed: %d", rc);
		return rc;
	}

	return 0;
}

int Write(Id id, const void *data, size_t len)
{
	const auto rc = zms_write(&zms, id, data, len);
	if (rc < 0) {
		LOG_ERR("zms_write failed: %d", rc);
		return rc;
	}

	/* rc == 0 means the identical data was already stored; treat as success. */
	return 0;
}

int Read(Id id, void *data, size_t &len)
{
	const auto rc = zms_read(&zms, id, data, len);
	if (rc < 0) {
		LOG_ERR("zms_read failed: %d", rc);
		return rc;
	}

	len = static_cast<size_t>(rc);
	return 0;
}

int Delete(Id id)
{
	const auto rc = zms_delete(&zms, id);
	if (rc) {
		LOG_ERR("zms_delete failed: %d", rc);
		return rc;
	}

	return 0;
}

} // namespace DoorLock::InternalZms::Storage
