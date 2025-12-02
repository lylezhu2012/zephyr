/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/ztest.h>

static K_SEM_DEFINE(br_discover_sem, 0, 1);

static bt_addr_t peer_addr;
static ATOMIC_DEFINE(test_flags, 32);

#define TEST_FLAG_CONN_CONNECTED 0
#define TEST_FLAG_CONN_DISCONNECTED 1

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

ZTEST(gap_peripheral, test_01_bt_general_discovery)
{
	int err;

	k_sem_reset(&br_discover_sem);

	err = bt_br_set_connectable(true);
	zassert_equal(err, 0, "Failed to set connectable (err %d)\n", err);

	err = bt_br_set_discoverable(true, false);
	zassert_equal(err, 0, "Failed to set discoverable (err %d)\n", err);

	err = k_sem_take(&br_discover_sem, K_SECONDS(30));
	zassert_equal(err, 0, "Connected timeout (err %d)\n", err);
	zassert_true(atomic_test_bit(test_flags, TEST_FLAG_CONN_CONNECTED), "Connection failed");

	k_sem_reset(&br_discover_sem);

	err = k_sem_take(&br_discover_sem, K_SECONDS(30));
	zassert_equal(err, 0, "Disconnected timeout (err %d)\n", err);
	zassert_true(atomic_test_bit(test_flags, TEST_FLAG_CONN_DISCONNECTED), "Disconnection failed");

	err = bt_br_set_discoverable(false, false);
	zassert_equal(err, 0, "Failed to clear discoverable (err %d)\n", err);

	err = bt_br_set_connectable(false);
	zassert_equal(err, 0, "Failed to clear connectable (err %d)\n", err);
}

ZTEST(gap_peripheral, test_02_bt_limited_discovery)
{
	int err;

	k_sem_reset(&br_discover_sem);

	err = bt_br_set_connectable(true);
	zassert_equal(err, 0, "Failed to set connectable (err %d)\n", err);

	err = bt_br_set_discoverable(true, true);
	zassert_equal(err, 0, "Failed to set discoverable (err %d)\n", err);

	err = k_sem_take(&br_discover_sem, K_SECONDS(30 + CONFIG_BT_LIMITED_DISCOVERABLE_DURATION));
	zassert_equal(err, 0, "Connected timeout (err %d)\n", err);
	zassert_true(atomic_test_bit(test_flags, TEST_FLAG_CONN_CONNECTED), "Connection failed");

	k_sem_reset(&br_discover_sem);

	err = k_sem_take(&br_discover_sem, K_SECONDS(30));
	zassert_equal(err, 0, "Disconnected timeout (err %d)\n", err);
	zassert_true(atomic_test_bit(test_flags, TEST_FLAG_CONN_DISCONNECTED), "Disconnection failed");

	err = bt_br_set_connectable(false);
	zassert_equal(err, 0, "Failed to clear connectable (err %d)\n", err);
}

static void before(void *f)
{
	int err;

	printk("Initializing Bluetooth\n");

	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(NULL);
	zassert_equal(err, 0, "Bluetooth init failed (err %d)\n", err);

	printk("Bluetooth initialized\n");

	err = bt_addr_from_str(CONFIG_TEST_GAP_CENTRAL_ADDRESS, &peer_addr);
	zassert_equal(err, 0, "Invalid peer address (err %d)\n", err);
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

ZTEST_SUITE(gap_peripheral, NULL, NULL, before, after, NULL);
