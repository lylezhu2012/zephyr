/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/bluetooth/classic/classic.h>
#include <zephyr/bluetooth/classic/goep.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"

#define TEST_MOPL    255
#define TEST_CHANNEL 10
#define TEST_PSM     0x1001

NET_BUF_POOL_FIXED_DEFINE(tx_pool, 4, 1024, CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

static struct bt_goep session;
static struct bt_goep_transport transport;
struct bt_obex_client test_client;
struct bt_obex_server test_server;
void (*test_goep_get_hook)(void);

int test_goep_try_send_raw(struct net_buf *buf)
{
	int err;

	if (IS_ENABLED(CONFIG_TEST_GOEP_L2CAP)) {
		err = bt_l2cap_chan_send(&transport.v2.chan.chan, buf);
	} else {
		err = bt_rfcomm_dlc_send(&transport.v1.dlc, buf);
	}
	return err < 0 ? err : 0;
}

void test_goep_send_raw(struct net_buf *buf)
{
	test_goep_send(test_goep_try_send_raw(buf), buf);
}

enum connect_mode {
	CONNECT_PLAIN,
	CONNECT_TARGET,
	CONNECT_SERVER_AUTH_1234,
	CONNECT_SERVER_AUTH_0000,
	CONNECT_CLIENT_AUTH,
};

static bool with_connection_id;
static bool authenticating;
static const uint8_t target_name[] = "1234567812345678";
static union bt_obex_uuid target_uuid;

struct operation_event {
	enum test_goep_operation operation;
	uint8_t status;
	uint16_t len;
	uint8_t headers[TEST_MOPL];
};

K_MSGQ_DEFINE(operation_events, sizeof(struct operation_event), 4, 4);
static struct net_buf *received_headers;

static K_SEM_DEFINE(transport_connected_sem, 0, 1);
static K_SEM_DEFINE(transport_disconnected_sem, 0, 1);
static K_SEM_DEFINE(obex_connected_sem, 0, 1);
static K_SEM_DEFINE(obex_disconnected_sem, 0, 1);

static void wait_event(struct k_sem *sem)
{
	zassert_ok(test_br_wait(sem, TEST_BR_STEP_TIMEOUT));
}

static void transport_connected(struct bt_conn *conn, struct bt_goep *goep)
{
	zassert_equal_ptr(goep, &session);
	zassert_equal_ptr(conn, test_br_conn());
	k_sem_give(&transport_connected_sem);
}

static void transport_disconnected(struct bt_goep *goep)
{
	zassert_equal_ptr(goep, &session);
	k_sem_give(&transport_disconnected_sem);
}

static const struct bt_goep_transport_ops transport_ops = {
	.connected = transport_connected,
	.disconnected = transport_disconnected,
};

static void session_init(void)
{
	memset(&session, 0, sizeof(session));
	memset(&transport, 0, sizeof(transport));
	session.transport_ops = &transport_ops;
	if (IS_ENABLED(CONFIG_TEST_GOEP_L2CAP)) {
		BT_GOEP_INIT_V2(&session, &transport.v2);
	} else {
		BT_GOEP_INIT_V1(&session, &transport.v1);
	}
	k_sem_reset(&transport_connected_sem);
	k_sem_reset(&transport_disconnected_sem);
	k_sem_reset(&obex_connected_sem);
	k_sem_reset(&obex_disconnected_sem);
}

static int rfcomm_accept(struct bt_conn *conn, struct bt_goep_transport_rfcomm_server *listener,
			 struct bt_goep **goep)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(listener);
	*goep = &session;
	return 0;
}

static int l2cap_accept(struct bt_conn *conn, struct bt_goep_transport_l2cap_server *listener,
			struct bt_goep **goep)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(listener);
	*goep = &session;
	return 0;
}

static struct bt_goep_transport_rfcomm_server rfcomm_listener = {
	.rfcomm.channel = TEST_CHANNEL,
	.accept = rfcomm_accept,
};

static struct bt_goep_transport_l2cap_server l2cap_listener = {
	.l2cap.psm = TEST_PSM,
	.l2cap.sec_level = BT_SECURITY_L2,
	.accept = l2cap_accept,
};

struct net_buf *test_goep_pdu(void)
{
	struct net_buf *buf = bt_goep_create_pdu(&session, &tx_pool);

	zassert_not_null(buf);
	return buf;
}

void test_goep_send(int err, struct net_buf *buf)
{
	if (err != 0) {
		net_buf_unref(buf);
	}
	zassert_ok(err);
}

static void record_operation(enum test_goep_operation operation, uint8_t status,
			     struct net_buf *headers);

static void server_connect(struct bt_obex_server *obex_server, uint8_t version, uint16_t mopl,
			   struct net_buf *headers)
{
	struct net_buf *buf;
	const uint8_t *target;
	uint16_t length;

	zassert_equal_ptr(obex_server, &test_server);
	zassert_equal(version, 0x10);
	zassert_equal(mopl, TEST_MOPL);
	if (authenticating) {
		record_operation(TEST_GOEP_CONNECT, 0, headers);
		return;
	}
	buf = test_goep_pdu();
	if (with_connection_id) {
		zassert_ok(bt_obex_get_header_target(headers, &length, &target));
		zassert_equal(length, sizeof(target_name) - 1);
		zassert_mem_equal(target, target_name, length);
		zassert_ok(bt_obex_add_header_who(buf, sizeof(target_name) - 1, target_name));
		zassert_ok(bt_obex_add_header_conn_id(buf, 1));
	} else {
		zassert_equal(headers->len, 0);
	}
	test_goep_send(bt_obex_connect_rsp(obex_server, BT_OBEX_RSP_CODE_SUCCESS, TEST_MOPL, buf),
		       buf);
	k_sem_give(&obex_connected_sem);
}

static void server_disconnect(struct bt_obex_server *obex_server, struct net_buf *headers)
{
	struct net_buf *buf = test_goep_pdu();
	const uint8_t *who;
	uint16_t length;
	uint32_t conn_id;

	zassert_equal_ptr(obex_server, &test_server);
	if (with_connection_id) {
		zassert_ok(bt_obex_get_header_who(headers, &length, &who));
		zassert_equal(length, sizeof(target_name) - 1);
		zassert_mem_equal(who, target_name, length);
		zassert_ok(bt_obex_get_header_conn_id(headers, &conn_id));
		zassert_equal(conn_id, 1);
	} else {
		zassert_equal(headers->len, 0);
	}
	test_goep_send(bt_obex_disconnect_rsp(obex_server, BT_OBEX_RSP_CODE_SUCCESS, buf), buf);
	k_sem_give(&obex_disconnected_sem);
}

static void record_operation(enum test_goep_operation operation, uint8_t status,
			     struct net_buf *headers)
{
	struct operation_event event = {.operation = operation, .status = status};

	if (headers != NULL) {
		zassert_true(headers->len <= sizeof(event.headers));
		event.len = headers->len;
		memcpy(event.headers, headers->data, headers->len);
	}
	zassert_ok(k_msgq_put(&operation_events, &event, K_NO_WAIT));
}

struct net_buf *test_goep_expect(enum test_goep_operation operation, uint8_t status)
{
	struct operation_event event;

	zassert_ok(k_msgq_get(&operation_events, &event, TEST_BR_STEP_TIMEOUT));
	zassert_equal(event.operation, operation, "operation %u, expected %u",
		      event.operation, operation);
	zassert_equal(event.status, status);
	if (received_headers != NULL) {
		net_buf_unref(received_headers);
	}
	received_headers = test_goep_pdu();
	net_buf_add_mem(received_headers, event.headers, event.len);
	return received_headers;
}

static void server_put(struct bt_obex_server *obex_server, bool final, struct net_buf *headers)
{
	zassert_equal_ptr(obex_server, &test_server);
	record_operation(TEST_GOEP_PUT, final, headers);
}

static void server_get(struct bt_obex_server *obex_server, bool final, struct net_buf *headers)
{
	zassert_equal_ptr(obex_server, &test_server);
	record_operation(TEST_GOEP_GET, final, headers);
}

static void server_action(struct bt_obex_server *obex_server, bool final, struct net_buf *headers)
{
	zassert_equal_ptr(obex_server, &test_server);
	record_operation(TEST_GOEP_ACTION, final, headers);
}

static void server_abort(struct bt_obex_server *obex_server, struct net_buf *headers)
{
	zassert_equal_ptr(obex_server, &test_server);
	record_operation(TEST_GOEP_ABORT, 0, headers);
}

static void server_setpath(struct bt_obex_server *obex_server, uint8_t flags,
			   struct net_buf *headers)
{
	zassert_equal_ptr(obex_server, &test_server);
	record_operation(TEST_GOEP_SETPATH, flags, headers);
}

static const struct bt_obex_server_ops server_ops = {
	.connect = server_connect,
	.disconnect = server_disconnect,
	.put = server_put,
	.get = server_get,
	.action = server_action,
	.abort = server_abort,
	.setpath = server_setpath,
};

static void client_connect(struct bt_obex_client *obex_client, uint8_t rsp_code, uint8_t version,
			   uint16_t mopl, struct net_buf *headers)
{
	const uint8_t *who;
	uint16_t length;
	uint32_t conn_id;

	zassert_equal_ptr(obex_client, &test_client);
	zassert_equal(version, 0x10);
	zassert_equal(mopl, TEST_MOPL);
	if (authenticating) {
		record_operation(TEST_GOEP_CONNECT, rsp_code, headers);
		return;
	}
	zassert_equal(rsp_code, BT_OBEX_RSP_CODE_SUCCESS);
	if (with_connection_id) {
		zassert_ok(bt_obex_get_header_who(headers, &length, &who));
		zassert_equal(length, sizeof(target_name) - 1);
		zassert_mem_equal(who, target_name, length);
		zassert_ok(bt_obex_get_header_conn_id(headers, &conn_id));
		zassert_equal(conn_id, 1);
	} else {
		zassert_equal(headers->len, 0);
	}
	k_sem_give(&obex_connected_sem);
}

static void client_disconnect(struct bt_obex_client *obex_client, uint8_t rsp_code,
			      struct net_buf *headers)
{
	zassert_equal_ptr(obex_client, &test_client);
	zassert_equal(rsp_code, BT_OBEX_RSP_CODE_SUCCESS);
	zassert_not_null(headers);
	if (!with_connection_id) {
		zassert_equal(headers->len, 0);
	}
	k_sem_give(&obex_disconnected_sem);
}

static void client_put(struct bt_obex_client *obex_client, uint8_t rsp_code,
		       struct net_buf *headers)
{
	zassert_equal_ptr(obex_client, &test_client);
	record_operation(TEST_GOEP_PUT, rsp_code, headers);
}

static void client_get(struct bt_obex_client *obex_client, uint8_t rsp_code,
		       struct net_buf *headers)
{
	zassert_equal_ptr(obex_client, &test_client);
	if (test_goep_get_hook != NULL) {
		void (*hook)(void) = test_goep_get_hook;

		test_goep_get_hook = NULL;
		hook();
	}
	record_operation(TEST_GOEP_GET, rsp_code, headers);
}

static void client_action(struct bt_obex_client *obex_client, uint8_t rsp_code,
			  struct net_buf *headers)
{
	zassert_equal_ptr(obex_client, &test_client);
	record_operation(TEST_GOEP_ACTION, rsp_code, headers);
}

static void client_abort(struct bt_obex_client *obex_client, uint8_t rsp_code,
			 struct net_buf *headers)
{
	zassert_equal_ptr(obex_client, &test_client);
	record_operation(TEST_GOEP_ABORT, rsp_code, headers);
}

static void client_setpath(struct bt_obex_client *obex_client, uint8_t rsp_code,
			   struct net_buf *headers)
{
	zassert_equal_ptr(obex_client, &test_client);
	record_operation(TEST_GOEP_SETPATH, rsp_code, headers);
}

static const struct bt_obex_client_ops client_ops = {
	.connect = client_connect,
	.disconnect = client_disconnect,
	.put = client_put,
	.get = client_get,
	.action = client_action,
	.abort = client_abort,
	.setpath = client_setpath,
};

void *test_goep_client_setup(void)
{
	test_br_setup(TEST_BR_INITIATOR, BT_SECURITY_L2);
	return NULL;
}

void *test_goep_server_setup(void)
{
	test_br_setup(TEST_BR_ACCEPTOR, BT_SECURITY_L2);
	if (IS_ENABLED(CONFIG_TEST_GOEP_L2CAP)) {
		zassert_ok(bt_goep_transport_l2cap_server_register(&l2cap_listener));
	} else {
		zassert_ok(bt_goep_transport_rfcomm_server_register(&rfcomm_listener));
	}
	return NULL;
}

void test_goep_teardown(void *fixture)
{
	ARG_UNUSED(fixture);
	test_br_teardown();
}

bool test_goep_is_client(void)
{
	return test_br_role() == TEST_BR_INITIATOR;
}

struct authentication_value {
	uint8_t bytes[BT_OBEX_CHALLENGE_TAG_NONCE_LEN];
	bool found;
};

static bool authentication_tlv(struct bt_obex_tlv *tlv, void *user_data)
{
	struct authentication_value *value = user_data;

	zassert_equal(tlv->type, BT_OBEX_CHALLENGE_TAG_NONCE);
	zassert_equal(tlv->data_len, sizeof(value->bytes));
	zassert_false(value->found);
	memcpy(value->bytes, tlv->data, sizeof(value->bytes));
	value->found = true;
	return true;
}

static void authentication_header(struct net_buf *headers, bool response, uint8_t value[16])
{
	struct authentication_value parsed = {0};
	const uint8_t *data;
	uint16_t length;

	if (response) {
		zassert_ok(bt_obex_get_header_auth_rsp(headers, &length, &data));
	} else {
		zassert_ok(bt_obex_get_header_auth_challenge(headers, &length, &data));
	}
	zassert_equal(length, 18);
	zassert_ok(bt_obex_tlv_parse(length, data, authentication_tlv, &parsed));
	zassert_true(parsed.found);
	memcpy(value, parsed.bytes, sizeof(parsed.bytes));
}

static void add_challenge(struct net_buf *buf, const uint8_t nonce[16])
{
	const struct bt_obex_tlv tlv = {
		.type = BT_OBEX_CHALLENGE_TAG_NONCE,
		.data_len = BT_OBEX_CHALLENGE_TAG_NONCE_LEN,
		.data = nonce,
	};

	zassert_ok(bt_obex_add_header_auth_challenge(buf, 1, &tlv));
}

static void add_auth_response(struct net_buf *buf, const uint8_t *password, const uint8_t nonce[16])
{
	uint8_t digest[BT_OBEX_RESPONSE_TAG_REQ_DIGEST_LEN];
	const struct bt_obex_tlv tlv = {
		.type = BT_OBEX_RESPONSE_TAG_REQ_DIGEST,
		.data_len = sizeof(digest),
		.data = digest,
	};

	zassert_ok(bt_obex_calculate_request_digest(password, 4, nonce, digest));
	zassert_ok(bt_obex_add_header_auth_rsp(buf, 1, &tlv));
}

static void verify_response(struct net_buf *headers, const uint8_t *password,
			    const uint8_t nonce[16])
{
	uint8_t digest[BT_OBEX_RESPONSE_TAG_REQ_DIGEST_LEN];

	authentication_header(headers, true, digest);
	zassert_ok(bt_obex_verify_auth_response(password, 4, nonce, digest));
	zassert_equal(bt_obex_verify_auth_response((const uint8_t *)"bad", 3, nonce, digest),
		      -EINVAL);
}

static void authenticate(enum connect_mode mode)
{
	const uint8_t *password =
		(const uint8_t *)(mode == CONNECT_SERVER_AUTH_1234 ? "1234" : "0000");
	uint8_t nonce[BT_OBEX_CHALLENGE_TAG_NONCE_LEN];
	struct net_buf *headers;
	struct net_buf *buf;

	if (test_goep_is_client()) {
		buf = test_goep_pdu();
		if (mode == CONNECT_CLIENT_AUTH) {
			zassert_ok(bt_obex_generate_nonce(password, 4, nonce));
			add_challenge(buf, nonce);
		}
		test_goep_send(bt_obex_connect(&test_client, TEST_MOPL, buf), buf);
		if (mode != CONNECT_CLIENT_AUTH) {
			headers = test_goep_expect(TEST_GOEP_CONNECT, BT_OBEX_RSP_CODE_UNAUTH);
			authentication_header(headers, false, nonce);
			buf = test_goep_pdu();
			add_auth_response(buf, password, nonce);
			test_goep_send(bt_obex_connect(&test_client, TEST_MOPL, buf), buf);
		}
		headers = test_goep_expect(TEST_GOEP_CONNECT, BT_OBEX_RSP_CODE_SUCCESS);
		if (mode == CONNECT_CLIENT_AUTH) {
			verify_response(headers, password, nonce);
		} else {
			zassert_equal(headers->len, 0);
		}
	} else {
		headers = test_goep_expect(TEST_GOEP_CONNECT, 0);
		if (mode == CONNECT_CLIENT_AUTH) {
			authentication_header(headers, false, nonce);
			buf = test_goep_pdu();
			add_auth_response(buf, password, nonce);
		} else {
			zassert_equal(headers->len, 0);
			zassert_ok(bt_obex_generate_nonce(password, 4, nonce));
			buf = test_goep_pdu();
			add_challenge(buf, nonce);
			test_goep_send(bt_obex_connect_rsp(&test_server, BT_OBEX_RSP_CODE_UNAUTH,
							   TEST_MOPL, buf),
				       buf);
			headers = test_goep_expect(TEST_GOEP_CONNECT, 0);
			verify_response(headers, password, nonce);
			buf = test_goep_pdu();
		}
		test_goep_send(
			bt_obex_connect_rsp(&test_server, BT_OBEX_RSP_CODE_SUCCESS, TEST_MOPL, buf),
			buf);
	}
}

static void open_transport(void)
{
	if (received_headers != NULL) {
		net_buf_unref(received_headers);
		received_headers = NULL;
	}
	k_msgq_purge(&operation_events);
	if (!test_br_connected()) {
		if (test_goep_is_client()) {
			test_br_connect();
		} else {
			test_br_wait_connected(TEST_BR_STEP_TIMEOUT);
		}
		test_br_security(BT_SECURITY_L2);
	}
	test_br_wait_connected(TEST_BR_STEP_TIMEOUT);
	session_init();
	test_br_sync();
	if (test_goep_is_client()) {
		if (IS_ENABLED(CONFIG_TEST_GOEP_L2CAP)) {
			zassert_ok(bt_goep_transport_l2cap_connect(test_br_conn(), &session,
								   TEST_PSM));
		} else {
			zassert_ok(bt_goep_transport_rfcomm_connect(test_br_conn(), &session,
								    TEST_CHANNEL));
		}
	}
	wait_event(&transport_connected_sem);
}

static void open_session(enum connect_mode mode)
{
	struct net_buf *buf;

	with_connection_id = mode == CONNECT_TARGET;
	authenticating = mode == CONNECT_SERVER_AUTH_1234 || mode == CONNECT_SERVER_AUTH_0000 ||
			 mode == CONNECT_CLIENT_AUTH;
	open_transport();
	if (test_goep_is_client()) {
		memset(&test_client, 0, sizeof(test_client));
		test_client.obex = &session.obex;
		test_client.ops = &client_ops;
	} else {
		memset(&test_server, 0, sizeof(test_server));
		test_server.obex = &session.obex;
		test_server.ops = &server_ops;
		if (with_connection_id) {
			zassert_ok(bt_obex_make_uuid(&target_uuid, target_name,
						     sizeof(target_name) - 1));
		}
		zassert_ok(bt_obex_server_register(&test_server,
						   with_connection_id ? &target_uuid.u128 : NULL));
	}
	test_br_sync();
	if (authenticating) {
		authenticate(mode);
		return;
	}
	if (test_goep_is_client()) {
		buf = test_goep_pdu();
		if (with_connection_id) {
			zassert_ok(bt_obex_add_header_target(buf, sizeof(target_name) - 1,
							     target_name));
		}
		test_goep_send(bt_obex_connect(&test_client, TEST_MOPL, buf), buf);
	}
	wait_event(&obex_connected_sem);
}

void test_goep_open(void)
{
	open_session(CONNECT_PLAIN);
}

static void close_acl(bool server_initiated)
{
	int err;

	if (server_initiated) {
		test_br_acceptor_disconnect();
	} else if (test_goep_is_client()) {
		test_br_disconnect();
	} else {
		test_br_wait_disconnected(TEST_BR_STEP_TIMEOUT);
	}
	err = bt_br_unpair(&peer_addr);
	zassert_true(err == 0 || err == -ENOENT);
}

void test_goep_close(bool server_initiated)
{
	test_br_sync();
	zassert_equal(k_msgq_num_used_get(&operation_events), 0);
	if (received_headers != NULL) {
		net_buf_unref(received_headers);
		received_headers = NULL;
	}
	close_acl(server_initiated);
	wait_event(&transport_disconnected_sem);
	zassert_equal(k_sem_count_get(&obex_disconnected_sem), 0);
	if (!test_goep_is_client()) {
		zassert_ok(bt_obex_server_unregister(&test_server));
	}
}

void test_goep_expect_transport_closed(void)
{
	wait_event(&transport_disconnected_sem);
	zassert_equal(k_msgq_num_used_get(&operation_events), 0);
	k_sem_give(&transport_disconnected_sem);
}

static void close_obex_session(bool server_initiated, bool explicit_transport)
{
	struct net_buf *buf;

	test_br_sync();
	zassert_equal(k_msgq_num_used_get(&operation_events), 0);
	if (received_headers != NULL) {
		net_buf_unref(received_headers);
		received_headers = NULL;
	}
	if (test_goep_is_client()) {
		buf = test_goep_pdu();
		if (with_connection_id) {
			zassert_ok(
				bt_obex_add_header_who(buf, sizeof(target_name) - 1, target_name));
			zassert_ok(bt_obex_add_header_conn_id(buf, 1));
		}
		test_goep_send(bt_obex_disconnect(&test_client, buf), buf);
	}
	wait_event(&obex_disconnected_sem);
	test_br_sync();
	if (explicit_transport) {
		if (test_goep_is_client() != server_initiated) {
			if (IS_ENABLED(CONFIG_TEST_GOEP_L2CAP)) {
				zassert_ok(bt_goep_transport_l2cap_disconnect(&session));
			} else {
				zassert_ok(bt_goep_transport_rfcomm_disconnect(&session));
			}
		}
	} else {
		close_acl(server_initiated);
	}
	wait_event(&transport_disconnected_sem);
	if (!test_goep_is_client()) {
		zassert_ok(bt_obex_server_unregister(&test_server));
	}
	if (explicit_transport) {
		test_br_sync();
		close_acl(server_initiated);
	}
}

ZTEST(goep_client_connect, test_001_obex_connect_disconnect)
{
	for (int repetition = 0; repetition < 4; repetition++) {
		test_goep_open();
		close_obex_session((repetition % 2) != 0, repetition >= 2);
	}
}

ZTEST(goep_server_connect, test_001_obex_connect_disconnect)
{
	for (int repetition = 0; repetition < 4; repetition++) {
		test_goep_open();
		close_obex_session((repetition % 2) != 0, repetition >= 2);
	}
}

ZTEST(goep_client_connect, test_002_connection_id)
{
	for (int repetition = 0; repetition < 2; repetition++) {
		open_session(CONNECT_TARGET);
		close_obex_session(repetition != 0, false);
	}
}

ZTEST(goep_server_connect, test_002_connection_id)
{
	for (int repetition = 0; repetition < 2; repetition++) {
		open_session(CONNECT_TARGET);
		close_obex_session(repetition != 0, false);
	}
}

static void authenticated_sessions(enum connect_mode mode)
{
	for (int repetition = 0; repetition < 2; repetition++) {
		open_session(mode);
		test_goep_close(repetition != 0);
	}
}

ZTEST(goep_client_connect, test_003_server_challenge_1234)
{
	authenticated_sessions(CONNECT_SERVER_AUTH_1234);
}

ZTEST(goep_server_connect, test_003_server_challenge_1234)
{
	authenticated_sessions(CONNECT_SERVER_AUTH_1234);
}

ZTEST(goep_client_connect, test_004_server_challenge_0000)
{
	authenticated_sessions(CONNECT_SERVER_AUTH_0000);
}

ZTEST(goep_server_connect, test_004_server_challenge_0000)
{
	authenticated_sessions(CONNECT_SERVER_AUTH_0000);
}

ZTEST(goep_client_connect, test_005_client_challenge_0000)
{
	authenticated_sessions(CONNECT_CLIENT_AUTH);
}

ZTEST(goep_server_connect, test_005_client_challenge_0000)
{
	authenticated_sessions(CONNECT_CLIENT_AUTH);
}

static void transport_sessions(bool obex, bool explicit_disconnect)
{
	int repetitions = explicit_disconnect ? 4 : 2;

	for (int repetition = 0; repetition < repetitions; repetition++) {
		bool server_initiated = (repetition % 2) != 0;
		bool server_transport = repetition >= 2;

		if (obex) {
			open_session(CONNECT_PLAIN);
		} else {
			open_transport();
		}
		test_br_sync();
		if (explicit_disconnect) {
			if (test_goep_is_client() != server_transport) {
				if (IS_ENABLED(CONFIG_TEST_GOEP_L2CAP)) {
					zassert_ok(bt_goep_transport_l2cap_disconnect(&session));
				} else {
					zassert_ok(bt_goep_transport_rfcomm_disconnect(&session));
				}
			}
		} else {
			close_acl(server_initiated);
		}
		wait_event(&transport_disconnected_sem);
		zassert_equal(k_sem_count_get(&obex_disconnected_sem), 0);
		if (obex && !test_goep_is_client()) {
			zassert_ok(bt_obex_server_unregister(&test_server));
		}
		if (explicit_disconnect) {
			test_br_sync();
			close_acl(server_initiated);
		}
	}
}

ZTEST(goep_client_connect, test_006_obex_acl_teardown)
{
	transport_sessions(true, false);
}

ZTEST(goep_server_connect, test_006_obex_acl_teardown)
{
	transport_sessions(true, false);
}

ZTEST(goep_client_connect, test_007_transport_acl_teardown)
{
	transport_sessions(false, false);
}

ZTEST(goep_server_connect, test_007_transport_acl_teardown)
{
	transport_sessions(false, false);
}

ZTEST(goep_client_connect, test_008_transport_disconnect)
{
	transport_sessions(false, true);
}

ZTEST(goep_server_connect, test_008_transport_disconnect)
{
	transport_sessions(false, true);
}

ZTEST_SUITE(goep_client_connect, NULL, test_goep_client_setup, NULL, NULL, test_goep_teardown);
ZTEST_SUITE(goep_server_connect, NULL, test_goep_server_setup, NULL, NULL, test_goep_teardown);
