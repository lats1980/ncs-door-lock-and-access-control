#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "sr150.h"
#include "uwb_bus_interface.h"

LOG_MODULE_REGISTER(uwb_bus_io, CONFIG_UWB_NXP_PLATFORM_LOG_LEVEL);

/* Binary semaphore (max=1) — matches the reference NXP implementation.
 * Using max=1 prevents stale-count accumulation from spurious edges:
 * if the semaphore is already at 1, further k_sem_give calls are no-ops. */
static K_SEM_DEFINE(s_irq_sem, 0, 1);

uwb_bus_status_t uwb_bus_io_init(uwb_bus_board_ctx_t *pCtx)
{
    (void)pCtx;
    k_sem_reset(&s_irq_sem);
    return kUWB_bus_Status_OK;
}

uwb_bus_status_t uwb_bus_io_deinit(uwb_bus_board_ctx_t *pCtx)
{
    (void)pCtx;
    return kUWB_bus_Status_OK;
}

uwb_bus_status_t uwb_bus_io_val_get(uwb_bus_board_ctx_t *pCtx, uwbs_io_t gpioPin, uwbs_io_state_t *pGpioValue)
{
    (void)pCtx;
    if (pGpioValue == NULL) return kUWB_bus_Status_FAILED;
    switch (gpioPin) {
    case kUWBS_IO_I_UWBS_IRQ:
        *pGpioValue = sr150_gpio_irq_get() ? kUWBS_IO_State_High : kUWBS_IO_State_Low;
        break;
    default:
        *pGpioValue = kUWBS_IO_State_Low;
        break;
    }
    return kUWB_bus_Status_OK;
}

uwb_bus_status_t uwb_bus_io_val_set(uwb_bus_board_ctx_t *pCtx, uwbs_io_t gpioPin, uwbs_io_state_t gpioValue)
{
    (void)pCtx;
    switch (gpioPin) {
    case kUWBS_IO_O_HELIOS_SYNC:
        if (gpioValue == kUWBS_IO_State_High) sr150_sync_high();
        else sr150_sync_low();
        break;
    case kUWBS_IO_O_ENABLE_HELIOS:
        if (gpioValue == kUWBS_IO_State_High) sr150_en_set(1);
        else sr150_en_set(0);
        break;
    case kUWBS_IO_O_ENABLE_UWBS_1V8:
        if (gpioValue == kUWBS_IO_State_High) sr150_power_set(1);
        else sr150_power_set(0);
        break;
    case kUWBS_IO_O_RSTN:
        if (gpioValue == kUWBS_IO_State_High) sr150_en_set(1);
        else sr150_en_set(0);
        break;
    default:
        break;
    }
    return kUWB_bus_Status_OK;
}

uwb_bus_status_t uwb_bus_io_irq_wait(uwb_bus_board_ctx_t *pCtx, uint32_t timeout_ms)
{
    /*
     * The NXP TML code waits up to 10 s for an IRQ.  That is longer than the
     * UCI command retry window, so the reader could not recover in time when
     * a retry response is lost.  Cap the wait (CONFIG_UWB_NXP_IRQ_WAIT_MAX_MS)
     * instead of patching the NXP source.
     */
    if (timeout_ms > CONFIG_UWB_NXP_IRQ_WAIT_MAX_MS) {
        timeout_ms = CONFIG_UWB_NXP_IRQ_WAIT_MAX_MS;
    }

    struct k_sem *sem = &s_irq_sem;

    (void)pCtx;

    /*
     * Drain stale tokens BEFORE re-arming the interrupt.
     * If we drained after re-arming, a rising edge that fires
     * between re-arm and drain would be consumed by the drain
     * and lost — the wait below would time out because the ISR
     * already disabled itself.
     */
    while (k_sem_take(sem, K_NO_WAIT) == 0) {
        /* consume stale token */
    }

    /* Re-arm the one-shot IRQ.  After this point every rising edge
     * gives the semaphore and disables the interrupt, so exactly
     * one edge can be captured per wait cycle.
     */
    sr150_irq_enable();

    /*
     * SR150 firmware may assert IRQ before the host reader is ready.
     * Since the IRQ is edge-triggered (RISING), if we missed the first
     * edge the semaphore will never be given and we would deadlock.
     * Check IRQ level first: if it is already HIGH, return immediately
     * without waiting for a rising edge.
     */
    if (sr150_gpio_irq_get()) {
        return kUWB_bus_Status_OK;
    }

    if (k_sem_take(sem, K_MSEC(timeout_ms)) == 0) {
        return kUWB_bus_Status_OK;
    }
    return kUWB_bus_Status_FAILED;
}

void uwb_bus_io_irq_cb(void *pCtx)
{
    /* The ISR signals the single IRQ semaphore; pCtx is not needed. */
    ARG_UNUSED(pCtx);
    k_sem_give(&s_irq_sem);
}

uwb_bus_status_t uwb_bus_io_uwbs_irq_enable(uwb_bus_board_ctx_t *pCtx)
{
    (void)pCtx;
    sr150_irq_enable();
    return kUWB_bus_Status_OK;
}

uwb_bus_status_t uwb_bus_io_uwbs_irq_disable(uwb_bus_board_ctx_t *pCtx)
{
    (void)pCtx;
    sr150_irq_disable();
    return kUWB_bus_Status_OK;
}

uwb_bus_status_t uwb_bus_io_irq_en(uwb_bus_board_ctx_t *pCtx, uwbs_io_t irqPin, uwbs_io_callback *pCallback)
{
    /* The GPIO ISR in sr150.c always signals the IRQ semaphore; the callback is not used. */
    ARG_UNUSED(pCtx);
    ARG_UNUSED(pCallback);
    if (irqPin != kUWBS_IO_I_UWBS_IRQ) return kUWB_bus_Status_FAILED;
    sr150_irq_enable();
    return kUWB_bus_Status_OK;
}

uwb_bus_status_t uwb_bus_io_irq_dis(uwb_bus_board_ctx_t *pCtx, uwbs_io_t irqPin)
{
    ARG_UNUSED(pCtx);
    if (irqPin != kUWBS_IO_I_UWBS_IRQ) return kUWB_bus_Status_FAILED;
    sr150_irq_disable();
    return kUWB_bus_Status_OK;
}
