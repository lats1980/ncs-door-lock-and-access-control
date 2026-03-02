/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "rfal_nfc_config.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pal_nfc_config, CONFIG_NFC_LOG_LEVEL);

void rfalNfcWakeupConfig(rfalNfcDiscoverParam *conf)
{
#ifdef CONFIG_RFAL_FEATURE_WAKEUP_MODE

	conf->wakeupEnabled = true;
	conf->wakeupPollBefore = IS_ENABLED(CONFIG_RFAL_WAKEUP_POLL_BEFORE);
	conf->wakeupNPolls = CONFIG_RFAL_WAKEUP_NPOLLS;

#endif // CONFIG_RFAL_FEATURE_WAKEUP_MODE
}
