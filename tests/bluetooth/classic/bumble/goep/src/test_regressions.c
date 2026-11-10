/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"

static void sequence_header(void)
{
	static const uint8_t valid[] = {0x93, 0x7f};
	static const uint8_t values[] = {0, 0x7f, UINT8_MAX};
	struct net_buf *buf;
	uint8_t encoded[8];
	uint16_t encoded_len;
	uint32_t sequence = 0;
	uint8_t action = 0;
	int decode_result;

	test_goep_open();
	for (size_t index = 0; index < ARRAY_SIZE(values); index++) {
		buf = test_goep_pdu();
		zassert_ok(bt_obex_add_header_session_seq_number(buf, values[index]));
		zassert_equal(buf->len, 2);
		zassert_equal(buf->data[0], valid[0]);
		zassert_equal(buf->data[1], values[index]);
		zassert_ok(bt_obex_add_header_conn_id(buf, 0x1234));
		zassert_ok(bt_obex_get_header_session_seq_number(buf, &sequence));
		zassert_equal(sequence, values[index]);
		zassert_ok(bt_obex_get_header_conn_id(buf, &sequence));
		zassert_equal(sequence, 0x1234);
		net_buf_unref(buf);
	}
	buf = test_goep_pdu();
	zassert_equal(bt_obex_add_header_session_seq_number(buf, 256), -EINVAL);
	zassert_equal(buf->len, 0);
	net_buf_add(buf, net_buf_tailroom(buf) - 1);
	encoded_len = buf->len;
	zassert_equal(bt_obex_add_header_session_seq_number(buf, 1), -ENOMEM);
	zassert_equal(buf->len, encoded_len);
	net_buf_unref(buf);
	sequence = 0;
	buf = test_goep_pdu();
	zassert_ok(bt_obex_add_header_action_id(buf, 0x7f));
	zassert_equal(buf->len, 2);
	zassert_ok(bt_obex_get_header_action_id(buf, &action));
	zassert_equal(action, 0x7f);
	net_buf_unref(buf);
	buf = test_goep_pdu();
	zassert_ok(bt_obex_add_header_session_seq_number(buf, 0x7f));
	encoded_len = buf->len;
	zassert_true(encoded_len <= sizeof(encoded));
	memcpy(encoded, buf->data, encoded_len);
	net_buf_unref(buf);
	buf = test_goep_pdu();
	net_buf_add_mem(buf, valid, sizeof(valid));
	decode_result = bt_obex_get_header_session_seq_number(buf, &sequence);
	net_buf_unref(buf);
	printk("CONTRACT SSN encoded_len=%u decode=%d value=%u action_control=%u\n",
	       encoded_len, decode_result, sequence, action);
	test_goep_close(false);
	zassert_equal(encoded_len, sizeof(valid), "SSN must be a one-byte quantity");
	zassert_mem_equal(encoded, valid, sizeof(valid));
	zassert_ok(decode_result, "independent valid SSN must decode");
	zassert_equal(sequence, 0x7f);
}

static void empty_end_body(void)
{
	static const uint8_t valid[] = {0x49, 0, 3};
	struct net_buf *buf;
	const uint8_t *body = NULL;
	uint16_t length = UINT16_MAX;
	int absent_result;
	int empty_result;

	test_goep_open();
	buf = test_goep_pdu();
	absent_result = bt_obex_get_header_end_body(buf, &length, &body);
	net_buf_add_mem(buf, valid, sizeof(valid));
	empty_result = bt_obex_get_header_end_body(buf, &length, &body);
	net_buf_unref(buf);
	printk("CONTRACT EOB absent=%d present_empty=%d length=%u\n",
	       absent_result, empty_result, length);
	test_goep_close(false);
	zassert_equal(absent_result, -ENODATA);
	zassert_ok(empty_result, "present zero-length End-of-Body must succeed");
	zassert_equal(length, 0);
}

ZTEST(goep_client_contracts, test_ssn_encoding)
{
	sequence_header();
}

ZTEST(goep_server_contracts, test_ssn_encoding)
{
	sequence_header();
}

ZTEST(goep_client_contracts, test_empty_end_body)
{
	empty_end_body();
}

ZTEST(goep_server_contracts, test_empty_end_body)
{
	empty_end_body();
}

ZTEST_SUITE(goep_client_contracts, NULL, test_goep_client_setup, NULL, NULL, test_goep_teardown);
ZTEST_SUITE(goep_server_contracts, NULL, test_goep_server_setup, NULL, NULL, test_goep_teardown);
