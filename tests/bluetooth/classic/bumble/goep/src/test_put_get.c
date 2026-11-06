/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/ztest.h>

#include "test_common.h"

enum transfer_mode {
	TRANSFER_FINAL,
	TRANSFER_CONTINUE,
	TRANSFER_ERROR,
	TRANSFER_SRM,
	TRANSFER_SRMP,
};

static void add_srm(struct net_buf *buf, enum transfer_mode mode)
{
	if (mode >= TRANSFER_SRM) {
		zassert_ok(bt_obex_add_header_srm(buf, 1));
	}
	if (mode == TRANSFER_SRMP) {
		zassert_ok(bt_obex_add_header_srm_param(buf, 1));
	}
}

static void check_srm(struct net_buf *buf, enum transfer_mode mode)
{
	uint8_t value;

	if (mode >= TRANSFER_SRM) {
		zassert_ok(bt_obex_get_header_srm(buf, &value));
		zassert_equal(value, 1);
	}
	if (mode == TRANSFER_SRMP) {
		zassert_ok(bt_obex_get_header_srm_param(buf, &value));
		zassert_equal(value, 1);
	}
}

static void add_body(struct net_buf *buf, const char *message, bool end_body)
{
	uint16_t len = strlen(message) + 1;

	if (end_body) {
		zassert_ok(bt_obex_add_header_end_body(buf, len, (const uint8_t *)message));
	} else {
		zassert_ok(bt_obex_add_header_body(buf, len, (const uint8_t *)message));
	}
}

static void check_body(struct net_buf *buf, const char *message, bool end_body)
{
	const uint8_t *body;
	uint16_t len;

	if (end_body) {
		zassert_ok(bt_obex_get_header_end_body(buf, &len, &body));
	} else {
		zassert_ok(bt_obex_get_header_body(buf, &len, &body));
	}
	zassert_equal(len, strlen(message) + 1);
	zassert_mem_equal(body, message, len);
}

static void transfer(bool put, enum transfer_mode mode)
{
	enum test_goep_operation operation = put ? TEST_GOEP_PUT : TEST_GOEP_GET;
	unsigned int steps = mode == TRANSFER_CONTINUE ? 2 : 1;

	for (unsigned int repetition = 0; repetition < 2; repetition++) {
		test_goep_open();
		for (unsigned int step = 0; step < steps; step++) {
			bool last = step == steps - 1;
			bool final = !put || last;
			uint8_t response =
				last ? BT_OBEX_RSP_CODE_SUCCESS : BT_OBEX_RSP_CODE_CONTINUE;
			const char *message;
			struct net_buf *buf;

			if (mode == TRANSFER_CONTINUE) {
				message = !last ? "test_message_continue"
						: (put ? "test_message_final"
						       : "test_message_success");
			} else {
				message = put ? "test_message" : "test_get_message";
			}
			if (mode == TRANSFER_ERROR) {
				response = put ? BT_OBEX_RSP_CODE_FORBIDDEN
					       : BT_OBEX_RSP_CODE_NOT_FOUND;
			}
			if (test_goep_is_client()) {
				buf = test_goep_pdu();
				add_srm(buf, mode);
				if (put) {
					add_body(buf, message, true);
					test_goep_send(bt_obex_put(&test_client, final, buf), buf);
				} else {
					test_goep_send(bt_obex_get(&test_client, final, buf), buf);
				}
				buf = test_goep_expect(operation, response);
				check_srm(buf, mode);
				if (!put && mode < TRANSFER_ERROR) {
					check_body(buf, message, last);
				}
			} else {
				buf = test_goep_expect(operation, final);
				check_srm(buf, mode);
				if (put) {
					check_body(buf, message, true);
				}
				buf = test_goep_pdu();
				add_srm(buf, mode);
				if (put) {
					test_goep_send(bt_obex_put_rsp(&test_server, response, buf),
						       buf);
				} else {
					if (mode < TRANSFER_ERROR) {
						add_body(buf, message, last);
					}
					test_goep_send(bt_obex_get_rsp(&test_server, response, buf),
						       buf);
				}
			}
		}
		test_goep_close(repetition != 0);
	}
}

ZTEST(goep_client_put_get, test_001_put_success_final)
{
	transfer(true, TRANSFER_FINAL);
}

ZTEST(goep_server_put_get, test_001_put_success_final)
{
	transfer(true, TRANSFER_FINAL);
}

ZTEST(goep_client_put_get, test_002_put_continue_non_final)
{
	transfer(true, TRANSFER_CONTINUE);
}

ZTEST(goep_server_put_get, test_002_put_continue_non_final)
{
	transfer(true, TRANSFER_CONTINUE);
}

ZTEST(goep_client_put_get, test_003_put_error)
{
	transfer(true, TRANSFER_ERROR);
}

ZTEST(goep_server_put_get, test_003_put_error)
{
	transfer(true, TRANSFER_ERROR);
}

ZTEST(goep_client_put_get, test_004_put_srm_enable)
{
	transfer(true, TRANSFER_SRM);
}

ZTEST(goep_server_put_get, test_004_put_srm_enable)
{
	transfer(true, TRANSFER_SRM);
}

ZTEST(goep_client_put_get, test_005_put_srm_param)
{
	transfer(true, TRANSFER_SRMP);
}

ZTEST(goep_server_put_get, test_005_put_srm_param)
{
	transfer(true, TRANSFER_SRMP);
}

ZTEST(goep_client_put_get, test_006_get_success_final)
{
	transfer(false, TRANSFER_FINAL);
}

ZTEST(goep_server_put_get, test_006_get_success_final)
{
	transfer(false, TRANSFER_FINAL);
}

ZTEST(goep_client_put_get, test_007_get_continue_non_final)
{
	transfer(false, TRANSFER_CONTINUE);
}

ZTEST(goep_server_put_get, test_007_get_continue_non_final)
{
	transfer(false, TRANSFER_CONTINUE);
}

ZTEST(goep_client_put_get, test_008_get_not_found)
{
	transfer(false, TRANSFER_ERROR);
}

ZTEST(goep_server_put_get, test_008_get_not_found)
{
	transfer(false, TRANSFER_ERROR);
}

ZTEST(goep_client_put_get, test_009_get_srm_enable)
{
	transfer(false, TRANSFER_SRM);
}

ZTEST(goep_server_put_get, test_009_get_srm_enable)
{
	transfer(false, TRANSFER_SRM);
}

ZTEST(goep_client_put_get, test_010_get_srm_param)
{
	transfer(false, TRANSFER_SRMP);
}

ZTEST(goep_server_put_get, test_010_get_srm_param)
{
	transfer(false, TRANSFER_SRMP);
}

ZTEST_SUITE(goep_client_put_get, NULL, test_goep_client_setup, NULL, NULL, test_goep_teardown);
ZTEST_SUITE(goep_server_put_get, NULL, test_goep_server_setup, NULL, NULL, test_goep_teardown);
