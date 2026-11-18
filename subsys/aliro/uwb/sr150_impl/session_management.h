/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include "session_context.h"

#include <UwbApi_Types.h>

namespace Aliro::Uwb {

class UltraWideBandImpl;

/**
 * @brief Converts NXP UwbApi status to a POSIX-style error code.
 */
int ConvertUwbApiStatus(tUWBAPI_STATUS status);

} // namespace Aliro::Uwb
