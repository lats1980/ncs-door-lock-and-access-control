#ifndef AT_COMMAND_H
#define AT_COMMAND_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef DL_VERSION
#define DL_VERSION "1.0.0"
#endif
#ifndef NCS_VERSION_STRING
#define NCS_VERSION_STRING "ncs"
#endif

#define DL_AT_LINE_BUF_SIZE      256
#define DL_AT_CMD_RESP_BUF_SIZE 512

#define DL_START_CMD_STR      "AT+ALIROSTART\r\n"
#define DL_START_CMD_TIMEOUT 5

#define DL_ALIROCLEARSTORAGE_CMD "AT+ALIROCLEARSTORAGE=%d\r\n"
#define DL_ALIROCLEARSTORAGE_TIMEOUT 5

#define DL_RESET_CMD "AT+RESET\r\n"

#define DL_LOCK      "AT+LOCK"
#define DL_UNLOCK      "AT+UNLOCK"
#define DL_LOCKUNLOCK_TIMEOUT 5

#define DL_READERKEY_CMD     "AT+READERKEY?\r\n"
#define DL_READERKEY_TIMEOUT 5
#define READER_KEY_SIZE 32

#define DL_READERPUBLICKEY_CMD     "AT+READERPUBLICKEY?\r\n"
#define DL_READERPUBLICKEY_TIMEOUT 5
#define READER_PUBLIC_KEY_SIZE 65

#define DL_AMCMGR_ADD_CMD     "AT+AMCMGR=1,"
#define DL_AMCMGR_REMOVE_CMD  "AT+AMCMGR=0,"
#define DL_AMCMGR_TIMEOUT 5
#define DL_AMCMGR_PUBLIC_KEY_SIZE 65

#define DL_READERID_CMD     "AT+READERID?\r\n"
#define DL_READERID_TIMEOUT 5
#define READER_ID_SIZE 16
#define READER_SUBID_SIZE 16

#define DL_ALIRO_EXPEDITED_VERSION_CMD "AT+ALIROEXPEDITEDVER?\r\n"
#define DL_ALIRO_EXPEDITED_VERSION_TIMEOUT 5

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Process received AT command data. Called by transport layer when data is received.
 * @param data  Received bytes (valid only for the duration of the call).
 * @param len   Number of bytes.
 * @param stop_at_receive Output: set to true to stop further processing of this buffer by transport layer.
 * @return Number of bytes processed from the input buffer.
 */
size_t at_process(const uint8_t *data, size_t len, bool *stop_at_receive);

/**
 * @brief Send data via the AT transport layer (UART or BLE).
 * @param data  Bytes to send.
 * @param len   Number of bytes.
 * @return 0 on success, negative errno on failure.
 */
int at_send(const uint8_t *data, size_t len);

/**
 * @brief Receive data. Called from transport when data is received.
 * @param data  Received data (must be copied if needed after return).
 * @param len   Length in bytes.
 * @return 0 on success, negative errno on failure.
 */
int at_receive(const uint8_t *data, size_t len);

/**
 * @brief Send a null-terminated string via the AT transport layer.
 * @param str Null-terminated string; NULL returns -EINVAL.
 * @return Same as at_send().
 */
int at_send_str(const char *str);

#ifdef __cplusplus
}
#endif

#endif // AT_COMMAND_H