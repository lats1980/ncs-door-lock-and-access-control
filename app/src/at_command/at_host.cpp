#include "at_host.h"
#include "at_transport.h"

#include <atomic>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(at_host, CONFIG_DOOR_LOCK_APP_LOG_LEVEL);

#ifndef CONFIG_ALIRO_AT_MODULE_MAX_CUSTOM_RSPS
#define CONFIG_ALIRO_AT_MODULE_MAX_CUSTOM_RSPS 16
#endif

/* AT response strings (TS 27.007 style) */
static const char AT_CMD_OK_STR[]    = "\r\nOK\r\n";
static const char AT_CMD_ERROR_STR[] = "\r\nERROR\r\n";
static const char AT_CMD_CMS_STR[]   = "\r\n+CMS ERROR:";
static const char AT_CMD_CME_STR[]   = "\r\n+CME ERROR:";

static K_SEM_DEFINE(at_rsp_sem, 0, 1);
static K_SEM_DEFINE(dl_sync_sem, 0, 1);
static std::atomic<bool> s_dl_sync_armed;
static enum at_cmd_state at_cmd_state = AT_CMD_OK;
static bool at_host_initialized;

static uint8_t at_cmd_resp_buf[DL_AT_CMD_RESP_BUF_SIZE];
static size_t at_cmd_resp_len;

/* Optional response capture for at_host_send_cmd_with_rsp */
static char *s_rsp_buf;
static size_t s_rsp_size;

/* Find needle in haystack within haystack_len (like strnstr). */
static const char *find_substr(const char *haystack, size_t haystack_len,
			       const char *needle, size_t needle_len)
{
	if (needle_len == 0 || haystack_len < needle_len) {
		return NULL;
	}
	for (size_t i = 0; i <= haystack_len - needle_len; i++) {
		if (memcmp(haystack + i, needle, needle_len) == 0) {
			return haystack + i;
		}
	}
	return NULL;
}

/* Parse AT response in buffer; set at_cmd_state and return bytes consumed, or 0. */
static size_t parse_at_response(const char *data, size_t datalen)
{
	static const struct {
		const char *str;
		size_t len;
		enum at_cmd_state state;
	} responses[] = {
		{ AT_CMD_OK_STR,    sizeof(AT_CMD_OK_STR) - 1,    AT_CMD_OK },
		{ AT_CMD_ERROR_STR, sizeof(AT_CMD_ERROR_STR) - 1, AT_CMD_ERROR },
		{ AT_CMD_CMS_STR,   sizeof(AT_CMD_CMS_STR) - 1,   AT_CMD_ERROR_CMS },
		{ AT_CMD_CME_STR,   sizeof(AT_CMD_CME_STR) - 1,   AT_CMD_ERROR_CME },
	};

	for (size_t i = 0; i < ARRAY_SIZE(responses); i++) {
		const char *match = find_substr(data, datalen,
						responses[i].str, responses[i].len);
		if (!match) {
			continue;
		}
		if (responses[i].state == AT_CMD_OK || responses[i].state == AT_CMD_ERROR) {
			at_cmd_state = responses[i].state;
			return (match - data) + responses[i].len;
		}
		/* +CMS ERROR: / +CME ERROR: — consume until next \r\n */
		const char *rest = match + responses[i].len;
		size_t rest_len = datalen - (rest - data);
		const char *crlf = find_substr(rest, rest_len, "\r\n", 2);
		if (crlf) {
			at_cmd_state = responses[i].state;
			return (crlf - data) + 2;
		}
	}
	return 0;
}

struct dl_at_rsp_entry {
	const char *filter;
	dl_at_rsp_callback_t callback;
};

static struct dl_at_rsp_entry s_custom_rsps[CONFIG_ALIRO_AT_MODULE_MAX_CUSTOM_RSPS];
static size_t s_custom_rsp_count;

void dl_at_rsp_register(const char *filter, dl_at_rsp_callback_t cb)
{
	if (s_custom_rsp_count >= CONFIG_ALIRO_AT_MODULE_MAX_CUSTOM_RSPS) {
		LOG_ERR("DL_AT_RSP: max custom responses reached");
		return;
	}
	s_custom_rsps[s_custom_rsp_count].filter = filter;
	s_custom_rsps[s_custom_rsp_count].callback = cb;
	s_custom_rsp_count++;
}

static void dl_sync_str_rsp_handler(const uint8_t *data, size_t datalen)
{
	(void)datalen;
	LOG_INF("Received DL sync response: %.*s", (int)datalen, data);

	if (!s_dl_sync_armed.load(std::memory_order_relaxed)) {
		LOG_WRN("Received unexpected DL sync response");
		return;
	}

	if (strcmp((const char *)data, "Ready") != 0) {
		LOG_ERR("Received invalid DL sync response");
		return;
	}
	s_dl_sync_armed.store(false, std::memory_order_relaxed);
	k_sem_give(&dl_sync_sem);
	LOG_INF("DL sync complete");
}

DL_AT_RSP(dl_sync_ready, "Ready", dl_sync_str_rsp_handler);

static bool dispatch_at_line(const char *line)
{
	for (size_t i = 0; i < s_custom_rsp_count; i++) {
		size_t filter_len = strlen(s_custom_rsps[i].filter);
		if (strncmp(line, s_custom_rsps[i].filter, filter_len) == 0) {
			s_custom_rsps[i].callback((const uint8_t *)line, strlen(line));
			return true;
		}
	}
	return false;
}

extern "C" {

static void s_transport_state_cb(enum at_transport_state state)
{
	if (state == AT_TRANSPORT_CONNECTED) {
		LOG_INF("AT transport connected");
	} else {
		LOG_INF("AT transport disconnected");
	}
}

int at_host_init(void)
{
    LOG_INF("Initializing AT Host");
    int err = at_transport_enable(s_transport_state_cb);
    if (err) {
        LOG_ERR("Failed to enable AT transport: %d", err);
        return err;
    }
    at_host_initialized = true;
    at_cmd_state = AT_CMD_OK;
    LOG_INF("AT Host initialized successfully");
    return 0;
}

size_t at_process(const uint8_t *data, size_t len, bool *stop_at_receive)
{
	static char line_buf[DL_AT_LINE_BUF_SIZE];
	static size_t line_len;
	size_t consumed = 0;

	*stop_at_receive = true;

	/* When waiting for send_cmd result, accumulate and parse AT response. */
	if (at_cmd_state == AT_CMD_PENDING) {
		size_t copy_len = len;
		if (at_cmd_resp_len + copy_len > sizeof(at_cmd_resp_buf)) {
			copy_len = sizeof(at_cmd_resp_buf) - at_cmd_resp_len;
		}
		memcpy(at_cmd_resp_buf + at_cmd_resp_len, data, copy_len);
		at_cmd_resp_len += copy_len;

		size_t processed = parse_at_response((const char *)at_cmd_resp_buf, at_cmd_resp_len);
		if (processed > 0) {
			if (s_rsp_buf && s_rsp_size > 0) {
				size_t copy_len = processed < s_rsp_size ? processed : s_rsp_size - 1;
				memcpy(s_rsp_buf, at_cmd_resp_buf, copy_len);
				s_rsp_buf[copy_len] = '\0';
				s_rsp_buf = NULL;
				s_rsp_size = 0;
			}
			if (processed < at_cmd_resp_len) {
				memmove(at_cmd_resp_buf, at_cmd_resp_buf + processed,
					at_cmd_resp_len - processed);
			}
			at_cmd_resp_len -= processed;
			k_sem_give(&at_rsp_sem);
			/* Feed remainder into line processing so no data is dropped */
			for (size_t i = 0; i < at_cmd_resp_len; i++) {
				char c = (char)at_cmd_resp_buf[i];
				if (c == '\r' || c == '\n') {
					if (line_len > 0) {
						line_buf[line_len] = '\0';
						(void)dispatch_at_line(line_buf);
						line_len = 0;
					}
				} else if (line_len < sizeof(line_buf) - 1) {
					line_buf[line_len++] = c;
				}
			}
			at_cmd_resp_len = 0;
		}
		consumed = copy_len;
	}

	while (consumed < len) {
		char c = (char)data[consumed];
		if (c == '\r' || c == '\n') {
			if (line_len > 0) {
				line_buf[line_len] = '\0';
				if (!dispatch_at_line(line_buf)) {
					/* No custom handler matched; */
				}
				line_len = 0;
			}
			consumed++;
			continue;
		}
		if (line_len < sizeof(line_buf) - 1) {
			line_buf[line_len++] = c;
		}
		consumed++;
	}
	return len;
}

int at_host_send_cmd_with_rsp(const char *command, uint32_t timeout,
				char *rsp_buf, size_t rsp_size)
{
	int ret;
	size_t len;

	if (!at_host_initialized) {
		LOG_ERR("AT Host not initialized");
		return -EPERM;
	}
	if (!command) {
		LOG_ERR("Invalid AT command");
		return -EINVAL;
	}
	len = strlen(command);
	if (len > DL_AT_LINE_BUF_SIZE) {
		LOG_ERR("AT command too long");
		return -EINVAL;
	}
	/* Ensure command ends with \r\n */
	if (len < 2 || command[len - 2] != '\r' || command[len - 1] != '\n') {
		LOG_ERR("AT command must end with \\r\\n");
		return -EINVAL;
	}

	s_rsp_buf = rsp_buf;
	s_rsp_size = rsp_buf ? rsp_size : 0;

	at_cmd_state = AT_CMD_PENDING;
	at_cmd_resp_len = 0;

	ret = at_send((const uint8_t *)command, len);
	if (ret < 0) {
		LOG_ERR("Failed to send AT command");
		at_cmd_state = AT_CMD_ERROR;
		s_rsp_buf = NULL;
		s_rsp_size = 0;
		return ret;
	}

	if (timeout != 0) {
		ret = k_sem_take(&at_rsp_sem, K_SECONDS(timeout));
	} else {
		ret = k_sem_take(&at_rsp_sem, K_FOREVER);
	}

	s_rsp_buf = NULL;
	s_rsp_size = 0;

	if (ret != 0) {
		LOG_ERR("AT command timeout");
		at_cmd_state = AT_CMD_ERROR;
		return -EAGAIN;
	}

	return (int)at_cmd_state;
}

int at_host_send_cmd(const char *command, uint32_t timeout)
{
	return at_host_send_cmd_with_rsp(command, timeout, NULL, 0);
}

int at_host_send_reset_cmd(void)
{
	if (!at_host_initialized) {
		LOG_ERR("AT Host not initialized");
		return -EPERM;
	}
	return at_send((const uint8_t *)DL_RESET_CMD, strlen(DL_RESET_CMD));
}

int at_host_send_reset_wait_dl_sync(uint32_t sync_timeout_sec)
{
	if (!at_host_initialized) {
		LOG_ERR("AT Host not initialized");
		return -EPERM;
	}

	k_sem_reset(&dl_sync_sem);
	s_dl_sync_armed.store(true, std::memory_order_relaxed);

	int ret = at_host_send_reset_cmd();
	if (ret < 0) {
		s_dl_sync_armed.store(false, std::memory_order_relaxed);
		return ret;
	}

	ret = k_sem_take(&dl_sync_sem, K_SECONDS(sync_timeout_sec));
	s_dl_sync_armed.store(false, std::memory_order_relaxed);
	if (ret != 0) {
		LOG_ERR("Timeout waiting for DL_SYNC_STR after AT+RESET");
		return -ETIMEDOUT;
	}
	return 0;
}

bool at_reader_private_key_is_set(void)
{
	char buf[128];
	int ret;

	ret = at_host_send_cmd_with_rsp(DL_READERKEY_CMD, DL_READERKEY_TIMEOUT,
					buf, sizeof(buf));
	if (ret != AT_CMD_OK) {
		LOG_WRN("AT+READERKEY? failed or timeout: %d", ret);
		return false;
	}
	return (strstr(buf, "#READERKEY: 1") != NULL);
}

bool at_reader_start(void)
{
	int ret;

	ret = at_host_send_cmd(DL_START_CMD_STR, DL_START_CMD_TIMEOUT);
	if (ret != AT_CMD_OK) {
		LOG_WRN("AT+ALIROSTART failed or timeout: %d", ret);
		return false;
	}
	return true;
}

bool at_reader_private_key_set(const uint8_t *private_key, size_t key_size)
{	char cmd_buf[DL_AT_LINE_BUF_SIZE];
	char private_key_str[READER_KEY_SIZE * 2 + 1];
	int ret;

	if (!private_key || key_size != READER_KEY_SIZE) {
		return false;
	}
	for (size_t i = 0; i < key_size; i++) {
		sprintf(&private_key_str[i * 2], "%02x", private_key[i]);
	}
	private_key_str[key_size * 2] = '\0';

	ret = snprintf(cmd_buf, sizeof(cmd_buf), "AT+READERKEY=1,\"%s\"\r\n", private_key_str);
	if (ret < 0 || (size_t)ret >= sizeof(cmd_buf)) {
		LOG_ERR("Failed to format AT command");
		return false;
	}

	ret = at_host_send_cmd(cmd_buf, DL_READERKEY_TIMEOUT);
	if (ret != AT_CMD_OK) {
		LOG_WRN("AT+READERKEY=1 failed or timeout: %d", ret);
		return false;
	}
	return true;
}

static int hex_char_to_nibble(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	return -1;
}

int at_reader_public_key_get(uint8_t *key_out, size_t key_size)
{
	char buf[DL_AT_CMD_RESP_BUF_SIZE];
	const char *prefix = "#READERPUBLICKEY: 1,";
	const size_t prefix_len = strlen(prefix);
	int ret;

	if (!key_out || key_size != READER_PUBLIC_KEY_SIZE) {
		return -EINVAL;
	}

	ret = at_host_send_cmd_with_rsp(DL_READERPUBLICKEY_CMD, DL_READERPUBLICKEY_TIMEOUT,
					buf, sizeof(buf));
	if (ret != AT_CMD_OK) {
		LOG_WRN("AT+READERPUBLICKEY? failed or timeout: %d", ret);
		return -EAGAIN;
	}

	const char *p = strstr(buf, prefix);
	if (!p) {
		LOG_WRN("AT+READERPUBLICKEY? response missing or key not set");
		return -ENODATA;
	}

	p += prefix_len;
	p += 1; /* Skip opening quote */
	/* Need 130 hex chars for 65 bytes */
	for (size_t i = 0; i < READER_PUBLIC_KEY_SIZE; i++) {
		int hi = hex_char_to_nibble(p[i * 2]);
		int lo = hex_char_to_nibble(p[i * 2 + 1]);
		if (hi < 0 || lo < 0) {
			LOG_ERR("Invalid hex in READERPUBLICKEY response");
			return -EINVAL;
		}
		key_out[i] = (uint8_t)((hi << 4) | lo);
	}

	return 0;
}

int at_reader_identifier_create(const uint8_t *group_identifier, size_t group_identifier_size)
{
	char cmd_buf[DL_AT_LINE_BUF_SIZE];
	char reader_id_hex_str[READER_ID_SIZE * 2 + 1];
	int ret;

	if (!group_identifier || group_identifier_size != READER_ID_SIZE) {
		return -EINVAL;
	}
	for (size_t i = 0; i < READER_ID_SIZE; i++) {
		sprintf(&reader_id_hex_str[i * 2], "%02x", group_identifier[i]);
	}
	reader_id_hex_str[READER_ID_SIZE * 2] = '\0';

	ret = snprintf(cmd_buf, sizeof(cmd_buf), "AT+READERID=1,\"%s\"\r\n", reader_id_hex_str);
	if (ret < 0 || (size_t)ret >= sizeof(cmd_buf)) {
		LOG_ERR("Failed to format AT+READERID command");
		return -ENOMEM;
	}

	ret = at_host_send_cmd(cmd_buf, DL_READERID_TIMEOUT);
	if (ret != AT_CMD_OK) {
		LOG_WRN("AT+READERID=1 failed or timeout: %d", ret);
		return -EAGAIN;
	}
	return 0;
}

int at_reader_identifier_get(uint8_t *identifier_out, size_t identifier_size)
{
	char buf[DL_AT_CMD_RESP_BUF_SIZE];
	/* Match at_command.cpp response format: "#READERID: 1," (with space after colon) */
	const char *prefix = "#READERID: 1,";
	const size_t prefix_len = strlen(prefix);
	const size_t full_identifier_size = READER_ID_SIZE + READER_SUBID_SIZE;
	int ret;

	if (!identifier_out) {
		LOG_ERR("Invalid output buffer for reader identifier");
		return -EINVAL;
	}

	if (identifier_size != full_identifier_size) {
		LOG_ERR("Invalid output buffer size for reader identifier");
		LOG_ERR("Expected %zu bytes, got %zu bytes", full_identifier_size, identifier_size);
		return -EINVAL;
	}

	ret = at_host_send_cmd_with_rsp(DL_READERID_CMD, DL_READERID_TIMEOUT,
						buf, sizeof(buf));
	if (ret != AT_CMD_OK) {
		LOG_WRN("AT+READERID? failed or timeout: %d", ret);
		return -EAGAIN;
	}

	const char *p = strstr(buf, prefix);
	if (!p) {
		LOG_WRN("AT+READERID? response missing or identifier not set");
		return -ENODATA;
	}

	p += prefix_len;
	p += 1; /* Skip opening quote */
	/* Parse 64 hex chars: READERID (16 bytes) + READERSUBID (16 bytes) */
	for (size_t i = 0; i < full_identifier_size; i++) {
		int hi = hex_char_to_nibble(p[i * 2]);
		int lo = hex_char_to_nibble(p[i * 2 + 1]);
		if (hi < 0 || lo < 0) {
			LOG_ERR("Invalid hex in READERID response");
			return -EINVAL;
		}
		identifier_out[i] = (uint8_t)((hi << 4) | lo);
	}

	return 0;
}

int at_aliro_expedited_version_get(uint8_t *version_out, size_t version_out_max, size_t *version_count)
{
	char buf[DL_AT_CMD_RESP_BUF_SIZE];
	const char *prefix = "#ALIROEXPEDITEDVER:";
	const size_t prefix_len = strlen(prefix);
	int ret;

	if (!version_out || !version_count || version_out_max < 2U) {
		return -EINVAL;
	}

	ret = at_host_send_cmd_with_rsp(DL_ALIRO_EXPEDITED_VERSION_CMD,
					  DL_ALIRO_EXPEDITED_VERSION_TIMEOUT,
					  buf, sizeof(buf));
	if (ret != AT_CMD_OK) {
		LOG_WRN("AT+ALIROEXPEDITEDVER? failed or timeout: %d", ret);
		return -EAGAIN;
	}

	const char *p = strstr(buf, prefix);
	if (!p) {
		LOG_WRN("AT+ALIROEXPEDITEDVER? response missing prefix");
		return -ENODATA;
	}

	p += prefix_len;
	while (*p == ' ' || *p == '\t') {
		p++;
	}

	*version_count = 0;
	size_t max_versions = version_out_max / 2U;

	while (*p != '\0' && *p != '\r' && *p != '\n' && *version_count < max_versions) {
		unsigned int val;
		int n;

		if (sscanf(p, "%4x%n", &val, &n) != 1 || val > 0xFFFF) {
			LOG_WRN("Invalid hex in ALIROEXPEDITEDVER response");
			return -EINVAL;
		}
		version_out[(*version_count) * 2]     = (uint8_t)(val >> 8);
		version_out[(*version_count) * 2 + 1] = (uint8_t)(val & 0xFF);
		(*version_count)++;
		p += n;
		while (*p == ',') {
			p++;
		}
	}

	return 0;
}

bool at_amcmgr_add_public_key(uint16_t index, uint8_t type, const uint8_t *public_key)
{
	char cmd_buf[DL_AT_LINE_BUF_SIZE];
	char public_key_hex_str[DL_AMCMGR_PUBLIC_KEY_SIZE * 2 + 1];
	int ret;

	if (!public_key) {
		return false;
	}
	for (size_t i = 0; i < DL_AMCMGR_PUBLIC_KEY_SIZE; i++) {
		sprintf(&public_key_hex_str[i * 2], "%02x", public_key[i]);
	}
	public_key_hex_str[DL_AMCMGR_PUBLIC_KEY_SIZE * 2] = '\0';

	ret = snprintf(cmd_buf, sizeof(cmd_buf), "%s%u,%u,\"%s\"\r\n",
		       DL_AMCMGR_ADD_CMD, (unsigned int)index, (unsigned int)type, public_key_hex_str);
	if (ret < 0 || (size_t)ret >= sizeof(cmd_buf)) {
		LOG_ERR("Failed to format AT+AMCMGR=1 command");
		return false;
	}

	ret = at_host_send_cmd(cmd_buf, DL_AMCMGR_TIMEOUT);
	if (ret != AT_CMD_OK) {
		LOG_WRN("AT+AMCMGR=1 failed or timeout: %d", ret);
		return false;
	}
	return true;
}

bool at_amcmgr_remove_public_key(uint16_t index, uint8_t type)
{
	char cmd_buf[DL_AT_LINE_BUF_SIZE];
	int ret;

	ret = snprintf(cmd_buf, sizeof(cmd_buf), "%s%u,%u\r\n",
		       DL_AMCMGR_REMOVE_CMD, (unsigned int)index, (unsigned int)type);
	if (ret < 0 || (size_t)ret >= sizeof(cmd_buf)) {
		LOG_ERR("Failed to format AT+AMCMGR=0 command");
		return false;
	}

	ret = at_host_send_cmd(cmd_buf, DL_AMCMGR_TIMEOUT);
	if (ret != AT_CMD_OK) {
		LOG_WRN("AT+AMCMGR=0 failed or timeout: %d", ret);
		return false;
	}
	return true;
}

bool at_aliro_lock(Aliro::OperationSource source)
{
	char cmd_buf[DL_AT_LINE_BUF_SIZE];
	int ret;

	ret = snprintf(cmd_buf, sizeof(cmd_buf), "AT+ALIROLOCK=%" PRIu8 "\r\n", (uint8_t)source);
	if (ret < 0 || (size_t)ret >= sizeof(cmd_buf)) {
		LOG_ERR("Failed to format AT+ALIROLOCK command");
		return false;
	}
	ret = at_host_send_cmd(cmd_buf, DL_LOCKUNLOCK_TIMEOUT);
	if (ret != AT_CMD_OK) {
		LOG_WRN("AT+ALIROLOCK failed or timeout: %d", ret);
		return false;
	}

	return true;
}

bool at_aliro_unlock(Aliro::OperationSource source)
{
	char cmd_buf[DL_AT_LINE_BUF_SIZE];
	int ret;

	ret = snprintf(cmd_buf, sizeof(cmd_buf), "AT+ALIROUNLOCK=%" PRIu8 "\r\n", (uint8_t)source);
	if (ret < 0 || (size_t)ret >= sizeof(cmd_buf)) {
		LOG_ERR("Failed to format AT+ALIROUNLOCK command");
		return false;
	}
	ret = at_host_send_cmd(cmd_buf, DL_LOCKUNLOCK_TIMEOUT);
	if (ret != AT_CMD_OK) {
		LOG_WRN("AT+ALIROUNLOCK failed or timeout: %d", ret);
		return false;
	}

	return true;
}

int at_send_time_response(uint16_t year, uint8_t month, uint8_t day,
			  uint8_t hour, uint8_t minute, uint8_t second)
{
	char buf[DL_AT_LINE_BUF_SIZE];
	int ret;

	ret = snprintf(buf, sizeof(buf), "AT+TIME=1,\"%04u-%02u-%02u\",\"%02u:%02u:%02u\"\r\n",
		       (unsigned int)year, (unsigned int)month, (unsigned int)day,
		       (unsigned int)hour, (unsigned int)minute, (unsigned int)second);
	if (ret < 0 || (size_t)ret >= sizeof(buf)) {
		LOG_ERR("Failed to format AT+TIME response");
		return -ENOMEM;
	}
	return at_send_str(buf);
}

}  // extern "C"