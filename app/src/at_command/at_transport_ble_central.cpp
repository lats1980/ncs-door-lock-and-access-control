/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * AT transport over BLE Central: NUS client, scan/connect/pair (see Nordic central_uart sample).
 */

#include "at_transport.h"
#include "at_command.h"
#include "at_host.h"

#include <errno.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <bluetooth/gatt_dm.h>
#include <bluetooth/scan.h>
#include <bluetooth/services/nus.h>
#include <bluetooth/services/nus_client.h>

LOG_MODULE_REGISTER(at_transport_ble, CONFIG_DOOR_LOCK_APP_LOG_LEVEL);

#define NUS_WRITE_TIMEOUT K_MSEC(150)

namespace at_transport_ble {

static struct bt_nus_client nus_client;
static struct bt_conn *at_nus_conn;
static K_SEM_DEFINE(nus_init_sem, 0, 1);
static K_MUTEX_DEFINE(nus_tx_mutex);
static uint8_t nus_tx_buf[CONFIG_ALIRO_AT_COMMAND_BUF_SIZE];

static void scan_work_handler(struct k_work *work);
K_WORK_DEFINE(scan_work, scan_work_handler);

static bool central_started;
static bool conn_cb_registered;
static bool nus_client_inited;
static bool scan_module_inited;
#if defined(CONFIG_BT_SETTINGS)
static bool settings_loaded_once;
#endif

static void nus_sent_cb(struct bt_nus_client *nus, uint8_t err, const uint8_t *data, uint16_t len);

static uint8_t nus_received_cb(struct bt_nus_client *nus, const uint8_t *data, uint16_t len)
{
	ARG_UNUSED(nus);

	if (at_transport_rx(data, len) != 0) {
		LOG_WRN("Dropped NUS notification");
	}
	return BT_GATT_ITER_CONTINUE;
}

static void nus_sent_cb(struct bt_nus_client *nus, uint8_t err, const uint8_t *data, uint16_t len)
{
	ARG_UNUSED(nus);
	ARG_UNUSED(data);
	ARG_UNUSED(len);

	if (err) {
		LOG_WRN("NUS TX ATT error: 0x%02x", err);
	}
}

static void discovery_complete(struct bt_gatt_dm *dm, void *context)
{
	struct bt_nus_client *nus = (struct bt_nus_client *)context;
	int err;

	LOG_INF("NUS GATT discovery complete");

	err = bt_nus_handles_assign(dm, nus);
	if (err) {
		LOG_ERR("bt_nus_handles_assign failed (err %d)", err);
		bt_gatt_dm_data_release(dm);
		return;
	}

	err = bt_nus_subscribe_receive(nus);
	if (err) {
		LOG_ERR("Subscribe NUS TX failed (err %d)", err);
	}

	bt_gatt_dm_data_release(dm);

	at_host_update_event(AT_HOST_TRANSPORT_CONNECTED);
	k_sem_give(&nus_init_sem);
}

static void discovery_service_not_found(struct bt_conn *conn, void *context)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(context);
	LOG_WRN("NUS service not found on peer");
}

static void discovery_error(struct bt_conn *conn, int err, void *context)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(context);
	LOG_ERR("NUS GATT discovery error (%d)", err);
}

static struct bt_gatt_dm_cb discovery_cb = {
	.completed = discovery_complete,
	.service_not_found = discovery_service_not_found,
	.error_found = discovery_error,
};

static void gatt_discover(struct bt_conn *conn)
{
	int err;

	if (conn != at_nus_conn) {
		return;
	}

	err = bt_gatt_dm_start(conn, BT_UUID_NUS_SERVICE, &discovery_cb, &nus_client);
	if (err) {
		LOG_ERR("GATT discovery start failed (err %d)", err);
	}
}

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_exchange_params *params)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(params);

	if (!err) {
		LOG_DBG("MTU exchange done");
	} else {
		LOG_WRN("MTU exchange failed (err %u)", err);
	}
}

static void on_connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		LOG_WRN("Connect failed %s err 0x%02x %s", addr, conn_err, bt_hci_err_to_str(conn_err));
		if (at_nus_conn == conn) {
			bt_conn_unref(at_nus_conn);
			at_nus_conn = nullptr;
			(void)k_work_submit(&scan_work);
		}
		return;
	}

	if (conn != at_nus_conn) {
		return;
	}

	LOG_INF("AT NUS central connected: %s", addr);

	static struct bt_gatt_exchange_params exchange_params;

	exchange_params.func = mtu_exchange_cb;
	err = bt_gatt_exchange_mtu(conn, &exchange_params);
	if (err) {
		LOG_WRN("MTU exchange start failed (err %d)", err);
	}

	err = bt_conn_set_security(conn, BT_SECURITY_L2);
	if (err) {
		LOG_WRN("Set security L2 failed (err %d), discovering anyway", err);
		gatt_discover(conn);
	}

	err = bt_scan_stop();
	if (err && err != -EALREADY) {
		LOG_ERR("Stop LE scan failed (err %d)", err);
	}
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (at_nus_conn != conn) {
		return;
	}

	LOG_INF("AT NUS central disconnected: %s reason 0x%02x %s", addr, reason, bt_hci_err_to_str(reason));

	at_host_update_event(AT_HOST_TRANSPORT_DISCONNECTED);

	bt_conn_unref(at_nus_conn);
	at_nus_conn = nullptr;
	memset(&nus_client, 0, sizeof(nus_client));

	struct bt_nus_client_init_param init = {
		.cb =
			{
				.received = nus_received_cb,
				.sent = nus_sent_cb,
			},
	};
	(void)bt_nus_client_init(&nus_client, &init);

	(void)k_work_submit(&scan_work);
}

static void on_security_changed(struct bt_conn *conn, bt_security_t level, bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (conn != at_nus_conn) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		LOG_INF("AT NUS security changed: %s level %u", addr, static_cast<unsigned>(level));
	} else {
		LOG_WRN("AT NUS security failed: %s level %u err %d %s", addr, static_cast<unsigned>(level),
			static_cast<int>(err), bt_security_err_to_str(err));
	}

	if (err == BT_SECURITY_ERR_SUCCESS) {
		gatt_discover(conn);
	}
}

static struct bt_conn_cb at_nus_conn_callbacks = {
	.connected = on_connected,
	.disconnected = on_disconnected,
	.security_changed = on_security_changed,
};

static void scan_filter_match(struct bt_scan_device_info *device_info, struct bt_scan_filter_match *filter_match,
			      bool connectable)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));
	LOG_DBG("Scan filter match %s connectable %d", addr, connectable);
}

static void scan_connecting_error(struct bt_scan_device_info *device_info)
{
	ARG_UNUSED(device_info);
	LOG_WRN("Scan connect failed");
}

static void scan_connecting(struct bt_scan_device_info *device_info, struct bt_conn *conn)
{
	ARG_UNUSED(device_info);
	at_nus_conn = bt_conn_ref(conn);
}

BT_SCAN_CB_INIT(at_scan_cb, scan_filter_match, nullptr, scan_connecting_error, scan_connecting);

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Pairing cancelled: %s", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Pairing complete: %s bonded %d", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_WRN("Pairing failed: %s reason %u %s", addr, static_cast<unsigned>(reason),
		bt_security_err_to_str(reason));
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

static void try_add_bonded_addr(const struct bt_bond_info *info, void *user_data)
{
	int err;
	char addr[BT_ADDR_LE_STR_LEN];
	uint8_t *filter_mode = (uint8_t *)user_data;

	bt_addr_le_to_str(&info->addr, addr, sizeof(addr));

	struct bt_conn *existing = bt_conn_lookup_addr_le(BT_ID_DEFAULT, &info->addr);
	if (existing) {
		bt_conn_unref(existing);
		return;
	}

	err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_ADDR, &info->addr);
	if (err) {
		LOG_ERR("Address filter add failed (%d): %s", err, addr);
		return;
	}

	LOG_INF("Bonded NUS peer filter: %s", addr);
	*filter_mode |= BT_SCAN_ADDR_FILTER;
}

static int scan_start(void)
{
	int err;
	uint8_t filter_mode = 0;

	err = bt_scan_stop();
	if (err && err != -EALREADY) {
		LOG_ERR("bt_scan_stop failed (%d)", err);
		return err;
	}

	bt_scan_filter_remove_all();

	err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, BT_UUID_NUS_SERVICE);
	if (err) {
		LOG_ERR("UUID filter add failed (%d)", err);
		return err;
	}
	filter_mode |= BT_SCAN_UUID_FILTER;

	bt_foreach_bond(BT_ID_DEFAULT, try_add_bonded_addr, &filter_mode);

	err = bt_scan_filter_enable(filter_mode, false);
	if (err) {
		LOG_ERR("Scan filter enable failed (%d)", err);
		return err;
	}

	err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
	if (err) {
		LOG_ERR("Scan start failed (%d)", err);
		return err;
	}

	LOG_INF("AT NUS central: scanning");
	return 0;
}

static void scan_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	(void)scan_start();
}

static int nus_client_setup(void)
{
	struct bt_nus_client_init_param init = {
		.cb =
			{
				.received = nus_received_cb,
				.sent = nus_sent_cb,
			},
	};

	return bt_nus_client_init(&nus_client, &init);
}

static void scan_module_init(void)
{
	struct bt_scan_init_param scan_init = {
		.connect_if_match = true,
	};

	bt_scan_init(&scan_init);
	bt_scan_cb_register(&at_scan_cb);
}

static void register_auth_callbacks(void)
{
	int err;

	err = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (err == -EALREADY) {
		LOG_DBG("BLE auth callbacks already registered (e.g. Matter)");
	} else if (err) {
		LOG_WRN("bt_conn_auth_cb_register failed (%d)", err);
	}

	err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
	if (err) {
		LOG_WRN("bt_conn_auth_info_cb_register failed (%d)", err);
	}
}

static void start_at_central(void)
{
	int err;

	if (central_started) {
		LOG_ERR("AT central already started");
		return;
	}

	register_auth_callbacks();

	if (!conn_cb_registered) {
		err = bt_conn_cb_register(&at_nus_conn_callbacks);
		if (err) {
			LOG_ERR("bt_conn_cb_register failed (%d)", err);
			return;
		}
		conn_cb_registered = true;
	}

	if (!nus_client_inited) {
		err = nus_client_setup();
		if (err) {
			LOG_ERR("NUS client init failed (%d)", err);
			return;
		}
		nus_client_inited = true;
	}

	err = bt_enable(nullptr);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	if (!scan_module_inited) {
		scan_module_init();
		scan_module_inited = true;
	}

#if defined(CONFIG_BT_SETTINGS)
	if (!settings_loaded_once) {
		err = settings_load();
		if (err) {
			LOG_WRN("settings_load failed (%d)", err);
		}
		settings_loaded_once = true;
	}
#endif

	err = scan_start();
	if (err) {
		LOG_ERR("scan_start failed (%d)", err);
		return;
	}

	central_started = true;
}

} /* namespace at_transport_ble */

extern "C" {

int at_transport_enable(void)
{
	LOG_INF("Enabling AT transport over BLE Central");
	at_transport_ble::start_at_central();
	k_sem_take(&at_transport_ble::nus_init_sem, K_MSEC(5000));

	return 0;
}

int at_transport_tx(const uint8_t *data, size_t len)
{
	using namespace at_transport_ble;

	if (!data || len == 0 || len > CONFIG_ALIRO_AT_COMMAND_BUF_SIZE) {
		return -EINVAL;
	}

	if (!nus_client.conn) {
		LOG_WRN("NUS not connected");
		return -ENOTCONN;
	}

	k_mutex_lock(&nus_tx_mutex, K_FOREVER);
	memcpy(nus_tx_buf, data, len);

	int err = bt_nus_client_send(&nus_client, nus_tx_buf, (uint16_t)len);
	if (err) {
		k_mutex_unlock(&nus_tx_mutex);
		return err;
	}

	k_mutex_unlock(&nus_tx_mutex);

	return 0;
}

int at_transport_rx(const uint8_t *data, size_t len)
{
	return at_receive(data, len);
}

} /* extern "C" */
