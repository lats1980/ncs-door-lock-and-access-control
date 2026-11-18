/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Overlay for libs/uwb-iot/uwb_api/Api/SE_Wrapper.h of the NXP package.
 * No SE051W is used, so the SE middleware (sus_APDU.h, se_FiRaLite_API.h) the
 * original header depends on is not needed.
 */

#ifndef __SE_WRAPPER_H__
#define __SE_WRAPPER_H__

typedef int se_status_t;
#define SE_STATUS_OK 0

static inline int Se_API_SetHandle(void *handle)
{
    (void)handle;
    return 0;
}

static inline void Se_API_ResetHandle(void)
{
}

#endif /* __SE_WRAPPER_H__ */
