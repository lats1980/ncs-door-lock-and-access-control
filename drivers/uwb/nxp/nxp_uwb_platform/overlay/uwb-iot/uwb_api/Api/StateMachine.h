/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Overlay for libs/uwb-iot/uwb_api/Api/StateMachine.h of the NXP package.
 * Only the SE binding/locking entry points that UwbApi references are kept;
 * they are weak stubs, see nxp_uwb_platform/stubs/se_stubs.c.
 */

#ifndef STATEMACHINE_H
#define STATEMACHINE_H

#include "UwbApi_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

tUWBAPI_STATUS Binding_Process(void);
tUWBAPI_STATUS locking_Process(void);

#ifdef __cplusplus
}
#endif

#endif /* STATEMACHINE_H */
