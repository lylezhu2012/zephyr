/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/classic/classic.h>
#include <zephyr/bluetooth/classic/l2cap_br.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include "test_br.h"

LOG_MODULE_REGISTER(test_br, CONFIG_TEST_BR_LOG_LEVEL);

#define POLL_INTERVAL K_MSEC(20)

/* Time for the peer to reach the next test, including its own ACL release. */
#define SYNC_TIMEOUT K_SECONDS(60)

/* Time the acceptor waits for the initiator to release the ACL link after a test. */
#define RELEASE_TIMEOUT K_SECONDS(30)

#define DISCONNECT_TIMEOUT K_SECONDS(10)

#define DISCOVERY_RESULT_COUNT 10

NET_BUF_POOL_FIXED_DEFINE(sync_pool, 2, BT_L2CAP_BUF_SIZE(4 + TEST_BR_SYNC_LEN),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

struct sync_msg {
	uint8_t ident;
	uint8_t len;
	char name[TEST_BR_SYNC_LEN];
};

K_MSGQ_DEFINE(sync_req_msgq, sizeof(struct sync_msg), 4, 4);
K_MSGQ_DEFINE(sync_rsp_msgq, sizeof(struct sync_msg), 4, 4);

static bool active;
static enum test_br_role role;
static bt_security_t required_sec = BT_SECURITY_L1;
static const char *current_test;

static struct bt_conn *default_conn;
static atomic_t conn_err;
static atomic_t sec_err;

/* A sync request of a later test, kept until this side reaches that test. */
static struct sync_msg pending_req;
static bool pending_valid;

static struct bt_br_discovery_param discovery_param;
static struct bt_br_discovery_result discovery_result[DISCOVERY_RESULT_COUNT];
static K_SEM_DEFINE(peer_found_sem, 0, 1);

static void acl_connected(struct bt_conn *conn, uint8_t err)
{
	struct bt_conn_info info;

	if ((bt_conn_get_info(conn, &info) != 0) || (info.type != BT_CONN_TYPE_BR)) {
		return;
	}

	LOG_DBG("ACL connected: conn %p err 0x%02x", (void *)conn, err);

	if (err != 0) {
		atomic_set(&conn_err, err);
		return;
	}

	if (default_conn == NULL) {
		default_conn = bt_conn_ref(conn);
	}
}

static void acl_disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_DBG("ACL disconnected: conn %p reason 0x%02x", (void *)conn, reason);

	if (conn == default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	LOG_DBG("Security changed: conn %p level %u err %d", (void *)conn, level, err);

	if ((conn == default_conn) && (err != BT_SECURITY_ERR_SUCCESS)) {
		atomic_set(&sec_err, err);
	}
}

BT_CONN_CB_DEFINE(test_br_conn_cb) = {
	.connected = acl_connected,
	.disconnected = acl_disconnected,
	.security_changed = security_changed,
};

static void sync_msg_from_buf(struct sync_msg *msg, uint8_t ident, struct net_buf *buf)
{
	msg->ident = ident;
	msg->len = MIN(buf->len, TEST_BR_SYNC_LEN);
	memcpy(msg->name, buf->data, msg->len);
}

static void echo_req(struct bt_conn *conn, uint8_t identifier, struct net_buf *buf)
{
	struct sync_msg msg;

	sync_msg_from_buf(&msg, identifier, buf);
	if (k_msgq_put(&sync_req_msgq, &msg, K_NO_WAIT) != 0) {
		LOG_WRN("Sync request queue full");
	}
}

static void echo_rsp(struct bt_conn *conn, struct net_buf *buf)
{
	struct sync_msg msg;

	sync_msg_from_buf(&msg, 0, buf);
	if (k_msgq_put(&sync_rsp_msgq, &msg, K_NO_WAIT) != 0) {
		LOG_WRN("Sync response queue full");
	}
}

static struct bt_l2cap_br_echo_cb echo_cb = {
	.req = echo_req,
	.rsp = echo_rsp,
};

static void discovery_recv(const struct bt_br_discovery_result *result)
{
	if (bt_addr_eq(&peer_addr, &result->addr)) {
		k_sem_give(&peer_found_sem);
	}
}

static struct bt_br_discovery_cb discovery_cb = {
	.recv = discovery_recv,
};

static void sync_token(const char *name, struct sync_msg *msg)
{
	msg->len = MIN(strlen(name), TEST_BR_SYNC_LEN);
	memcpy(msg->name, name, msg->len);
}

static int sync_cmp(const struct sync_msg *a, const struct sync_msg *b)
{
	int cmp = memcmp(a->name, b->name, MIN(a->len, b->len));

	if (cmp != 0) {
		return cmp;
	}

	return (int)a->len - (int)b->len;
}

static struct net_buf *sync_buf(const struct sync_msg *msg)
{
	struct net_buf *buf;

	buf = net_buf_alloc(&sync_pool, K_SECONDS(1));
	if (buf != NULL) {
		net_buf_reserve(buf, BT_L2CAP_BR_ECHO_REQ_RESERVE);
		net_buf_add_mem(buf, msg->name, msg->len);
	}

	return buf;
}

static void sync_initiator(const char *name)
{
	struct sync_msg own;
	struct sync_msg rsp;
	struct net_buf *buf;
	int err;

	sync_token(name, &own);
	k_msgq_purge(&sync_rsp_msgq);

	/* The signaling channel serves one request at a time; the Information Request sent after
	 * the ACL connection is established makes the stack log "Waiting for ECHO RSP" and return
	 * -EBUSY until it is answered.
	 */
	for (int i = 0; i < 50; i++) {
		buf = sync_buf(&own);
		zassert_not_null(buf, "No sync buffer");

		err = bt_l2cap_br_echo_req(default_conn, buf);
		if (err == 0) {
			break;
		}

		net_buf_unref(buf);
		if (err != -EBUSY) {
			break;
		}
		k_sleep(K_MSEC(200));
	}
	zassert_equal(err, 0, "Failed to send sync request (err %d)", err);

	err = -EAGAIN;
	for (k_timepoint_t end = sys_timepoint_calc(SYNC_TIMEOUT); !sys_timepoint_expired(end);) {
		err = k_msgq_get(&sync_rsp_msgq, &rsp, POLL_INTERVAL);
		if ((err == 0) || (default_conn == NULL)) {
			break;
		}
	}
	zassert_equal(err, 0, "No sync response from peer for %s", name);
	zassert_equal(sync_cmp(&rsp, &own), 0, "Peer is at test %.*s, this side at %s", rsp.len,
		      rsp.name, name);
}

static void sync_send_rsp(const struct sync_msg *req, const struct sync_msg *own)
{
	struct net_buf *buf;
	int err;

	if (default_conn == NULL) {
		return;
	}

	buf = sync_buf(own);
	if (buf == NULL) {
		LOG_ERR("No sync buffer");
		return;
	}

	err = bt_l2cap_br_echo_rsp(default_conn, req->ident, buf);
	if (err != 0) {
		LOG_ERR("Failed to send sync response (err %d)", err);
		net_buf_unref(buf);
	}
}

static void sync_acceptor(const char *name)
{
	k_timepoint_t end = sys_timepoint_calc(SYNC_TIMEOUT);
	struct sync_msg own;
	struct sync_msg req;
	int cmp;
	int err;

	sync_token(name, &own);

	while (true) {
		if (pending_valid) {
			req = pending_req;
			pending_valid = false;
		} else {
			err = k_msgq_get(&sync_req_msgq, &req, sys_timepoint_timeout(end));
			zassert_equal(err, 0, "No sync request from peer for %s", name);
		}

		cmp = sync_cmp(&req, &own);
		if (cmp == 0) {
			sync_send_rsp(&req, &own);
			return;
		}

		if (cmp < 0) {
			/* The peer is behind; it fails its test and connects again. */
			LOG_WRN("Peer is at earlier test %.*s, this side at %s", req.len, req.name,
				name);
			sync_send_rsp(&req, &own);
			continue;
		}

		/* The peer is ahead; answer it once this side reaches the same test. */
		pending_req = req;
		pending_valid = true;
		zassert_unreachable("Peer is at test %.*s, this side at %s", req.len, req.name,
				    name);
	}
}

static bool wait_for(bool (*cond)(void), k_timeout_t timeout)
{
	k_timepoint_t end = sys_timepoint_calc(timeout);

	while (!cond()) {
		if (sys_timepoint_expired(end)) {
			return false;
		}
		k_sleep(POLL_INTERVAL);
	}

	return true;
}

static bool is_connected(void)
{
	return default_conn != NULL;
}

static bool is_disconnected(void)
{
	return default_conn == NULL;
}

static bool is_released(void)
{
	struct bt_conn *conn = bt_conn_lookup_addr_br(&peer_addr);

	if (conn != NULL) {
		bt_conn_unref(conn);
		return false;
	}

	return true;
}

static bt_security_t wait_sec_level;

static bool is_secure(void)
{
	return (default_conn == NULL) || (atomic_get(&sec_err) != 0) ||
	       (bt_conn_get_security(default_conn) >= wait_sec_level);
}

enum test_br_role test_br_role(void)
{
	return role;
}

struct bt_conn *test_br_conn(void)
{
	return default_conn;
}

bool test_br_connected(void)
{
	return default_conn != NULL;
}

void test_br_sync(void)
{
	zassert_not_null(current_test, "No test running");
	zassert_not_null(default_conn, "ACL not connected");

	if (role == TEST_BR_INITIATOR) {
		sync_initiator(current_test);
	} else {
		sync_acceptor(current_test);
	}
}

int test_br_wait(struct k_sem *sem, k_timeout_t timeout)
{
	k_timepoint_t end = sys_timepoint_calc(timeout);

	while (k_sem_take(sem, POLL_INTERVAL) != 0) {
		if (default_conn == NULL) {
			return (k_sem_take(sem, K_NO_WAIT) == 0) ? 0 : -ENOTCONN;
		}

		if (sys_timepoint_expired(end)) {
			return -EAGAIN;
		}
	}

	return 0;
}

void test_br_connect(void)
{
	struct bt_conn *conn;

	zassert_is_null(default_conn, "ACL already connected");

	atomic_set(&conn_err, 0);
	conn = bt_conn_create_br(&peer_addr, BT_BR_CONN_PARAM_DEFAULT);
	zassert_not_null(conn, "Failed to create ACL connection");
	bt_conn_unref(conn);

	test_br_wait_connected(K_SECONDS(CONFIG_BT_CREATE_CONN_TIMEOUT));
}

void test_br_wait_connected(k_timeout_t timeout)
{
	bool ok = wait_for(is_connected, timeout);

	zassert_true(ok, "ACL connection timeout (err 0x%02lx)", atomic_get(&conn_err));
}

void test_br_disconnect(void)
{
	int err;

	zassert_not_null(default_conn, "ACL not connected");

	err = bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	zassert_equal(err, 0, "Failed to disconnect ACL (err %d)", err);

	test_br_wait_disconnected(DISCONNECT_TIMEOUT);
}

void test_br_wait_disconnected(k_timeout_t timeout)
{
	bool ok = wait_for(is_disconnected, timeout);

	zassert_true(ok, "ACL disconnection timeout");
}

void test_br_security(bt_security_t sec_level)
{
	int err;
	bool ok;

	zassert_not_null(default_conn, "ACL not connected");

	atomic_set(&sec_err, 0);
	if (role == TEST_BR_INITIATOR) {
		err = bt_conn_set_security(default_conn, sec_level);
		zassert_equal(err, 0, "Failed to set security (err %d)", err);
	}

	wait_sec_level = sec_level;
	ok = wait_for(is_secure, K_SECONDS(30));
	zassert_true(ok, "Security timeout");
	zassert_not_null(default_conn, "ACL lost during pairing");
	zassert_equal(atomic_get(&sec_err), 0, "Security failed (err %ld)", atomic_get(&sec_err));
}

void test_br_set_security(bt_security_t sec_level)
{
	required_sec = sec_level;
}

void test_br_setup(enum test_br_role test_role, bt_security_t sec_level)
{
	int err;

	role = test_role;
	required_sec = sec_level;
	pending_valid = false;

	err = bt_enable(NULL);
	zassert_equal(err, 0, "bt_enable failed (err %d)", err);

	err = bt_l2cap_br_echo_cb_register(&echo_cb);
	zassert_true((err == 0) || (err == -EEXIST), "Failed to register echo cb (err %d)", err);

	err = bt_br_set_connectable(true, NULL);
	zassert_equal(err, 0, "Failed to set connectable (err %d)", err);

	err = bt_br_set_discoverable(true, false);
	zassert_equal(err, 0, "Failed to set discoverable (err %d)", err);

	if (role == TEST_BR_INITIATOR) {
		/* The peer is discoverable once its host is up and able to accept connections. */
		bt_br_discovery_cb_register(&discovery_cb);

		discovery_param.length = DISCOVERY_RESULT_COUNT;
		discovery_param.limited = false;
		err = bt_br_discovery_start(&discovery_param, discovery_result,
					    ARRAY_SIZE(discovery_result));
		zassert_equal(err, 0, "Failed to start inquiry (err %d)", err);

		err = k_sem_take(&peer_found_sem, K_MSEC(DISCOVERY_RESULT_COUNT * 1280));
		zassert_equal(err, 0, "Peer not found by inquiry");

		err = bt_br_discovery_stop();
		if ((err != 0) && (err != -EALREADY)) {
			LOG_WRN("Failed to stop inquiry (err %d)", err);
		}

		bt_br_discovery_cb_unregister(&discovery_cb);
	}

	active = true;
}

void test_br_teardown(void)
{
	int err;

	active = false;

	err = bt_disable();
	if (err != 0) {
		LOG_ERR("bt_disable failed (err %d)", err);
	}
}

static void before_each(const struct ztest_unit_test *test, void *data)
{
	ARG_UNUSED(data);

	if (!active) {
		return;
	}

	LOG_INF("=== %s", test->name);
	current_test = test->name;

	if (role == TEST_BR_INITIATOR) {
		if (default_conn == NULL) {
			test_br_connect();
		}
	} else {
		test_br_wait_connected(K_SECONDS(30));
	}

	if (required_sec > BT_SECURITY_L1) {
		test_br_security(required_sec);
	}
}

static void release(void)
{
	int err;

	if (default_conn != NULL) {
		err = bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		if (err != 0) {
			LOG_ERR("Failed to disconnect ACL (err %d)", err);
		}

		if (!wait_for(is_disconnected, DISCONNECT_TIMEOUT)) {
			LOG_ERR("ACL disconnection timeout");
		}
	}
}

static void after_each(const struct ztest_unit_test *test, void *data)
{
	int err;

	ARG_UNUSED(data);

	if (!active) {
		return;
	}

	if (role == TEST_BR_INITIATOR) {
		release();

		/* The connection object is reused only after every reference is gone. */
		if (!wait_for(is_released, DISCONNECT_TIMEOUT)) {
			LOG_ERR("ACL connection object of %s not released", test->name);
		}
	} else if (!pending_valid) {
		/* The initiator releases the link; take over if it does not. */
		if (!wait_for(is_disconnected, RELEASE_TIMEOUT)) {
			LOG_WRN("Initiator did not release the ACL link");
			release();
		}
	}

	if ((required_sec > BT_SECURITY_L1) && !pending_valid) {
		err = bt_br_unpair(&peer_addr);
		if ((err != 0) && (err != -ENOENT)) {
			LOG_WRN("Failed to unpair (err %d)", err);
		}
	}

	current_test = NULL;
}

ZTEST_RULE(test_br_rule, before_each, after_each);
