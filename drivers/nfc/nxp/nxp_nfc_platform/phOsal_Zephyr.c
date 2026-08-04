/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <phOsal.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <inttypes.h>

/* *****************************************************************************************************************
 * Includes
 * ***************************************************************************************************************** */

/* *****************************************************************************************************************
 * Internal Definitions
 * ***************************************************************************************************************** */

/** Mask covering all bits of a phOsal_EventBits_t, used to read back the full event state after post/clear. */
#define PH_OSAL_ZEPHYR_EVENT_ALL_FLAGS_MASK    (0xFFFFFFFFUL)

/* *****************************************************************************************************************
 * Type Definitions
 * ***************************************************************************************************************** */

/* *****************************************************************************************************************
 * Global and Static Variables
 * Total Size: NNNbytes
 * ***************************************************************************************************************** */

static struct k_event sEventQueuePool[PH_OSAL_CONFIG_MAX_NUM_EVENTS];
static bool sEventQueueInUse[PH_OSAL_CONFIG_MAX_NUM_EVENTS] = { false };

/** Protects the scan-and-mark of sEventQueueInUse[] against concurrent phOsal_EventCreate()/phOsal_EventDelete() callers. */
static struct k_spinlock sEventPoolLock;

/* *****************************************************************************************************************
 * Private Functions Prototypes
 * ***************************************************************************************************************** */
static phStatus_t phOsal_NullOs_ReturnUnsupportedCmd(void);

/* *****************************************************************************************************************
 * Public Functions
 * ***************************************************************************************************************** */
phStatus_t phOsal_Init(void)
{
    return PH_OSAL_SUCCESS;
}

phStatus_t phOsal_EventCreate(phOsal_Event_t *eventHandle, pphOsal_EventObj_t eventObj)
{
    uint8_t bEventIndex;
    k_spinlock_key_t key;

    if ((eventHandle == NULL) || (eventObj == NULL))
    {
        return PH_OSAL_ADD_COMPCODE(PH_OSAL_ERROR, PH_COMP_OSAL);
    }

    /* Enter Critical Section: find a free slot and claim it atomically so that
     * concurrent callers can never be handed the same event slot. */
    key = k_spin_lock(&sEventPoolLock);

    for (bEventIndex = 0; bEventIndex < PH_OSAL_CONFIG_MAX_NUM_EVENTS; bEventIndex++)
    {
        if (sEventQueueInUse[bEventIndex] == false)
        {
            sEventQueueInUse[bEventIndex] = true;
            break;
        }
    }

    /* Exit Critical Section. */
    k_spin_unlock(&sEventPoolLock, key);

    if (bEventIndex >= PH_OSAL_CONFIG_MAX_NUM_EVENTS)
    {
        return PH_OSAL_ADD_COMPCODE(PH_OSAL_ERROR, PH_COMP_OSAL);
    }

    k_event_init(&(sEventQueuePool[bEventIndex]));
    *eventHandle = (phOsal_Event_t)(&(sEventQueuePool[bEventIndex]));
    eventObj->EventHandle = (phOsal_Event_t)(&(sEventQueuePool[bEventIndex]));
    eventObj->dwEventIndex = bEventIndex;

    return PH_OSAL_SUCCESS;
}

phStatus_t phOsal_EventPend(volatile phOsal_Event_t * eventHandle, phOsal_EventOpt_t options, phOsal_Ticks_t ticksToWait,
    phOsal_EventBits_t FlagsToWait, phOsal_EventBits_t *pCurrFlags)
{
    phStatus_t status = PH_OSAL_SUCCESS;
    phOsal_EventBits_t CurrentFlags;

    if ((eventHandle == NULL) || ((*eventHandle) == NULL))
    {
        return PH_OSAL_ADD_COMPCODE(PH_OSAL_ERROR, PH_COMP_OSAL);
    }

    if (options & E_OS_EVENT_OPT_PEND_SET_ALL)
    {
        CurrentFlags = k_event_wait_all((struct k_event *)(*eventHandle), FlagsToWait, false, K_TICKS(ticksToWait));
    }
    else
    {
        CurrentFlags = k_event_wait((struct k_event *)(*eventHandle), FlagsToWait, false, K_TICKS(ticksToWait));
    }

    if (CurrentFlags == 0)
    {
        status = PH_OSAL_IO_TIMEOUT;
    }
    else
    {
        status = PH_OSAL_SUCCESS;
    }

    if (options & E_OS_EVENT_OPT_PEND_CLEAR_ON_EXIT)
    {
        k_event_clear((struct k_event *)(*eventHandle), FlagsToWait);
    }

    if (pCurrFlags != NULL)
    {
        *pCurrFlags = CurrentFlags;
    }

    return PH_OSAL_ADD_COMPCODE(status, PH_COMP_OSAL);
}

phStatus_t phOsal_EventPost(phOsal_Event_t * eventHandle, phOsal_EventOpt_t options, phOsal_EventBits_t FlagsToPost,
    phOsal_EventBits_t *pCurrFlags)
{
    phOsal_EventBits_t CurrentFlags;

    if ((eventHandle == NULL) || ((*eventHandle) == NULL))
    {
        return PH_OSAL_ADD_COMPCODE(PH_OSAL_ERROR, PH_COMP_OSAL);
    }

    k_event_post((struct k_event *)(*eventHandle), FlagsToPost);

    if (pCurrFlags != NULL)
    {
        CurrentFlags = k_event_test((struct k_event *)(*eventHandle), PH_OSAL_ZEPHYR_EVENT_ALL_FLAGS_MASK);
        *pCurrFlags = CurrentFlags;
    }

    return PH_OSAL_SUCCESS;
}

phStatus_t phOsal_EventClear(phOsal_Event_t * eventHandle, phOsal_EventOpt_t options, phOsal_EventBits_t FlagsToClear,
    phOsal_EventBits_t *pCurrFlags)
{
    phOsal_EventBits_t CurrentFlags;

    if ((eventHandle == NULL) || ((*eventHandle) == NULL))
    {
        return PH_OSAL_ADD_COMPCODE(PH_OSAL_ERROR, PH_COMP_OSAL);
    }

    if (pCurrFlags != NULL)
    {
        CurrentFlags = k_event_test((struct k_event *)(*eventHandle), PH_OSAL_ZEPHYR_EVENT_ALL_FLAGS_MASK);
        *pCurrFlags = CurrentFlags;
    }

    k_event_clear((struct k_event *)(*eventHandle), FlagsToClear);

    return PH_OSAL_SUCCESS;
}

phStatus_t phOsal_EventGet(phOsal_Event_t * eventHandle, phOsal_EventBits_t *pCurrFlags)
{
    return PH_OSAL_SUCCESS;
}

phStatus_t phOsal_EventDelete(phOsal_Event_t * eventHandle)
{
    struct k_event *pEvent;
    uint32_t dwEventIndex;
    k_spinlock_key_t key;

    if ((eventHandle == NULL) || ((*eventHandle) == NULL))
    {
        return PH_OSAL_ADD_COMPCODE(PH_OSAL_ERROR, PH_COMP_OSAL);
    }

    pEvent = (struct k_event *)(*eventHandle);

    /* Reject handles that were not handed out by phOsal_EventCreate(). */
    if ((pEvent < sEventQueuePool) || (pEvent >= (sEventQueuePool + PH_OSAL_CONFIG_MAX_NUM_EVENTS)))
    {
        return PH_OSAL_ADD_COMPCODE(PH_OSAL_ERROR, PH_COMP_OSAL);
    }

    dwEventIndex = (uint32_t)(pEvent - sEventQueuePool);

    /* Enter Critical Section. */
    key = k_spin_lock(&sEventPoolLock);
    sEventQueueInUse[dwEventIndex] = false;
    /* Exit Critical Section. */
    k_spin_unlock(&sEventPoolLock, key);

    *eventHandle = NULL;

    return PH_OSAL_SUCCESS;
}

phStatus_t phOsal_TimerCreate(phOsal_Timer_t *timerHandle, pphOsal_TimerObj_t timerObj)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_TimerStart(phOsal_Timer_t * timerHandle)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_TimerStop(phOsal_Timer_t * timerHandle)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_TimerGetCurrent(phOsal_Timer_t * timerHandle, uint32_t * pdwGetElapsedTime)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_TimerModify(phOsal_Timer_t *timerHandle, pphOsal_TimerObj_t timerObj)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_TimerDelete(phOsal_Timer_t * timerHandle)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_ThreadSecureStack(uint32_t stackSizeInNum)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_ThreadDelete(phOsal_Thread_t * threadHandle)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_ThreadCreate(phOsal_Thread_t *threadHandle, pphOsal_ThreadObj_t threadObj, pphOsal_StartFunc_t startFunc, void *arg)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_ThreadChangePrio(phOsal_Thread_t * threadHandle, uint32_t newPrio)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_ThreadExit(void)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_ThreadDelay(phOsal_Ticks_t ticksToSleep)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_SemCreate(phOsal_Semaphore_t *semHandle, pphOsal_SemObj_t semObj, phOsal_SemOpt_t opt)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_SemPend(phOsal_Semaphore_t * semHandle, phOsal_TimerPeriodObj_t timePeriodToWait)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_SemPost(phOsal_Semaphore_t * semHandle, phOsal_SemOpt_t opt)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_SemDelete(phOsal_Semaphore_t * semHandle)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_MutexCreate(phOsal_Mutex_t *mutexHandle, pphOsal_MutexObj_t mutexObj)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_MutexLock(phOsal_Mutex_t * mutexHandle, phOsal_TimerPeriodObj_t timePeriodToWait)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_MutexUnLock(phOsal_Mutex_t * mutexHandle)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

phStatus_t phOsal_MutexDelete(phOsal_Mutex_t * mutexHandle)
{
    return phOsal_NullOs_ReturnUnsupportedCmd();
}

void phOsal_StartScheduler(void)
{
    return;
}

/* *****************************************************************************************************************
 * Private Functions
 * ***************************************************************************************************************** */
static phStatus_t phOsal_NullOs_ReturnUnsupportedCmd(void)
{
    return (PH_OSAL_UNSUPPORTED_COMMAND | PH_COMP_OSAL);
}
