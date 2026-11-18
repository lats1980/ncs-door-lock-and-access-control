/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "session_management.h"
#include "uwb_impl.h"

#include <doorlock/utils/memory.h>
#include <doorlock/utils/mutex_guard.h>
#include <doorlock/utils/utils.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(UwbImpl);

namespace Aliro::Uwb {

int ConvertUwbApiStatus(tUWBAPI_STATUS status)
{
	switch (status) {
	case UWBAPI_STATUS_OK:
		return 0;
	case UWBAPI_STATUS_INVALID_PARAM:
		return -EINVAL;
	case UWBAPI_STATUS_TIMEOUT:
		return -ETIMEDOUT;
	case UWBAPI_STATUS_MAX_SESSIONS_EXCEEDED:
		return -ENOMEM;
	case UWBAPI_STATUS_SESSION_NOT_EXIST:
		return -ENOENT;
	case UWBAPI_STATUS_NOT_INITIALIZED:
		return -ENODEV;
	default:
		return -EIO;
	}
}

int UltraWideBandImpl::AddSession(SessionContext *sessionCtx)
{
	using DoorLock::Utils::MutexGuard;

	VerifyOrReturnValue(sessionCtx, -EINVAL, LOG_ERR("Session context is null"));

	MutexGuard lock{ mMutex };
	sys_slist_append(&mActiveSessionsList, &sessionCtx->mSessionContextNode);

	return 0;
}

void UltraWideBandImpl::RemoveSession(SessionContext *sessionCtx)
{
	using DoorLock::Utils::MutexGuard;

	{
		MutexGuard lock{ mMutex };
		VerifyOrReturn(sys_slist_find_and_remove(&mActiveSessionsList, &sessionCtx->mSessionContextNode),
			       LOG_WRN("Session doesn't exist"));
	}

	DestroySession(sessionCtx);
	delete sessionCtx;
}

void UltraWideBandImpl::RemoveAllSessions()
{
	using DoorLock::Utils::MutexGuard;

	while (true) {
		SessionContext *sessionCtx = nullptr;

		{
			MutexGuard lock{ mMutex };
			sys_snode_t *node = sys_slist_get(&mActiveSessionsList);
			VerifyOrReturn(node);
			sessionCtx = CONTAINER_OF(node, SessionContext, mSessionContextNode);
		}

		DestroySession(sessionCtx);
		delete sessionCtx;
	}
}

SessionContext *UltraWideBandImpl::FindSession(SessionContextHandle sessionContextData)
{
	using DoorLock::Utils::MutexGuard;

	SessionContext *sessionCtx{};

	MutexGuard lock{ mMutex };
	SYS_SLIST_FOR_EACH_CONTAINER (&mActiveSessionsList, sessionCtx, mSessionContextNode) {
		if (sessionCtx->mSessionContextData == sessionContextData) {
			return sessionCtx;
		}
	}

	return nullptr;
}

SessionContext *UltraWideBandImpl::FindSessionByNxpHandle(uint32_t sessionHandle)
{
	using DoorLock::Utils::MutexGuard;

	if (sessionHandle == 0) {
		return nullptr;
	}

	SessionContext *sessionCtx{};

	MutexGuard lock{ mMutex };
	SYS_SLIST_FOR_EACH_CONTAINER (&mActiveSessionsList, sessionCtx, mSessionContextNode) {
		if (sessionCtx->mNxpSessionHandle == sessionHandle) {
			return sessionCtx;
		}
	}

	return nullptr;
}

void UltraWideBandImpl::DestroySession(SessionContext *sessionCtx)
{
	/* Tear down the NXP UCI session. */
	StopRanging(*sessionCtx);
}

} // namespace Aliro::Uwb
