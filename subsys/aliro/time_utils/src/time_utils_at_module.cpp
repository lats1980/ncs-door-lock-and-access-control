/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <time_utils/time_utils.h>

#include <aliro_at/at_module.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(time_utils, CONFIG_DOOR_LOCK_ALIRO_TIME_UTILS_LOG_LEVEL);

namespace DoorLock::TimeUtils {

namespace {

/* Timeout in seconds for the host to answer the time request. */
constexpr uint8_t kTimeRequestTimeoutSec = 3;

/* Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's days_from_civil). */
int64_t DaysFromCivil(int year, int month, int day)
{
	year -= month <= 2;
	const int64_t era = (year >= 0 ? year : year - 399) / 400;
	const int64_t yoe = year - era * 400;
	const int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

	return era * 146097 + doe - 719468;
}

} // namespace

std::optional<Aliro::Time> GetCurrentTime()
{
	struct dl_time currentTime;

	if (dl_time_get(kTimeRequestTimeoutSec, &currentTime) != 0) {
		LOG_WRN("Current time not available from AT host");
		return std::nullopt;
	}

	return Aliro::Time(currentTime.year, currentTime.month, currentTime.day, currentTime.hour,
			   currentTime.minute, currentTime.second);
}

std::optional<uint32_t> GetCurrentUnixTime()
{
	const auto time = GetCurrentTime();

	if (!time.has_value()) {
		LOG_ERR("Wall clock time not available");
		return std::nullopt;
	}

	const int64_t seconds = DaysFromCivil(time->mYear, time->mMonth, time->mDay) * 86400 +
				time->mHour * 3600 + time->mMinute * 60 + time->mSecond;

	if (seconds < 0 || seconds > UINT32_MAX) {
		LOG_ERR("Time out of Unix 32-bit range");
		return std::nullopt;
	}

	return static_cast<uint32_t>(seconds);
}

} // namespace DoorLock::TimeUtils
