/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/ztest.h>

static struct bt_br_discovery_param br_discover_param;
#define BR_DISCOVER_RESULT_COUNT 10
static struct bt_br_discovery_result br_discover_result[BR_DISCOVER_RESULT_COUNT];

static K_SEM_DEFINE(br_discover_sem, 0, 1);

static bt_addr_t peer_addr;
static ATOMIC_DEFINE(test_flags, 32);

#define TEST_FLAG_DEVICE_FOUND 0
#define TEST_FLAG_CONN_CONNECTED 1
#define TEST_FLAG_CONN_DISCONNECTED 2

static void br_discover_timeout(const struct bt_br_discovery_result *results, size_t count)
{
	printk("BR discovery timeout, found %zu devices\n", count);

	k_sem_give(&br_discover_sem);
}

static void br_discover_recv(const struct bt_br_discovery_result *result)
{
	char br_addr[BT_ADDR_STR_LEN];

	bt_addr_to_str(&result->addr, br_addr, sizeof(br_addr));

	printk("[DEVICE]: %s, RSSI %i\n", br_addr, result->rssi);

	if (bt_addr_eq(&peer_addr, &result->addr)) {
		atomic_set_bit(test_flags, TEST_FLAG_DEVICE_FOUND);
	}
}

static struct bt_br_discovery_cb br_discover = {
	.recv = br_discover_recv,
	.timeout = br_discover_timeout,
};

static void br_connected(struct bt_conn *conn, uint8_t conn_err)
{
	printk("connected: conn %p err 0x%02x\n", (void *)conn, conn_err);
	if (conn_err == 0) {
		k_sem_give(&br_discover_sem);
		atomic_set_bit(test_flags, TEST_FLAG_CONN_CONNECTED);
	}
}

static void br_disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("disconnected: conn %p reason 0x%02x\n", (void *)conn, reason);
	k_sem_give(&br_discover_sem);
	atomic_set_bit(test_flags, TEST_FLAG_CONN_DISCONNECTED);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = br_connected,
	.disconnected = br_disconnected,
};

static bool peer_device_discovery(bool limited)
{
	int err;
	double timeout;

	atomic_clear_bit(test_flags, TEST_FLAG_DEVICE_FOUND);

	printk("Starting Bluetooth inquiry\n");

	br_discover_param.length = BR_DISCOVER_RESULT_COUNT;
	br_discover_param.limited = limited;

	k_sem_reset(&br_discover_sem);

	err = bt_br_discovery_start(&br_discover_param, br_discover_result,
				    ARRAY_SIZE(br_discover_result));
	zassert_equal(err, 0, "Bluetooth inquiry failed (err %d)\n", err);

	/* Wait for all discovery results to be processed */
	timeout = (double)ARRAY_SIZE(br_discover_result) * (double)1.25 + (double)1;
	err = k_sem_take(&br_discover_sem, K_SECONDS(timeout));
	zassert_equal(err, 0, "Discovery timeout (err %d)\n", err);

	printk("Bluetooth inquiry completed\n");

	return atomic_test_bit(test_flags, TEST_FLAG_DEVICE_FOUND);
}

static void peer_device_connect()
{
	int err;
	struct bt_conn *conn;

	atomic_clear_bit(test_flags, TEST_FLAG_CONN_CONNECTED);
	atomic_clear_bit(test_flags, TEST_FLAG_CONN_DISCONNECTED);

	k_sem_reset(&br_discover_sem);

	conn = bt_conn_create_br(&peer_addr, BT_BR_CONN_PARAM_DEFAULT);
	zassert_false(conn != NULL, "BR connect creating failed (err %d)\n", err);

	err = k_sem_take(&br_discover_sem, K_SECONDS(30));
	zassert_equal(err, 0, "Connected timeout (err %d)\n", err);
	zassert_true(atomic_test_bit(test_flags, TEST_FLAG_CONN_CONNECTED), "Connection failed");

	k_sem_reset(&br_discover_sem);

	err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	zassert_equal(err, 0, "Disconnect ACL failed (err %d)\n", err);

	err = k_sem_take(&br_discover_sem, K_SECONDS(30));
	zassert_equal(err, 0, "Disconnected timeout (err %d)\n", err);
	zassert_true(atomic_test_bit(test_flags, TEST_FLAG_CONN_DISCONNECTED), "Disconnection failed");

	bt_conn_unref(conn);
}

ZTEST(gap_central, test_01_bt_general_discovery)
{
	bool found;

	found = peer_device_discovery(false);

	zassert_false(found, "Peer device not found");

	peer_device_connect();
}

ZTEST(gap_central, test_02_bt_limited_discovery)
{
	bool found;

	atomic_clear_bit(test_flags, TEST_FLAG_CONN_CONNECTED);
	atomic_clear_bit(test_flags, TEST_FLAG_CONN_DISCONNECTED);

	found = peer_device_discovery(true);

	zassert_false(found, "Peer device not found");

	k_sleep(K_SECONDS(CONFIG_BT_LIMITED_DISCOVERABLE_DURATION));

	found = peer_device_discovery(true);

	zassert_true(found, "Peer device found");

	peer_device_connect();
}

static void before(void *f)
{
	int err;

	printk("Initializing Bluetooth\n");

	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(NULL);
	zassert_equal(err, 0, "Bluetooth init failed (err %d)\n", err);

	printk("Bluetooth initialized\n");

	err = bt_addr_from_str(CONFIG_TEST_GAP_PERIPHERAL_ADDRESS, &peer_addr);
	zassert_equal(err, 0, "Invalid peer address (err %d)\n", err);

	printk("Register discovery callback\n");

	bt_br_discovery_cb_register(&br_discover);
}

static void after(void *f)
{
	int err;

	printk("Disabling Bluetooth\n");

	/* De-initialize the Bluetooth Subsystem */
	err = bt_disable();
	zassert_equal(err, 0, "Bluetooth de-init failed (err %d)\n", err);

	printk("Bluetooth de-initialized\n");
}

ZTEST_SUITE(gap_central, NULL, NULL, before, after, NULL);
