/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/logging/log.h>

#include "uwb_logging.h"

LOG_MODULE_REGISTER(nxp_uwb_log, CONFIG_UWB_NXP_LOG_LEVEL);

static uint8_t nlog_zephyr_level(int level)
{
    switch (level) {
    case UWB_LOG_ERROR_LEVEL:
        return LOG_LEVEL_ERR;
    case UWB_LOG_WARN_LEVEL:
        return LOG_LEVEL_WRN;
    case UWB_LOG_INFO_LEVEL:
        return LOG_LEVEL_INF;
    default:
        return LOG_LEVEL_DBG;
    }
}

static void nlog_out(uint8_t level, const char *line)
{
    switch (level) {
    case LOG_LEVEL_ERR:
        LOG_ERR("%s", line);
        break;
    case LOG_LEVEL_WRN:
        LOG_WRN("%s", line);
        break;
    case LOG_LEVEL_INF:
        LOG_INF("%s", line);
        break;
    default:
        LOG_DBG("%s", line);
        break;
    }
}

void nLog(const char *tag, int level, const char *fmt, ...)
{
    const uint8_t zlevel = nlog_zephyr_level(level);

    /* Messages above the configured level are not formatted at all */
    if (zlevel > CONFIG_UWB_NXP_LOG_LEVEL) {
        return;
    }

    char line[CONFIG_UWB_NXP_LOG_LINE_SIZE];
    int n = snprintf(line, sizeof(line), "[%s] ", tag);
    size_t pos = (n > 0) ? MIN((size_t)n, sizeof(line) - 1) : 0;
    va_list args;

    va_start(args, fmt);
    (void)vsnprintf(line + pos, sizeof(line) - pos, fmt, args);
    va_end(args);

    nlog_out(zlevel, line);
}

void nLog_au8(const char *tag, int level, const char *message, const unsigned char *array, size_t array_len)
{
    const uint8_t zlevel = nlog_zephyr_level(level);

    if (zlevel > CONFIG_UWB_NXP_LOG_LEVEL) {
        return;
    }

    char line[CONFIG_UWB_NXP_LOG_LINE_SIZE];
    int n = snprintf(line, sizeof(line), "[%s] %s:", tag, message ? message : "");
    size_t pos = (n > 0) ? MIN((size_t)n, sizeof(line) - 1) : 0;

    for (size_t i = 0; i < array_len && pos + 4 < sizeof(line); i++) {
        pos += snprintf(line + pos, sizeof(line) - pos, " %02x", array[i]);
    }

    nlog_out(zlevel, line);
}

/* Called by UwbApi_Init() / UwbApi_ShutDown(): Zephyr logging needs no setup */
void phUwb_LogInit(void)
{
}

void phUwb_LogDeInit(void)
{
}
