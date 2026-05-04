/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef AT_HOST_H
#define AT_HOST_H

#include "aliro/types.h"
#include "at_command.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/types.h>
#include <stdbool.h>

/**
 * @brief AT command result codes
 */
enum at_cmd_state {
	AT_CMD_OK,
	AT_CMD_ERROR,
	AT_CMD_ERROR_CMS,
	AT_CMD_ERROR_CME,
	AT_CMD_PENDING
};

/**
 * @brief Send an AT command and wait for the result.
 *
 * Sends the command and blocks until OK/ERROR (or variant)
 * is received or the timeout expires. Response data is still passed to
 * at_process and any registered DL_AT_RSP handlers.
 *
 * @param command AT command string (e.g. "AT\r\n").
 * @param timeout Response timeout in seconds; 0 means wait forever.
 * @return AT_CMD_OK, AT_CMD_ERROR, AT_CMD_ERROR_CMS, or AT_CMD_ERROR_CME on success;
 *         -EAGAIN on timeout;
 *         -EPERM if host not initialized;
 *         other negative errno on send failure.
 */
int at_host_send_cmd(const char *command, uint32_t timeout);

/**
 * @brief Send AT+RESET\r\n to the external reader MCU (no response wait).
 *
 * @return 0 on success, -EPERM if host not initialized, negative errno from transport on send failure.
 */
int at_host_send_reset_cmd(void);

/**
 * @brief Send AT+RESET, then wait for unsolicited DL_SYNC_STR (line "Ready" / "Ready\r\n" from peer).
 *
 * @param sync_timeout_sec Maximum seconds to wait for sync after send succeeds.
 * @return 0 on success, -ETIMEDOUT if sync not received in time, -EPERM if host not initialized,
 *         negative errno on send failure.
 */
int at_host_send_reset_wait_dl_sync(uint32_t sync_timeout_sec);

/**
 * @brief Send an AT command and optionally capture the response body.
 *
 * Same as at_host_send_cmd, but when @p rsp_buf is non-NULL and @p rsp_size > 0,
 * copies the raw response (including lines before OK/ERROR) into @p rsp_buf,
 * null-terminated. Useful for parsing responses like "+READERKEY: 1".
 *
 * @param command  AT command string including terminator (e.g. "AT+READERKEY?\r\n").
 * @param timeout  Response timeout in seconds; 0 means wait forever.
 * @param rsp_buf  Buffer to receive response text, or NULL to ignore.
 * @param rsp_size Size of @p rsp_buf.
 * @return Same as at_host_send_cmd.
 */
int at_host_send_cmd_with_rsp(const char *command, uint32_t timeout,
				char *rsp_buf, size_t rsp_size);

/**
 * @brief Check whether the reader private key is set on the device.
 *
 * Sends AT+READERKEY? and parses the response for "#READERKEY: 1".
 *
 * @return true if reader private key is set, false otherwise (not set, timeout, or error).
 */
bool at_reader_private_key_is_set(void);

/**
 * @brief Start the reader functionality on the device.
 *
 * Sends AT+ALIROSTART to start the reader. This should be called after setting the private key.
 *
 * @return true on success, false otherwise (timeout or error).
 */
bool at_reader_start(void);

/**
 * @brief Clear the reader's Aliro storage, optionally reinitializing it.
 * 
 * Sends AT+ALIROCLEARSTORAGE=1 to clear storage and reinitialize, or AT+ALIROCLEARSTORAGE=0 to clear
 * without reinitializing.
 * 
 * @return true on success, false otherwise (timeout or error).
 */
bool at_reader_clear_storage(bool reinitializeStorage);

/**
 * @brief Set the reader private key on the device.
 *
 * Sends AT+READERKEY=1 with the provided private key.
 *
 * @param private_key  Pointer to the private key data.
 * @param key_size     Size of the private key data.
 * @return true on success, false otherwise (timeout, or error).
 */
bool at_reader_private_key_set(const uint8_t *private_key, size_t key_size);

/**
 * @brief Get the reader public key for verification via AT+READERPUBLICKEY?
 *
 * Sends AT+READERPUBLICKEY? and parses the hex-encoded public key from the response.
 *
 * @param key_out  Buffer to receive the 65-byte public key.
 * @param key_size Must be 65 (kAliroReaderVerificationKeySize).
 * @return 0 on success, negative errno on failure (timeout, parse error, or key not set).
 */
int at_reader_public_key_get(uint8_t *key_out, size_t key_size);

/**
 * @brief Create a new reader identifier via AT+READERID=1
 *
 * Sends AT+READERID=1 with the provided 16-byte group identifier (hex-encoded).
 * The device uses this as the reader ID and generates the SUBID randomly.
 *
 * @param group_identifier       Pointer to the 16-byte group identifier.
 * @param group_identifier_size  Size of group_identifier (must be 16, READER_ID_SIZE).
 * @return 0 on success, negative errno on failure (timeout, invalid args, or error).
 */
int at_reader_identifier_create(const uint8_t *group_identifier, size_t group_identifier_size);

/**
 * @brief Get the reader identifier via AT+READERID?
 * 
 * Sends AT+READERID? and parses the reader identifier from the response.
 * 
 * @param identifier_out Buffer to receive the full reader identifier (READERID + READERSUBID).
 * @param identifier_size Must be 32 (READER_ID_SIZE + READER_SUBID_SIZE, size of Aliro::Identifier).
 * @return 0 on success, negative errno on failure (timeout, parse error, or identifier not set).
 */
int at_reader_identifier_get(uint8_t *identifier_out, size_t identifier_size);

/**
 * @brief Get the Aliro expedited version list via AT+ALIROEXPEDITEDVER?
 *
 * Sends AT+ALIROEXPEDITEDVER? and parses the comma-separated version list from the response.
 * Each version is 2 bytes (uint16_t) stored big-endian in version_out.
 *
 * @param version_out       Buffer to receive the version data (2 bytes per version, big-endian).
 * @param version_out_max   Maximum bytes to write (must be multiple of 2).
 * @param version_count     Pointer to store the number of versions received.
 * @return 0 on success, negative errno on failure (timeout, parse error, or version not set).
 */
int at_aliro_expedited_version_get(uint8_t *version_out, size_t version_out_max, size_t *version_count);

/**
 * @brief Add a public key to the access manager via AT+AMCMGR=1 (DL_AMCMGR_ADD_CMD).
 *
 * Sends AT+AMCMGR=1,<index>,<type>,<hex_key> with the 65-byte public key encoded as 130 hex chars.
 *
 * @param index       Credential index.
 * @param type        Public key type (0=AccessCredential, 1=CredentialIssuer, 2=AccessDocument).
 * @param public_key  Pointer to the 65-byte public key (DL_AMCMGR_PUBLIC_KEY_SIZE).
 * @return true on success, false otherwise (timeout, invalid args, or error).
 */
bool at_amcmgr_add_public_key(uint16_t index, uint8_t type, const uint8_t *public_key);

/**
 * @brief Remove a public key from the access manager via AT+AMCMGR=0 (DL_AMCMGR_REMOVE_CMD).
 *
 * Sends AT+AMCMGR=0,<index>,<type>.
 *
 * @param index  Credential index.
 * @param type   Public key type (0=AccessCredential, 1=CredentialIssuer, 2=AccessDocument).
 * @return true on success, false otherwise (timeout or error).
 */
bool at_amcmgr_remove_public_key(uint16_t index, uint8_t type);

/**
 * @brief Lock the door via AT command.
 *
 * @param source The source of the lock operation (e.g. manual, auto, schedule).
 * @return true on success, false otherwise (timeout or error).
 */
bool at_aliro_lock(Aliro::OperationSource source);

/**
 * @brief Unlock the door via AT command.
 *
 * @param source The source of the unlock operation (e.g. manual, auto, schedule).
 * @return true on success, false otherwise (timeout or error).
 */
bool at_aliro_unlock(Aliro::OperationSource source);

/**
 * @brief Send current time response over AT command (AT+TIME=1,YYYY-MM-DD,HH:MM:SS).
 *
 * Used when handling the "+TIME: REQ" URC to report current time to the host.
 *
 * @param year   Year (e.g. 2026).
 * @param month  Month 1-12.
 * @param day    Day of month 1-31.
 * @param hour   Hour 0-23.
 * @param minute Minute 0-59.
 * @param second Second 0-59.
 * @return 0 on success, negative error code on failure.
 */
int at_send_time_response(uint16_t year, uint8_t month, uint8_t day,
			  uint8_t hour, uint8_t minute, uint8_t second);

/**
 * 
 * @brief AT Host for door lock application. Provides UART/BLE transport for AT commands.
 * 
 */
int at_host_init(void);

/**
 * @brief Callback type for custom AT response handlers (DL_AT_RSP).
 *
 * @param data     Received bytes (valid only for the duration of the call).
 * @param datalen  Number of bytes.
 * @return None.
 */
typedef void (*dl_at_rsp_callback_t)(const uint8_t *data, size_t datalen);

/** Register a custom AT response for future use. Used by DL_AT_RSP macro. */
void dl_at_rsp_register(const char *filter, dl_at_rsp_callback_t cb);

#define DL_AT_RSP(entry, _filter, _callback)                                            \
	static void __attribute__((constructor)) dl_at_rsp_register_##entry(void)           \
	{                                                                                   \
		dl_at_rsp_register(_filter, _callback);                                            \
	}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif // AT_HOST_H