/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/ztest.h>

#include "test_common.h"

static const uint8_t source_name[] = {
	0, 's', 0, 'o', 0, 'u', 0, 'r', 0, 'c', 0, 'e', 0, 0,
};
static const uint8_t destination_name[] = {0, 'd', 0, 'e', 0, 's', 0, 't', 0, 0};
static const uint8_t child_name[] = {0, 'c', 0, 'h', 0, 'i', 0, 'l', 0, 'd', 0, 0};

enum action_mode {
	ACTION_NAMES,
	ACTION_SRM,
	ACTION_SRMP,
};

static void action_headers(struct net_buf *buf, enum action_mode mode)
{
	if (mode >= ACTION_SRM) {
		zassert_ok(bt_obex_add_header_srm(buf, 1));
	}
	if (mode == ACTION_SRMP) {
		zassert_ok(bt_obex_add_header_srm_param(buf, 1));
	}
}

static void check_action_headers(struct net_buf *buf, enum action_mode mode)
{
	uint8_t value;

	if (mode >= ACTION_SRM) {
		zassert_ok(bt_obex_get_header_srm(buf, &value));
		zassert_equal(value, 1);
	}
	if (mode == ACTION_SRMP) {
		zassert_ok(bt_obex_get_header_srm_param(buf, &value));
		zassert_equal(value, 1);
	}
}

static void action(uint8_t requested_action, uint8_t response, enum action_mode mode)
{
	struct net_buf *buf;
	const uint8_t *name;
	uint16_t length;
	uint8_t action_id;

	for (int repetition = 0; repetition < 2; repetition++) {
		test_goep_open();
		if (test_goep_is_client()) {
			buf = test_goep_pdu();
			zassert_ok(bt_obex_add_header_action_id(buf, requested_action));
			action_headers(buf, mode);
			if (mode == ACTION_NAMES) {
				zassert_ok(bt_obex_add_header_name(buf, sizeof(source_name),
								   source_name));
				zassert_ok(bt_obex_add_header_dest_name(
					buf, sizeof(destination_name), destination_name));
			}
			test_goep_send(bt_obex_action(&test_client, true, buf), buf);
			buf = test_goep_expect(TEST_GOEP_ACTION, response);
			check_action_headers(buf, mode);
			if (mode == ACTION_NAMES) {
				zassert_equal(buf->len, 0);
			}
		} else {
			buf = test_goep_expect(TEST_GOEP_ACTION, true);
			zassert_ok(bt_obex_get_header_action_id(buf, &action_id));
			zassert_equal(action_id, requested_action);
			check_action_headers(buf, mode);
			if (mode == ACTION_NAMES) {
				zassert_ok(bt_obex_get_header_name(buf, &length, &name));
				zassert_equal(length, sizeof(source_name));
				zassert_mem_equal(name, source_name, length);
				zassert_ok(bt_obex_get_header_dest_name(buf, &length, &name));
				zassert_equal(length, sizeof(destination_name));
				zassert_mem_equal(name, destination_name, length);
			}
			buf = test_goep_pdu();
			action_headers(buf, mode);
			test_goep_send(bt_obex_action_rsp(&test_server, response, buf), buf);
		}
		test_goep_close(repetition != 0);
	}
}

static void abort_request(bool active)
{
	static const uint8_t message[] = "test_message_continue";
	struct net_buf *buf;
	const uint8_t *body;
	uint16_t length;

	for (int repetition = 0; repetition < 2; repetition++) {
		test_goep_open();
		if (!active) {
			if (test_goep_is_client()) {
				zassert_equal(bt_obex_abort(&test_client, NULL), -EINVAL);
			}
		} else if (test_goep_is_client()) {
			buf = test_goep_pdu();
			zassert_ok(bt_obex_add_header_end_body(buf, sizeof(message), message));
			test_goep_send(bt_obex_put(&test_client, false, buf), buf);
			buf = test_goep_expect(TEST_GOEP_PUT, BT_OBEX_RSP_CODE_CONTINUE);
			zassert_equal(buf->len, 0);
			buf = test_goep_pdu();
			test_goep_send(bt_obex_abort(&test_client, buf), buf);
			buf = test_goep_expect(TEST_GOEP_ABORT, BT_OBEX_RSP_CODE_SUCCESS);
			zassert_equal(buf->len, 0);
		} else {
			buf = test_goep_expect(TEST_GOEP_PUT, false);
			zassert_ok(bt_obex_get_header_end_body(buf, &length, &body));
			zassert_equal(length, sizeof(message));
			zassert_mem_equal(body, message, length);
			buf = test_goep_pdu();
			test_goep_send(
				bt_obex_put_rsp(&test_server, BT_OBEX_RSP_CODE_CONTINUE, buf), buf);
			buf = test_goep_expect(TEST_GOEP_ABORT, 0);
			zassert_equal(buf->len, 0);
			buf = test_goep_pdu();
			test_goep_send(
				bt_obex_abort_rsp(&test_server, BT_OBEX_RSP_CODE_SUCCESS, buf),
				buf);
		}
		test_goep_close(repetition != 0);
	}
}

static void setpath(bool parent, bool child, uint8_t response)
{
	uint8_t flags = BT_OBEX_SETPATH_FLAG_NO_CREATE;
	struct net_buf *buf;
	const uint8_t *name;
	uint16_t length;

	if (parent) {
		flags |= BT_OBEX_SETPATH_FLAG_BACKUP;
	}
	for (int repetition = 0; repetition < 2; repetition++) {
		test_goep_open();
		if (test_goep_is_client()) {
			buf = test_goep_pdu();
			if (child) {
				zassert_ok(bt_obex_add_header_name(buf, sizeof(child_name),
								   child_name));
			}
			test_goep_send(bt_obex_setpath(&test_client, flags, buf), buf);
			buf = test_goep_expect(TEST_GOEP_SETPATH, response);
			zassert_equal(buf->len, 0);
		} else {
			buf = test_goep_expect(TEST_GOEP_SETPATH, flags);
			if (child) {
				zassert_ok(bt_obex_get_header_name(buf, &length, &name));
				zassert_equal(length, sizeof(child_name));
				zassert_mem_equal(name, child_name, length);
			} else {
				zassert_equal(buf->len, 0);
			}
			buf = test_goep_pdu();
			test_goep_send(bt_obex_setpath_rsp(&test_server, response, buf), buf);
		}
		test_goep_close(repetition != 0);
	}
}

ZTEST(goep_client_misc, test_001_action_copy)
{
	action(BT_OBEX_ACTION_COPY, BT_OBEX_RSP_CODE_SUCCESS, ACTION_NAMES);
}

ZTEST(goep_server_misc, test_001_action_copy)
{
	action(BT_OBEX_ACTION_COPY, BT_OBEX_RSP_CODE_SUCCESS, ACTION_NAMES);
}

ZTEST(goep_client_misc, test_002_action_move)
{
	action(BT_OBEX_ACTION_MOVE_RENAME, BT_OBEX_RSP_CODE_SUCCESS, ACTION_NAMES);
}

ZTEST(goep_server_misc, test_002_action_move)
{
	action(BT_OBEX_ACTION_MOVE_RENAME, BT_OBEX_RSP_CODE_SUCCESS, ACTION_NAMES);
}

ZTEST(goep_client_misc, test_003_action_permission_source_variant)
{
	action(BT_OBEX_ACTION_SET_PERM, BT_OBEX_RSP_CODE_SUCCESS, ACTION_NAMES);
}

ZTEST(goep_server_misc, test_003_action_permission_source_variant)
{
	action(BT_OBEX_ACTION_SET_PERM, BT_OBEX_RSP_CODE_SUCCESS, ACTION_NAMES);
}

ZTEST(goep_client_misc, test_004_action_move_not_found)
{
	action(BT_OBEX_ACTION_MOVE_RENAME, BT_OBEX_RSP_CODE_NOT_FOUND, ACTION_NAMES);
}

ZTEST(goep_server_misc, test_004_action_move_not_found)
{
	action(BT_OBEX_ACTION_MOVE_RENAME, BT_OBEX_RSP_CODE_NOT_FOUND, ACTION_NAMES);
}

ZTEST(goep_client_misc, test_005_action_permission_not_found)
{
	action(BT_OBEX_ACTION_SET_PERM, BT_OBEX_RSP_CODE_NOT_FOUND, ACTION_NAMES);
}

ZTEST(goep_server_misc, test_005_action_permission_not_found)
{
	action(BT_OBEX_ACTION_SET_PERM, BT_OBEX_RSP_CODE_NOT_FOUND, ACTION_NAMES);
}

ZTEST(goep_client_misc, test_006_action_move_srm)
{
	action(BT_OBEX_ACTION_MOVE_RENAME, BT_OBEX_RSP_CODE_SUCCESS, ACTION_SRM);
}

ZTEST(goep_server_misc, test_006_action_move_srm)
{
	action(BT_OBEX_ACTION_MOVE_RENAME, BT_OBEX_RSP_CODE_SUCCESS, ACTION_SRM);
}

ZTEST(goep_client_misc, test_007_action_permission_srm)
{
	action(BT_OBEX_ACTION_SET_PERM, BT_OBEX_RSP_CODE_SUCCESS, ACTION_SRM);
}

ZTEST(goep_server_misc, test_007_action_permission_srm)
{
	action(BT_OBEX_ACTION_SET_PERM, BT_OBEX_RSP_CODE_SUCCESS, ACTION_SRM);
}

ZTEST(goep_client_misc, test_008_action_srmp)
{
	action(BT_OBEX_ACTION_SET_PERM, BT_OBEX_RSP_CODE_SUCCESS, ACTION_SRMP);
}

ZTEST(goep_server_misc, test_008_action_srmp)
{
	action(BT_OBEX_ACTION_SET_PERM, BT_OBEX_RSP_CODE_SUCCESS, ACTION_SRMP);
}

ZTEST(goep_client_misc, test_009_abort_active)
{
	abort_request(true);
}

ZTEST(goep_server_misc, test_009_abort_active)
{
	abort_request(true);
}

ZTEST(goep_client_misc, test_010_abort_idle)
{
	abort_request(false);
}

ZTEST(goep_server_misc, test_010_abort_idle)
{
	abort_request(false);
}

ZTEST(goep_client_misc, test_011_setpath_root)
{
	setpath(false, false, BT_OBEX_RSP_CODE_SUCCESS);
}

ZTEST(goep_server_misc, test_011_setpath_root)
{
	setpath(false, false, BT_OBEX_RSP_CODE_SUCCESS);
}

ZTEST(goep_client_misc, test_012_setpath_down)
{
	setpath(false, true, BT_OBEX_RSP_CODE_SUCCESS);
}

ZTEST(goep_server_misc, test_012_setpath_down)
{
	setpath(false, true, BT_OBEX_RSP_CODE_SUCCESS);
}

ZTEST(goep_client_misc, test_013_setpath_up)
{
	setpath(true, false, BT_OBEX_RSP_CODE_SUCCESS);
}

ZTEST(goep_server_misc, test_013_setpath_up)
{
	setpath(true, false, BT_OBEX_RSP_CODE_SUCCESS);
}

ZTEST(goep_client_misc, test_014_setpath_no_create)
{
	setpath(false, true, BT_OBEX_RSP_CODE_NOT_FOUND);
}

ZTEST(goep_server_misc, test_014_setpath_no_create)
{
	setpath(false, true, BT_OBEX_RSP_CODE_NOT_FOUND);
}

ZTEST(goep_client_misc, test_015_setpath_forbidden)
{
	setpath(false, true, BT_OBEX_RSP_CODE_FORBIDDEN);
}

ZTEST(goep_server_misc, test_015_setpath_forbidden)
{
	setpath(false, true, BT_OBEX_RSP_CODE_FORBIDDEN);
}

ZTEST_SUITE(goep_client_misc, NULL, test_goep_client_setup, NULL, NULL, test_goep_teardown);
ZTEST_SUITE(goep_server_misc, NULL, test_goep_server_setup, NULL, NULL, test_goep_teardown);
