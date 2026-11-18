/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef UWB_ASSERT_H_
#define UWB_ASSERT_H_

#include <zephyr/sys/__assert.h>

#define UWB_DEBUG_ASSERT(cond, msg) __ASSERT(cond, msg)

#endif /* UWB_ASSERT_H_ */
