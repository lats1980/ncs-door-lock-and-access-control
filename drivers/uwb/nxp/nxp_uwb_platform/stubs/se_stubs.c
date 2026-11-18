/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * The NXP UWB API references the secure element (SE051W) binding/locking state
 * machines. No SE is used on this platform, so they are not compiled; these
 * weak stubs only keep the link complete if the API entry points are used.
 */

#include <zephyr/toolchain.h>
#include "UwbApi_Types.h"

__weak tUWBAPI_STATUS Binding_Process(void)
{
    return UWBAPI_STATUS_FAILED;
}

__weak tUWBAPI_STATUS locking_Process(void)
{
    return UWBAPI_STATUS_FAILED;
}
