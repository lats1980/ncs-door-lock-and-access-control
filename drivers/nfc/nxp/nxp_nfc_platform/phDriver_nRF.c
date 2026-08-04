#include <Board_nRF.h>
#include <phDriver.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(nxp_nfc_platform, CONFIG_NFC_LOG_LEVEL);

static const struct gpio_dt_spec irq_gpio =
    GPIO_DT_SPEC_GET(DT_INST(0, nxp_pn5190), irq_gpios);

static const struct gpio_dt_spec reset_gpio =
    GPIO_DT_SPEC_GET(DT_INST(0, nxp_pn5190), reset_gpios);

static pphDriver_TimerCallBck_t palTimerCallback;

static struct gpio_callback irq_gpio_cb;
static bool irq_pin_configured;

static int logical_level_to_dt_output(const struct gpio_dt_spec *gpio, uint8_t logical_level)
{
	bool drive_high = (logical_level == PH_DRIVER_SET_HIGH);

	if (gpio->dt_flags & GPIO_ACTIVE_LOW) {
		return drive_high ? 0 : 1;
	}

	return drive_high ? 1 : 0;
}

static uint8_t dt_input_to_logical_level(const struct gpio_dt_spec *gpio)
{
	int raw_level = gpio_pin_get_raw(gpio->port, gpio->pin);

	return raw_level ? PH_DRIVER_SET_HIGH : PH_DRIVER_SET_LOW;
}

void dal_timer_handler(struct k_timer *dummy)
{
    if(NULL != palTimerCallback)
    {
        palTimerCallback();
    }
}

K_TIMER_DEFINE(dal_timer, dal_timer_handler, NULL);

K_SEM_DEFINE(irq_gpio_sem, 0, 1);

static void irq_gpio_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    k_sem_give(&irq_gpio_sem);
}

phStatus_t phDriver_ConfigureIrqPin(void)
{
    int ret;

    if (irq_pin_configured) {
        return PH_DRIVER_SUCCESS;
    }

    k_sem_reset(&irq_gpio_sem);

    if (!gpio_is_ready_dt(&irq_gpio)) {
        LOG_ERR("IRQ GPIO device not ready");
        return PH_DRIVER_ERROR;
    }

    ret = gpio_pin_configure_dt(&irq_gpio, GPIO_INPUT);
    if (ret != 0) {
        LOG_ERR("Configuring IRQ GPIO pin failed: %d", ret);
        return PH_DRIVER_ERROR;
    }

    gpio_init_callback(&irq_gpio_cb, irq_gpio_handler, BIT(irq_gpio.pin));

    ret = gpio_add_callback(irq_gpio.port, &irq_gpio_cb);
    if (ret != 0) {
        LOG_ERR("gpio_add_callback failed: %d", ret);
        return PH_DRIVER_ERROR;
    }

    ret = gpio_pin_interrupt_configure_dt(&irq_gpio, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        LOG_ERR("gpio_pin_interrupt_configure_dt failed: %d", ret);
        gpio_remove_callback(irq_gpio.port, &irq_gpio_cb);
        return PH_DRIVER_ERROR;
    }

    irq_pin_configured = true;

    return PH_DRIVER_SUCCESS;
}

phStatus_t phDriver_TimerStart(phDriver_Timer_Unit_t eTimerUnit, uint32_t dwTimePeriod, pphDriver_TimerCallBck_t pTimerCallBack)
{
	palTimerCallback = pTimerCallBack;
	switch(eTimerUnit)
	{
		case PH_DRIVER_TIMER_SECS:
            k_timer_start(&dal_timer, K_SECONDS(dwTimePeriod), K_NO_WAIT);
            break;
		case PH_DRIVER_TIMER_MILLI_SECS:
            k_timer_start(&dal_timer, K_MSEC(dwTimePeriod), K_NO_WAIT);
            break;
		case PH_DRIVER_TIMER_MICRO_SECS:
            k_timer_start(&dal_timer, K_USEC(dwTimePeriod), K_NO_WAIT);
            break;
        default:
            LOG_ERR("phDriver_TimerStart eTimerUnit err");
            break;
	}

    if(NULL == pTimerCallBack)
    {
        k_timer_status_sync(&dal_timer);
    }

    return PH_DRIVER_SUCCESS;
}

phStatus_t phDriver_TimerStop(void)
{
	k_timer_stop(&dal_timer);

    return PH_DRIVER_SUCCESS;
}

phStatus_t phDriver_PinConfig(uint32_t dwPinNumber, phDriver_Pin_Func_t ePinFunc, phDriver_Pin_Config_t *pPinConfig)
{
    ARG_UNUSED(ePinFunc);
    ARG_UNUSED(pPinConfig);

    if (dwPinNumber == irq_gpio.pin) {
        return phDriver_ConfigureIrqPin();
    }

    return PH_DRIVER_SUCCESS;
}

uint8_t phDriver_PinRead(uint32_t dwPinNumber, phDriver_Pin_Func_t ePinFunc)
{
	ARG_UNUSED(ePinFunc);

	if (dwPinNumber == irq_gpio.pin) {
		return (uint8_t)gpio_pin_get(irq_gpio.port, irq_gpio.pin);
	}

	if (dwPinNumber == reset_gpio.pin) {
		return dt_input_to_logical_level(&reset_gpio);
	}

	return 0;
}

phStatus_t phDriver_IRQPinPoll(uint32_t dwPinNumber, phDriver_Pin_Func_t ePinFunc, phDriver_Interrupt_Config_t eInterruptType)
{
    k_sem_take(&irq_gpio_sem, K_FOREVER);

    return PH_DRIVER_SUCCESS;
}

void phDriver_PinWrite(uint32_t dwPinNumber, uint8_t bValue)
{
	if (dwPinNumber == reset_gpio.pin) {
		gpio_pin_set_dt(&reset_gpio, logical_level_to_dt_output(&reset_gpio, bValue));
	}
}

void phDriver_PinClearIntStatus(uint32_t dwPinNumber)
{
}

phStatus_t phDriver_IRQPinRead(uint32_t dwPinNumber)
{
    return PH_DRIVER_SUCCESS;
}

void phDriver_EnterCriticalSection(void)
{
}

void phDriver_ExitCriticalSection(void)
{
}
