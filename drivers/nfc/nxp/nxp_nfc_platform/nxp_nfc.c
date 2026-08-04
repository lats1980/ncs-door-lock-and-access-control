/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <nxp_nfc.h>

#include <Board_nRF.h>
#include <phDriver.h>
#include <phOsal.h>
#include <phbalReg.h>
#include <phNfcLib.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(nxp_nfc_platform, CONFIG_NFC_LOG_LEVEL);

#define NXP_NFC_IRQ_TASK_PRIO  5
#define NXP_NFC_IRQ_TASK_STACK 2048

static phbalReg_Type_t bal_params;

static const struct gpio_dt_spec reset_gpio =
	GPIO_DT_SPEC_GET(DT_INST(0, nxp_pn5190), reset_gpios);

static K_THREAD_STACK_DEFINE(irq_task_stack, NXP_NFC_IRQ_TASK_STACK);
static struct k_thread irq_task_data;
static bool irq_monitor_started;
static nxp_nfc_irq_handler_t irq_handler;

static void nxp_nfc_irq_task(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		if (phDriver_IRQPinPoll(PHDRIVER_PIN_IRQ, PH_DRIVER_PINFUNC_INTERRUPT,
					PIN_IRQ_TRIGGER_TYPE) == PH_DRIVER_SUCCESS) {
			if (irq_handler != NULL) {
				irq_handler();
			}
		}
	}
}

int nxp_nfc_init(void)
{
	int err;
	phStatus_t status;
	phNfcLib_AppContext_t app_context = {0};
	phNfcLib_Status_t nfc_lib_status;

	if (!gpio_is_ready_dt(&reset_gpio)) {
		LOG_ERR("Reset GPIO device not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&reset_gpio, GPIO_OUTPUT_INACTIVE);
	if (err) {
		LOG_ERR("Configuring reset GPIO pin failed: %d", err);
		return err;
	}

	status = phDriver_ConfigureIrqPin();
	if (status != PH_DRIVER_SUCCESS) {
		LOG_ERR("phDriver_ConfigureIrqPin failed: 0x%04X", status);
		return -EIO;
	}

	status = phOsal_Init();
	if (status != PH_OSAL_SUCCESS) {
		LOG_ERR("phOsal_Init failed: 0x%04X", status);
		return -EIO;
	}

	status = phbalReg_Init(&bal_params, sizeof(phbalReg_Type_t));
	if (status != PH_ERR_SUCCESS) {
		LOG_ERR("phbalReg_Init failed: 0x%04X", status);
		return -EIO;
	}

	app_context.pBalDataparams = &bal_params;
	nfc_lib_status = phNfcLib_SetContext(&app_context);
	if (nfc_lib_status != PH_NFCLIB_STATUS_SUCCESS) {
		LOG_ERR("phNfcLib_SetContext failed: 0x%04X", nfc_lib_status);
		return -EIO;
	}

	return 0;
}

int nxp_nfc_start_irq_monitor(nxp_nfc_irq_handler_t handler)
{
	if (handler == NULL) {
		return -EINVAL;
	}

	if (irq_monitor_started) {
		return 0;
	}

	irq_handler = handler;

	k_thread_create(&irq_task_data, irq_task_stack,
			K_THREAD_STACK_SIZEOF(irq_task_stack),
			nxp_nfc_irq_task, NULL, NULL, NULL,
			NXP_NFC_IRQ_TASK_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&irq_task_data, "NFC_IRQ");

	irq_monitor_started = true;

	return 0;
}
