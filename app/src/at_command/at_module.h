/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef AT_MODULE_H
#define AT_MODULE_H

#include "aliro/types.h"
#include "at_command.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/types.h>
#include <ctype.h>
#include <modem/at_parser.h>

#define DL_SYNC_STR     "Ready\r\n"
#define DL_OK_STR		 "\r\nOK\r\n"
#define DL_ERROR_STR	 "\r\nERROR\r\n"

enum at_module_event {
    AT_MODULE_TRANSPORT_DISCONNECTED,
    AT_MODULE_TRANSPORT_CONNECTED
};

typedef void (*at_module_event_callback_t)(enum at_module_event event);

/**
 * @brief Update the AT module event to application.
 */
void at_module_update_event(enum at_module_event event);

/** Parsed current time from AT+TIME response (at_send_time_response). */
struct dl_time {
	uint16_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	bool valid;
};

/**
 * @brief AT module for aliro application. Provides UART/BLE transport for AT commands.
 *
 */
int at_module_init(at_module_event_callback_t event_cb);

/**
 * @brief Send an unsolicited result code (URC) to the host. URCs are used for asynchronous notifications.
 * @param urc   Null-terminated string containing the URC to send (e.g. "+DLLOCK:1").
 * @return 0 on success, negative error code on failure
 */
int at_module_send_urc(const char *urc);

/**
 * @brief Notify the host of a change in lock state.
 * @param state The new lock state.
 * @return 0 on success, negative error code on failure.
 */
int dl_notify_lock_state(Aliro::ReaderStateByte state);

/**
 * @brief Send the time request URC "+TIME: REQ" to the host.
 *        The host is expected to respond with current time (e.g. AT+TIME=1,...).
 * @return 0 on success, negative error code on failure.
 */
int dl_send_time_req_urc(void);


/** Get the last parsed current time from AT+TIME. Returns 0 on success, -ENODATA if not set. */
int dl_time_get(uint8_t timeout, struct dl_time *out);

/**
 * @brief Callback type for custom AT command handlers (DL_AT_CMD).
 *
 * @param cmd_type    Command type (AT_PARSER_CMD_TYPE_SET/READ/TEST/UNKNOWN).
 * @param parser     Initialized AT parser; use at_parser_num_get(), at_parser_string_get(), etc.
 * @param param_count Number of parameters in the current AT command line.
 * @return 0 on success; non-zero on error (module will send ERROR response).
 *         To send custom response text, use at_send() / at_send_str() from within the handler.
 */
typedef int (*dl_at_cmd_callback_t)(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				    uint32_t param_count);

/** Register a custom AT command for future use. Used by DL_AT_CMD macro. */
void dl_at_cmd_register(const char *filter, dl_at_cmd_callback_t cb);

/**
 * @brief Define a custom AT command and handler.
 *
 * @param entry     Unique entry name (e.g. xver).
 * @param _filter   AT command filter (prefix match), e.g. "AT+DLVER".
 * @param _callback Handler: int _callback(enum at_parser_cmd_type cmd_type,
 *                  struct at_parser *parser, uint32_t param_count).
 */
#define DL_AT_CMD(entry, _filter, _callback)                                            \
	static int _callback(enum at_parser_cmd_type cmd_type, struct at_parser *parser, \
			     uint32_t param_count);                                      \
	static struct _dl_reg_##entry {                                                  \
		_dl_reg_##entry() { dl_at_cmd_register(_filter, _callback); }   \
	} _dl_reg_instance_##entry


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif // AT_MODULE_H