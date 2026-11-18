#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdlib.h>
#include "phOsalUwb.h"
#include "phOsalUwb_Thread.h"
#include "phOsalUwb_Queue.h"
#include "phOsalUwb_Timer.h"

LOG_MODULE_REGISTER(phOsalUwb, CONFIG_UWB_NXP_PLATFORM_LOG_LEVEL);

/* ---- Memory ---- */
void *phOsalUwb_GetMemory(uint32_t dwSize) { return malloc(dwSize); }
void phOsalUwb_FreeMemory(void *pMem) { free(pMem); }
void phOsalUwb_SetMemory(void *pMem, uint8_t bVal, uint32_t dwSize) { memset(pMem, bVal, dwSize); }
void phOsalUwb_MemCopy(void *pDest, const void *pSrc, uint32_t dwSize) { memcpy(pDest, pSrc, dwSize); }
int32_t phOsalUwb_MemCompare(const void *pDest, const void *pSrc, uint32_t dwSize) { return memcmp(pDest, pSrc, dwSize); }

/* ---- Delay ---- */
void phOsalUwb_Delay(uint32_t dwDelay) { k_sleep(K_MSEC(dwDelay)); }

/* ---- Semaphore ---- */
UWBSTATUS phOsalUwb_CreateSemaphore(void **hSemaphore, uint8_t bInitialValue)
{
    if (hSemaphore == NULL) return UWBSTATUS_INVALID_PARAMETER;
    struct k_sem *sem = k_malloc(sizeof(struct k_sem));
    if (!sem) return UWBSTATUS_INSUFFICIENT_RESOURCES;
    k_sem_init(sem, bInitialValue, 1);
    *hSemaphore = sem;
    return UWBSTATUS_SUCCESS;
}

UWBSTATUS phOsalUwb_ProduceSemaphore(void *hSemaphore)
{
    if (hSemaphore == NULL) return UWBSTATUS_INVALID_PARAMETER;
    k_sem_give((struct k_sem *)hSemaphore);
    return UWBSTATUS_SUCCESS;
}

UWBSTATUS phOsalUwb_ConsumeSemaphore_WithTimeout(void *hSemaphore, uint32_t timeoutMs)
{
    if (hSemaphore == NULL) return UWBSTATUS_INVALID_PARAMETER;
    k_timeout_t t = (timeoutMs == MAX_DELAY) ? K_FOREVER : K_MSEC(timeoutMs);
    return (k_sem_take((struct k_sem *)hSemaphore, t) == 0) ? UWBSTATUS_SUCCESS : UWBSTATUS_FAILED;
}

/* ---- Mutex ---- */
UWBSTATUS phOsalUwb_CreateMutex(void **hMutex)
{
    if (hMutex == NULL) return UWBSTATUS_INVALID_PARAMETER;
    struct k_mutex *mutex = k_malloc(sizeof(struct k_mutex));
    if (!mutex) return UWBSTATUS_INSUFFICIENT_RESOURCES;
    k_mutex_init(mutex);
    *hMutex = mutex;
    return UWBSTATUS_SUCCESS;
}

UWBSTATUS phOsalUwb_LockMutex(void *hMutex)
{
    if (hMutex == NULL) return UWBSTATUS_INVALID_PARAMETER;
    return (k_mutex_lock((struct k_mutex *)hMutex, K_FOREVER) == 0) ? UWBSTATUS_SUCCESS : UWBSTATUS_FAILED;
}

UWBSTATUS phOsalUwb_UnlockMutex(void *hMutex)
{
    if (hMutex == NULL) return UWBSTATUS_INVALID_PARAMETER;
    k_mutex_unlock((struct k_mutex *)hMutex);
    return UWBSTATUS_SUCCESS;
}

UWBSTATUS phOsalUwb_DeleteMutex(void **hMutex)
{
    if (hMutex == NULL || *hMutex == NULL) return UWBSTATUS_INVALID_PARAMETER;
    k_free(*hMutex);
    *hMutex = NULL;
    return UWBSTATUS_SUCCESS;
}

/* ---- Binary semaphore ---- */
UWBSTATUS phOsalUwb_CreateBinSem(void **hBinSem)
{
    if (hBinSem == NULL) return UWBSTATUS_INVALID_PARAMETER;
    struct k_sem *sem = k_malloc(sizeof(struct k_sem));
    if (!sem) return UWBSTATUS_INSUFFICIENT_RESOURCES;
    k_sem_init(sem, 1, 1);
    *hBinSem = sem;
    return UWBSTATUS_SUCCESS;
}

/* ---- NXP Timer ID-based API ---- */
struct phOsalUwb_TimerEntry {
    struct k_work_delayable work;
    pphOsalUwb_TimerCallbck_t callback;
    void *context;
    uint32_t timer_id;
};

static struct phOsalUwb_TimerEntry *s_timer_slots[PH_OSALUWB_MAX_TIMER];

static void phOsalUwb_TimerEntry_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct phOsalUwb_TimerEntry *entry =
        CONTAINER_OF(dwork, struct phOsalUwb_TimerEntry, work);
    if (entry->callback) {
        entry->callback(entry->timer_id, entry->context);
    }
}

uint32_t phOsalUwb_Timer_Create(uint8_t bAutoReload)
{
    for (int i = 0; i < PH_OSALUWB_MAX_TIMER; i++) {
        if (s_timer_slots[i] == NULL) {
            s_timer_slots[i] = k_malloc(sizeof(struct phOsalUwb_TimerEntry));
            if (s_timer_slots[i]) {
                k_work_init_delayable(&s_timer_slots[i]->work, phOsalUwb_TimerEntry_handler);
                s_timer_slots[i]->callback = NULL;
                s_timer_slots[i]->context = NULL;
                s_timer_slots[i]->timer_id = i;
            }
            return i;
        }
    }
    return PH_OSALUWB_MAX_TIMER;
}

UWBSTATUS phOsalUwb_Timer_Start(uint32_t dwTimerId, uint32_t dwRegTimeCnt,
                                 pphOsalUwb_TimerCallbck_t pApplication_callback, void *pContext)
{
    if (dwTimerId >= PH_OSALUWB_MAX_TIMER || s_timer_slots[dwTimerId] == NULL)
        return UWBSTATUS_INVALID_PARAMETER;
    s_timer_slots[dwTimerId]->callback = pApplication_callback;
    s_timer_slots[dwTimerId]->context = pContext;
    k_work_schedule(&s_timer_slots[dwTimerId]->work, K_MSEC(dwRegTimeCnt));
    return UWBSTATUS_SUCCESS;
}

UWBSTATUS phOsalUwb_Timer_Stop(uint32_t dwTimerId)
{
    if (dwTimerId >= PH_OSALUWB_MAX_TIMER || s_timer_slots[dwTimerId] == NULL)
        return UWBSTATUS_INVALID_PARAMETER;
    k_work_cancel_delayable(&s_timer_slots[dwTimerId]->work);
    return UWBSTATUS_SUCCESS;
}

UWBSTATUS phOsalUwb_Timer_Delete(uint32_t dwTimerId)
{
    if (dwTimerId >= PH_OSALUWB_MAX_TIMER || s_timer_slots[dwTimerId] == NULL)
        return UWBSTATUS_INVALID_PARAMETER;
    k_work_cancel_delayable(&s_timer_slots[dwTimerId]->work);
    k_free(s_timer_slots[dwTimerId]);
    s_timer_slots[dwTimerId] = NULL;
    return UWBSTATUS_SUCCESS;
}

/* ---- Semaphore ---- */
UWBSTATUS phOsalUwb_DeleteSemaphore(void **hSemaphore)
{
    if (hSemaphore == NULL || *hSemaphore == NULL) return UWBSTATUS_INVALID_PARAMETER;
    k_free(*hSemaphore);
    *hSemaphore = NULL;
    return UWBSTATUS_SUCCESS;
}

/*
 * Thread pool.
 *
 * Stacks are pre-allocated (K_KERNEL_STACK) so that the MPU stack guard works
 * and no heap is needed.  The NXP middleware creates three threads (UWB task,
 * HAL client, TML reader).
 */
#define OSAL_MAX_THREADS CONFIG_UWB_NXP_MAX_THREADS

struct osal_thread_slot {
    struct k_thread thread;
    pphOsalUwb_ThreadFunction_t fn;
    void *ctx;
    bool used;
};

static struct osal_thread_slot osal_slots[OSAL_MAX_THREADS];
static K_KERNEL_STACK_ARRAY_DEFINE(osal_stacks, OSAL_MAX_THREADS, CONFIG_UWB_NXP_THREAD_STACK_SIZE);

/* Zephyr entry -> NXP "void fn(void *)" signature */
static void osal_thread_entry(void *p1, void *p2, void *p3)
{
    struct osal_thread_slot *slot = p1;

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    slot->fn(slot->ctx);
}

/*
 * NXP FreeRTOS priority at/above which a thread is considered "run first".
 * The UWB task is created with priority 5 while the HAL client and the TML
 * reader use 3 (see UwbAdaptation.c, phNxpUciHal.c, UwbCoreSDK_Internal.h).
 */
#define OSAL_NXP_PRIO_RUN_FIRST 5

/*
 * The NXP priorities (FreeRTOS, higher number == higher priority) are not
 * mapped one to one.  All middleware threads run at
 * CONFIG_UWB_NXP_THREAD_PRIORITY so that the UWB task, the HAL client and the
 * TML reader cannot starve each other.
 *
 * Start-up ordering
 * -----------------
 * The NXP code relies on the UWB task having the highest priority: on
 * FreeRTOS it runs up to its first blocking call inside StartUwbTask(),
 * before the creator continues.  uwb_task() publishes gp_uwbtask_ctrl and
 * clears uwb_cb there, both of which the creator uses/fills in right after
 * (phUwb_OSAL_send_msg(), UWA_Init()).  A Zephyr thread created at a priority
 * below its creator only starts later, which is a race.  To keep the NXP
 * sources unmodified such a thread is created at the highest application
 * priority, so it runs immediately until it blocks, and is moved to its final
 * priority afterwards.
 */
UWBSTATUS phOsalUwb_Thread_Create(void **hThread, pphOsalUwb_ThreadFunction_t pThreadFunction, void *pParam)
{
    if (hThread == NULL || pThreadFunction == NULL || pParam == NULL) return UWBSTATUS_INVALID_PARAMETER;

    phOsalUwb_ThreadCreationParams_t *params = (phOsalUwb_ThreadCreationParams_t *)pParam;
    const int final_prio = K_PRIO_PREEMPT(CONFIG_UWB_NXP_THREAD_PRIORITY);
    const bool run_first = params->priority >= OSAL_NXP_PRIO_RUN_FIRST;

    for (int i = 0; i < OSAL_MAX_THREADS; i++) {
        struct osal_thread_slot *slot = &osal_slots[i];

        if (slot->used) continue;

        slot->used = true;
        slot->fn   = pThreadFunction;
        slot->ctx  = params->pContext;

        k_tid_t tid = k_thread_create(&slot->thread, osal_stacks[i], K_KERNEL_STACK_SIZEOF(osal_stacks[i]),
                                      osal_thread_entry, slot, NULL, NULL,
                                      run_first ? K_HIGHEST_APPLICATION_THREAD_PRIO : final_prio, 0, K_NO_WAIT);
        if (tid == NULL) {
            slot->used = false;
            return UWBSTATUS_FAILED;
        }

        if (run_first) {
            /* Reached only once the new thread has blocked (or finished). */
            k_thread_priority_set(tid, final_prio);
        }
        if (IS_ENABLED(CONFIG_THREAD_NAME) && params->taskname[0] != '\0') {
            (void)k_thread_name_set(tid, params->taskname);
        }

        *hThread = (void *)tid;
        return UWBSTATUS_SUCCESS;
    }
    return UWBSTATUS_INSUFFICIENT_RESOURCES;
}

void phOsalUwb_TaskSuspend(UWBOSAL_TASK_HANDLE xTaskToSuspend)
{
    if (xTaskToSuspend) k_thread_suspend((k_tid_t)xTaskToSuspend);
}

void phOsalUwb_TaskResume(UWBOSAL_TASK_HANDLE xTaskToResume)
{
    if (xTaskToResume) k_thread_resume((k_tid_t)xTaskToResume);
}

UWBSTATUS phOsalUwb_Thread_Delete(UWBOSAL_TASK_HANDLE hThread)
{
    if (hThread == NULL) return UWBSTATUS_SUCCESS;

    k_thread_abort((k_tid_t)hThread);
    for (int i = 0; i < OSAL_MAX_THREADS; i++) {
        if (&osal_slots[i].thread == (struct k_thread *)hThread) {
            osal_slots[i].used = false;
        }
    }
    return UWBSTATUS_SUCCESS;
}

/* ---- Message Queue (POSIX-like, using k_msgq) ---- */
#include "phUwbTypes.h"

intptr_t phOsalUwb_msgget(uint32_t queueLength)
{
    struct k_msgq *q = k_malloc(sizeof(struct k_msgq));
    if (!q) return -1;
    char *buf = k_malloc(queueLength * sizeof(phLibUwb_Message_t));
    if (!buf) { k_free(q); return -1; }
    k_msgq_init(q, buf, sizeof(phLibUwb_Message_t), queueLength);
    return (intptr_t)q;
}

void phOsalUwb_msgrelease(intptr_t msqid)
{
    if (msqid > 0) k_free((void *)msqid);
}

UWBSTATUS phOsalUwb_msgsnd(intptr_t msqid, phLibUwb_Message_t *msg, unsigned long waittimeout)
{
    if (msqid <= 0 || msg == NULL) return UWBSTATUS_INVALID_PARAMETER;
    return (k_msgq_put((struct k_msgq *)msqid, msg, K_NO_WAIT) == 0) ? UWBSTATUS_SUCCESS : UWBSTATUS_FAILED;
}

UWBSTATUS phOsalUwb_msgrcv(intptr_t msqid, phLibUwb_Message_t *msg, unsigned long waittimeout)
{
    if (msqid <= 0 || msg == NULL) return UWBSTATUS_INVALID_PARAMETER;
    k_timeout_t t = (waittimeout == MAX_DELAY) ? K_FOREVER : K_MSEC(waittimeout);
    return (k_msgq_get((struct k_msgq *)msqid, msg, t) == 0) ? UWBSTATUS_SUCCESS : UWBSTATUS_FAILED;
}
