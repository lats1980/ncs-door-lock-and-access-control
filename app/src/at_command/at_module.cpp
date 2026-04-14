
#include <aliro/aliro.h>
#include <aliro/interface.h>
#include <aliro/init.h>
#include "at_module.h"
#include "at_transport.h"

#include <modem/at_parser.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <string.h>

#include <stdio.h>

#include "reader.h"
#include "aliro/access_manager/access_manager.h"
#include "aliro/lock_sim/lock_sim_instance.h"

LOG_MODULE_REGISTER(at_module, CONFIG_DOOR_LOCK_APP_LOG_LEVEL);

#ifdef __cplusplus
extern "C" {
#endif

static bool s_transport_ready_notified;

static void dl_at_reset_reboot_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	sys_reboot(SYS_REBOOT_WARM);
}

static void at_module_ready_work_fn(struct k_work *work)
{
	int err;
	ARG_UNUSED(work);
	LOG_INF("AT Module ready work handler called");
	err = at_send_str(DL_SYNC_STR);
	if (err) {
		LOG_ERR("Failed to send DL_SYNC_STR on transport connect: %d", err);
	} else {
		LOG_INF("DL_SYNC_STR sent successfully, AT Module is ready");
		s_transport_ready_notified = true;
	}
}

K_WORK_DELAYABLE_DEFINE(dl_at_reset_reboot_work, dl_at_reset_reboot_work_fn);
K_WORK_DEFINE(dl_at_module_ready_work, at_module_ready_work_fn);

#ifndef CONFIG_ALIRO_AT_MODULE_MAX_CUSTOM_CMDS
#define CONFIG_ALIRO_AT_MODULE_MAX_CUSTOM_CMDS 16
#endif

struct dl_at_cmd_entry {
	const char *filter;
	dl_at_cmd_callback_t callback;
};

static struct dl_at_cmd_entry s_custom_cmds[CONFIG_ALIRO_AT_MODULE_MAX_CUSTOM_CMDS];
static size_t s_custom_cmd_count;

static void s_transport_state_cb(enum at_transport_state state)
{
	if (state == AT_TRANSPORT_CONNECTED) {
		LOG_INF("AT transport connected");
		if (!s_transport_ready_notified) {
			k_work_submit(&dl_at_module_ready_work);
		}
	} else {
		LOG_INF("AT transport disconnected");
	}
}

static K_SEM_DEFINE(at_time_sem, 0, 1);

/* Stored current time from AT+TIME=1,<date>,<time> (at_send_time_response). */
static struct dl_time s_dl_time;

void dl_at_cmd_register(const char *filter, dl_at_cmd_callback_t cb)
{
	if (s_custom_cmd_count >= CONFIG_ALIRO_AT_MODULE_MAX_CUSTOM_CMDS) {
		LOG_ERR("DL_AT_CMD: max custom commands reached");
		return;
	}
	s_custom_cmds[s_custom_cmd_count].filter = filter;
	s_custom_cmds[s_custom_cmd_count].callback = cb;
	s_custom_cmd_count++;
}

static bool dispatch_at_line(const char *line)
{
	size_t filter_len;
	int err;
	struct at_parser parser;
	size_t param_count = 0;
	enum at_parser_cmd_type cmd_type;

	for (size_t i = 0; i < s_custom_cmd_count; i++) {
		filter_len = strlen(s_custom_cmds[i].filter);
		if (strncmp(line, s_custom_cmds[i].filter, filter_len) == 0) {
			err = at_parser_init(&parser, line);
			if (err) {
				at_send_str(DL_ERROR_STR);
				return true;
			}
			err = at_parser_cmd_count_get(&parser, &param_count);
			if (err) {
				at_send_str(DL_ERROR_STR);
				return true;
			}
			err = at_parser_cmd_type_get(&parser, &cmd_type);
			if (err) {
				at_send_str(DL_ERROR_STR);
				return true;
			}
			err = s_custom_cmds[i].callback(cmd_type, &parser, (uint32_t)param_count);
			if (err == 0) {
				at_send_str(DL_OK_STR);
			} else {
				at_send_str(DL_ERROR_STR);
			}
			return true;
		}
	}
	return false;
}

int at_module_init(void)
{
    LOG_INF("Initializing AT Module");
    int err = at_transport_enable(s_transport_state_cb);
    if (err) {
        LOG_ERR("Failed to enable AT transport: %d", err);
        return err;
    }
    LOG_INF("AT Module initialized successfully");
    return 0;
}

size_t at_process(const uint8_t *data, size_t len, bool *stop_at_receive)
{
	static char line_buf[DL_AT_LINE_BUF_SIZE];
	static size_t line_len;
	size_t consumed = 0;

	*stop_at_receive = true;

	while (consumed < len) {
		char c = (char)data[consumed];
		if (c == '\r' || c == '\n') {
			if (line_len > 0) {
				line_buf[line_len] = '\0';
				if (!dispatch_at_line(line_buf)) {
					/* No custom handler matched; respond ERROR (or could pass through) */
					at_send_str(DL_ERROR_STR);
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

int at_module_send_urc(const char *urc)
{
	if (!urc) {
		return -EINVAL;
	}
	return at_send_str(urc);
}

int dl_notify_lock_state(Aliro::ReaderStateByte state)
{
	char urc_buf[32];
	int err = snprintf(urc_buf, sizeof(urc_buf), "\r\n+LOCKSTATE: %u\r\n", (uint8_t)state);
	if (err < 0 || (size_t)err >= sizeof(urc_buf)) {
		LOG_ERR("Failed to format lock state URC");
		return -EINVAL;
	}
	return at_module_send_urc(urc_buf);
}

int dl_send_time_req_urc(void)
{
	static const char time_req_urc[] = "\r\n+TIME: REQ\r\n";
	return at_module_send_urc(time_req_urc);
}

DL_AT_CMD(reset, "AT+RESET", handle_at_reset);
static int handle_at_reset(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			   uint32_t param_count)
{
	(void)cmd_type;
	(void)parser;
	(void)param_count;

	LOG_INF("AT+RESET: scheduling warm reboot");
	k_work_reschedule(&dl_at_reset_reboot_work, K_MSEC(250));
	return 0;
}

DL_AT_CMD(dlver, "AT+DLVER", handle_at_dlver);
static int handle_at_dlver(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			   uint32_t param_count)
{
	int ret = -EINVAL;
	char buf[80];

	if (cmd_type == AT_PARSER_CMD_TYPE_READ) {
		(void)snprintf(buf, sizeof(buf), "\r\n#DLVER: %s,%s\r\n",
			       DL_VERSION, NCS_VERSION_STRING);
		at_send_str(buf);
		ret = 0;
	}

	(void)parser;
	(void)param_count;
	return ret;
}

DL_AT_CMD(readerkey, "AT+READERKEY", handle_at_readerkey);
static int handle_at_readerkey(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			      uint32_t param_count)
{
	char buf[80];
	int ret;
	uint16_t op;
	AliroError err;

	(void)cmd_type;
	(void)param_count;
	LOG_INF("Received READERKEY command");

	if (cmd_type == AT_PARSER_CMD_TYPE_SET) {
		ret = at_parser_uint16_get(parser, 1, &op);
		if (ret) {
			return ret;
		}
		if (op == 1) {
			if (DoorLock::Storage::Reader::IsPrivateKeySet()) {
				LOG_INF("Reader private key is already set; Do not overwrite");
				at_send_str(DL_ERROR_STR);
				return -EALREADY;
			}
			if (param_count < 2) {
				LOG_ERR("Missing parameter for READERKEY");
				at_send_str(DL_ERROR_STR);
				return -EINVAL;
			}
			Aliro::CryptoTypes::PrivateKey privateKey{};
			char private_key_str[READER_KEY_SIZE * 2 + 1];
			size_t len = sizeof(private_key_str);
			ret = at_parser_string_get(parser, 2, private_key_str, &len);
			if (ret) {
				LOG_ERR("Failed to get READERKEY parameter");
				at_send_str(DL_ERROR_STR);
				return ret;
			}
			/* Convert hex string to binary */
			for (size_t i = 0; i < privateKey.size(); i++) {
				sscanf(&private_key_str[i * 2], "%2hhx", &privateKey[i]);
			}
			LOG_HEXDUMP_INF(privateKey.data(), privateKey.size(), "Setting reader private key");
			err = DoorLock::Storage::Reader::SetPrivateKey(privateKey);
			if (err == ALIRO_NO_ERROR) {
				LOG_INF("Reader private key set successfully");
				ret = 0;
			} else {
				LOG_ERR("Failed to set reader private key");
				at_send_str(DL_ERROR_STR);
				return -EIO;
			}
		} else if (op == 0) {
			err = DoorLock::Storage::Reader::ClearPrivateKey();
			if (err == ALIRO_NO_ERROR) {
				LOG_INF("Reader private key cleared successfully");
				ret = 0;
			} else {
				LOG_ERR("Failed to clear reader private key");
				at_send_str(DL_ERROR_STR);
				return -EIO;
			}
		} else {
			LOG_ERR("Invalid operation for READERKEY: %d", op);
			at_send_str(DL_ERROR_STR);
			return -EINVAL;
		}
	} else if (cmd_type == AT_PARSER_CMD_TYPE_READ) {
		if (DoorLock::Storage::Reader::IsPrivateKeySet()) {
			LOG_INF("Reader private key is set");
			sprintf(buf, "\r\n#READERKEY: 1\r\n");
		} else {
			LOG_INF("Reader private key is NOT set");
			sprintf(buf, "\r\n#READERKEY: 0\r\n");
		}
		at_send_str(buf);
	}

	return 0;
}

DL_AT_CMD(readerid, "AT+READERID", handle_at_readerid);
static int handle_at_readerid(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			     uint32_t param_count)
{
	int ret = -EINVAL;
	AliroError err;
	uint16_t op;
	char buf[DL_AT_CMD_RESP_BUF_SIZE];

	LOG_INF("Received READERID command: cmd_type=%d, param_count=%d", (int)cmd_type, (int)param_count);

	if (cmd_type == AT_PARSER_CMD_TYPE_SET) {
		ret = at_parser_uint16_get(parser, 1, &op);
		if (ret) {
			return ret;
		}
		if (op == 1) {
			Aliro::Identifier identifier{};
			if (!DoorLock::Storage::Reader::IsIdentifierSet()) {
				LOG_INF("Reader identifier is NOT set; creating new one");
			} else {
				LOG_INF("Reader identifier is already set; Do not overwre");
				return -EALREADY;
			}
			if (param_count < 2) {
				LOG_ERR("Missing 32-byte hex string for READERID (e.g. AT+READERID=1,\"00112233445566778899aabbccddeeff\")");
				at_send_str(DL_ERROR_STR);
				return -EINVAL;
			}
			char reader_id_hex_str[READER_ID_SIZE * 2 + 1];
			size_t hex_len = sizeof(reader_id_hex_str);
			ret = at_parser_string_get(parser, 2, reader_id_hex_str, &hex_len);
			if (ret) {
				LOG_ERR("Failed to get READERID hex string parameter");
				at_send_str(DL_ERROR_STR);
				return ret;
			}
			if (hex_len != READER_ID_SIZE * 2) {
				LOG_ERR("READERID must be 32 hex chars (16 bytes), got %d", (int)hex_len);
				at_send_str(DL_ERROR_STR);
				return -EINVAL;
			}
			/* Convert 32-char hex string to 16 bytes and copy to identifier.data() */
			for (size_t i = 0; i < READER_ID_SIZE; i++) {
				sscanf(&reader_id_hex_str[i * 2], "%2hhx", &identifier.data()[i]);
			}
			LOG_HEXDUMP_INF(identifier.data(), READER_ID_SIZE, "Reader ID (from AT)");
			err = Aliro::Interface::Crypto::GenerateRandom(identifier.data() + READER_ID_SIZE,
								READER_SUBID_SIZE);
			if (err != ALIRO_NO_ERROR) {
				LOG_ERR("Failed to generate random reader identifier: %d", err.ToInt());
				return -EIO;
			}
			err = DoorLock::Storage::Reader::SetIdentifier(identifier);
			if (err == ALIRO_NO_ERROR) {
				LOG_INF("Reader identifier set successfully");
				ret = 0;
			} else {
				LOG_ERR("Failed to set reader identifier");
				ret = -EIO;
			}
		} else if (op == 0) {
			err = DoorLock::Storage::Reader::ClearIdentifier();
			if (err == ALIRO_NO_ERROR) {
				LOG_INF("Reader identifier cleared successfully");
				ret = 0;
			} else {
				LOG_ERR("Failed to clear reader identifier");
				ret = -EIO;
			}
		} else {
			LOG_ERR("Invalid operation for READERID: %d", op);
			ret = -EINVAL;
		}
	}
	else if (cmd_type == AT_PARSER_CMD_TYPE_READ) {
		if (DoorLock::Storage::Reader::IsIdentifierSet()) {
			Aliro::Identifier identifier{};
			const auto status = DoorLock::Storage::Reader::GetIdentifier(identifier);
			if (status == ALIRO_NO_ERROR) {
				LOG_INF("Reader identifier is set");
				int len = snprintf(buf, sizeof(buf), "\r\n#READERID: 1,\"");
				/* First 16 bytes: READERID (set by AT_PARSER_CMD_TYPE_SET) */
				for (int i = 0; i < READER_ID_SIZE && len < (int)sizeof(buf) - 2; i++) {
					len += snprintf(buf + len, (size_t)(sizeof(buf) - len), "%02x",
						       identifier[i]);
				}
				/* Next 16 bytes: READERSUBID (random generated) */
				for (int i = 0; i < READER_SUBID_SIZE && len < (int)sizeof(buf) - 2; i++) {
					len += snprintf(buf + len, (size_t)(sizeof(buf) - len), "%02x",
						       identifier[READER_ID_SIZE + i]);
				}
				snprintf(buf + len, (size_t)(sizeof(buf) - len), "\"\r\n");
			} else {
				LOG_ERR("Failed to get reader identifier");
				sprintf(buf, "\r\n#READERID: 1,ERROR\r\n");
			}
		} else {
			LOG_INF("Reader identifier is NOT set");
			sprintf(buf, "\r\n#READERID: 0\r\n");
		}
		at_send_str(buf);
		ret = 0;
	}

	return ret;
}

DL_AT_CMD(readerpublickey, "AT+READERPUBLICKEY", handle_at_readerpublickey);
static int handle_at_readerpublickey(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				   uint32_t param_count)
{
	int ret = -EINVAL;
	char buf[DL_AT_CMD_RESP_BUF_SIZE];

	(void)cmd_type;
	(void)parser;
	(void)param_count;
	LOG_INF("Received READERPUBLICKEY command");

	if (cmd_type == AT_PARSER_CMD_TYPE_READ) {
		if (DoorLock::Storage::Reader::IsPrivateKeySet()) {
			Aliro::CryptoTypes::PublicKey publicKey{};
			const auto status = DoorLock::Storage::Reader::GetPublicKey(publicKey);
			if (status == ALIRO_NO_ERROR) {
				LOG_INF("Reader public key is set");
				int len = snprintf(buf, sizeof(buf), "\r\n#READERPUBLICKEY: 1,\"");
				for (int i = 0; i < READER_PUBLIC_KEY_SIZE && len < (int)sizeof(buf) - 2; i++) {
					len += snprintf(buf + len, (size_t)(sizeof(buf) - len), "%02x",
						       publicKey[i]);
				}
				snprintf(buf + len, (size_t)(sizeof(buf) - len), "\"\r\n");
				ret = at_send_str(buf);
			} else {
				LOG_ERR("Failed to get reader public key");
				snprintf(buf, sizeof(buf), "\r\n#READERPUBLICKEY: 1,ERROR\r\n");
				ret = at_send_str(buf);
			}
		} else {
			LOG_INF("Reader public key is NOT set");
			snprintf(buf, sizeof(buf), "\r\n#READERPUBLICKEY: 0\r\n");
			ret = at_send_str(buf);
		}
	}

	return ret;
}

DL_AT_CMD(alirostart, "AT+ALIROSTART", handle_at_alirostart);
static int handle_at_alirostart(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				uint32_t param_count)
{
	LOG_INF("Received ALIROSTART command");
	(void)cmd_type;
	(void)parser;
	(void)param_count;
	int err = AliroStart();
	if (err != 0) {
		LOG_ERR("Failed to start Aliro");
	}
	return err;
}

DL_AT_CMD(aliroexpeditedver, "AT+ALIROEXPEDITEDVER", handle_at_aliroexpeditedver);
static int handle_at_aliroexpeditedver(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				      uint32_t param_count)
{
	LOG_INF("Received ALIROEXPEDITEDVER command");
	(void)parser;
	(void)param_count;

	if (cmd_type == AT_PARSER_CMD_TYPE_READ) {
		size_t versionCount{};
		const auto *versions = Aliro::AliroStack::Instance().GetExpeditedStandardProtocolVersions(versionCount);
		/* "\r\n#ALIROEXPEDITEDVER:" (22) + versionCount * 5 (e.g. "0001,") - 1 + "\r\n\0" */
		char buf[22 + (versionCount * 5) + 4];
		int len;

		LOG_INF("Expedited Transaction Supported Protocol Versions count: %d", (int)versionCount);
		len = snprintf(buf, sizeof(buf), "\r\n#ALIROEXPEDITEDVER:");
		for (size_t i = 0; i < versionCount && len < (int)sizeof(buf) - 6; i++) {
			len += snprintf(buf + len, (size_t)(sizeof(buf) - len), "%s%04x",
				       i ? "," : "", (unsigned int)versions[i]);
		}
		snprintf(buf + len, (size_t)(sizeof(buf) - len), "\r\n");
		at_send_str(buf);
		return 0;
	}

	return -EINVAL;
}

/* Access Manager Credential Manager */
DL_AT_CMD(amcmgr, "AT+AMCMGR", handle_at_amcmgr);
static int handle_at_amcmgr(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			    uint32_t param_count)
{
	int ret = 0;
	uint16_t credentialIndex, credentialType;

	if (cmd_type == AT_PARSER_CMD_TYPE_SET) {
		uint16_t op;
		ret = at_parser_uint16_get(parser, 1, &op);
		if (ret) {
			return ret;
		}
		if (op == 1) {
			LOG_INF("AMCMGR AddPublicKey command received");
			ret = at_parser_uint16_get(parser, 2, &credentialIndex);
			if (ret) {
				LOG_ERR("Failed to get credential index for AMCMGR AddPublicKey");
				return ret;
			}
			ret = at_parser_uint16_get(parser, 3, &credentialType);
			if (ret) {
				LOG_ERR("Failed to get credential type for AMCMGR AddPublicKey");
				return ret;
			}
			char public_key_hex_str[DL_AMCMGR_PUBLIC_KEY_SIZE * 2 + 1];
			size_t hex_len = sizeof(public_key_hex_str);
			ret = at_parser_string_get(parser, 4, public_key_hex_str, &hex_len);
			if (ret) {
				LOG_ERR("Failed to get public key hex string parameter for AMCMGR AddPublicKey");
				return ret;
			}
			if (hex_len != DL_AMCMGR_PUBLIC_KEY_SIZE * 2) {
				LOG_ERR("Public key must be 130 hex chars (65 bytes) for AMCMGR AddPublicKey, got %d", (int)hex_len);
				return -EINVAL;
			}
			Aliro::CryptoTypes::PublicKey publicKey{};
			/* Convert 130-char hex string to 65 bytes and copy to publicKey.data() */
			for (size_t i = 0; i < publicKey.size(); i++) {
				sscanf(&public_key_hex_str[i * 2], "%2hhx", &publicKey[i]);
			}
			LOG_HEXDUMP_INF(publicKey.data(), publicKey.size(), "Adding public key to Access Manager");
			if ((enum Aliro::AccessManager::PublicKeyType)credentialType == Aliro::AccessManager::PublicKeyType::AccessCredential) {
				Aliro::AccessManagerInstance().AddPublicKey(
					publicKey, Aliro::AccessManager::PublicKeyType::AccessCredential, credentialIndex);
			} else if ((enum Aliro::AccessManager::PublicKeyType)credentialType == Aliro::AccessManager::PublicKeyType::AccessDocument) {
				Aliro::AccessManagerInstance().AddPublicKey(
					publicKey, Aliro::AccessManager::PublicKeyType::AccessDocument, credentialIndex);
			} else if ((enum Aliro::AccessManager::PublicKeyType)credentialType == Aliro::AccessManager::PublicKeyType::CredentialIssuer) {
				Aliro::AccessManagerInstance().AddPublicKey(
					publicKey, Aliro::AccessManager::PublicKeyType::CredentialIssuer, credentialIndex);
			} else {
				LOG_ERR("Invalid credential type for AMCMGR AddPublicKey: %d", credentialType);
				return -EINVAL;
			}
		} else if (op == 0) {
			LOG_INF("AMCMGR RemovePublicKey command received");
			ret = at_parser_uint16_get(parser, 2, &credentialIndex);
			if (ret) {
				LOG_ERR("Failed to get credential index for AMCMGR RemovePublicKey");
				return ret;
			}
			ret = at_parser_uint16_get(parser, 3, &credentialType);
			if (ret) {
				LOG_ERR("Failed to get credential type for AMCMGR RemovePublicKey");
				return ret;
			}
			if ((enum Aliro::AccessManager::PublicKeyType)credentialType == Aliro::AccessManager::PublicKeyType::AccessCredential) {
				Aliro::AccessManagerInstance().RemovePublicKey(
					Aliro::AccessManager::PublicKeyType::AccessCredential, credentialIndex);
			} else if ((enum Aliro::AccessManager::PublicKeyType)credentialType == Aliro::AccessManager::PublicKeyType::AccessDocument) {
				Aliro::AccessManagerInstance().RemovePublicKey(
					Aliro::AccessManager::PublicKeyType::AccessDocument, credentialIndex);
			} else if ((enum Aliro::AccessManager::PublicKeyType)credentialType == Aliro::AccessManager::PublicKeyType::CredentialIssuer) {
				Aliro::AccessManagerInstance().RemovePublicKey(
					Aliro::AccessManager::PublicKeyType::CredentialIssuer, credentialIndex);
#ifdef CONFIG_DOOR_LOCK_STEP_UP_PHASE
					LOG_INF("Clearing validity iterations for removed key with index %u and type %u", credentialIndex, (uint8_t)credentialType);
					Aliro::ClearValidityIterations(credentialIndex);
#endif // CONFIG_DOOR_LOCK_STEP_UP_PHASE
			} else {
				LOG_ERR("Invalid credential type for AMCMGR RemovePublicKey: %d", credentialType);
				return -EINVAL;
			}
		} else {
			LOG_ERR("Invalid operation for AMCMGR: %d", op);
			return -EINVAL;
		}
	}

	return 0;
}

DL_AT_CMD(alirolock, "AT+ALIROLOCK", handle_at_alirolock);
static int handle_at_alirolock(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			      uint32_t param_count)
{
	if (cmd_type == AT_PARSER_CMD_TYPE_SET)	{
		uint16_t source;
		if (param_count < 1) {
			LOG_ERR("Missing parameter for ALIROLOCK");
			return -EINVAL;
		}
		int ret = at_parser_uint16_get(parser, 1, &source);
		if (ret) {
			LOG_ERR("Failed to get operation source for ALIROLOCK");
			return ret;
		}
		LOG_INF("Locking via ALIROLOCK with source %d", source);
		if(!Aliro::LockSimInstance().Lock((Aliro::OperationSource)source))
		{
			LOG_ERR("Failed to lock via ALIROLOCK");
			return -EIO;
		}
		return 0;
	}
	return -EINVAL;
}

DL_AT_CMD(alirounlock, "AT+ALIROUNLOCK", handle_at_alirounlock);
static int handle_at_alirounlock(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				uint32_t param_count)
{
	if (cmd_type == AT_PARSER_CMD_TYPE_SET)	{
		uint16_t source;
		if (param_count < 1) {
			LOG_ERR("Missing parameter for ALIROUNLOCK");
			return -EINVAL;
		}
		int ret = at_parser_uint16_get(parser, 1, &source);
		if (ret) {
			LOG_ERR("Failed to get operation source for ALIROUNLOCK");
			return ret;
		}
		LOG_INF("Unlocking via ALIROUNLOCK with source %d", source);
		if(!Aliro::LockSimInstance().Unlock((Aliro::OperationSource)source))
		{
			LOG_ERR("Failed to unlock via ALIROUNLOCK");
			return -EIO;
		}
		return 0;
	}
	return -EINVAL;
}

DL_AT_CMD(time, "AT+TIME", handle_time);
static int handle_time(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
		       uint32_t param_count)
{
	int ret = -EINVAL;

	if (cmd_type != AT_PARSER_CMD_TYPE_SET || param_count < 3) {
		return ret;
	}

	uint16_t op;
	ret = at_parser_uint16_get(parser, 1, &op);
	if (ret) {
		return ret;
	}
	if (op != 1) {
		LOG_ERR("AT+TIME: unsupported op %u", op);
		return -EINVAL;
	}

	char date_str[16];
	char time_str[16];
	size_t date_len = sizeof(date_str);
	size_t time_len = sizeof(time_str);
	ret = at_parser_string_get(parser, 2, date_str, &date_len);
	if (ret) {
		LOG_ERR("AT+TIME: failed to get date parameter");
		return ret;
	}
	ret = at_parser_string_get(parser, 3, time_str, &time_len);
	if (ret) {
		LOG_ERR("AT+TIME: failed to get time parameter");
		return ret;
	}

	/* Parse YYYY-MM-DD and HH:MM:SS (format from at_send_time_response) */
	unsigned int y, mo, d, h, mi, s;
	if (sscanf(date_str, "%u-%u-%u", &y, &mo, &d) != 3 ||
	    sscanf(time_str, "%u:%u:%u", &h, &mi, &s) != 3) {
		LOG_ERR("AT+TIME: parse failed date=%s time=%s", date_str, time_str);
		return -EINVAL;
	}

	s_dl_time.year = (uint16_t)y;
	s_dl_time.month = (uint8_t)mo;
	s_dl_time.day = (uint8_t)d;
	s_dl_time.hour = (uint8_t)h;
	s_dl_time.minute = (uint8_t)mi;
	s_dl_time.second = (uint8_t)s;
	s_dl_time.valid = true;

	LOG_INF("AT+TIME: %04u-%02u-%02u %02u:%02u:%02u", s_dl_time.year, s_dl_time.month,
		s_dl_time.day, s_dl_time.hour, s_dl_time.minute, s_dl_time.second);
	k_sem_give(&at_time_sem);
	return 0;
}

int dl_time_get(uint8_t timeout, struct dl_time *out)
{
	int ret = -ENODATA;

	memset(&s_dl_time, 0, sizeof(s_dl_time));
	ret = dl_send_time_req_urc();
	if (ret) {
		LOG_ERR("Failed to send time request URC: %d", ret);
		return ret;
	}
	if (timeout != 0) {
		ret = k_sem_take(&at_time_sem, K_SECONDS(timeout));
	} else {
		ret = k_sem_take(&at_time_sem, K_FOREVER);
	}
	if (ret == 0) {
		if (s_dl_time.valid) {
			*out = s_dl_time;
			ret = 0;
		} else {
			ret = -ENODATA;
		}
	} else {
		LOG_ERR("Timeout waiting for time response");
		ret = -ETIMEDOUT;
	}
	return ret;
}

#ifdef __cplusplus
}  // extern "C"
#endif