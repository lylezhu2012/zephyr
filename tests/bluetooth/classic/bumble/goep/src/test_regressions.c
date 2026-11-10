/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"

static void abort_response(bool description)
{
	static const uint8_t message[] = "pending";
	static const uint8_t text[] = {0, 'x', 0, 0};
	static const uint8_t expected[] = {0x05, 0, 7, 0, 'x', 0, 0};
	struct net_buf *buf;
	const uint8_t *body;
	uint16_t length;

	for (int repetition = 0; repetition < 2; repetition++) {
		test_goep_open();
		if (test_goep_is_client()) {
			buf = test_goep_pdu();
			zassert_ok(bt_obex_add_header_body(buf, sizeof(message), message));
			test_goep_send(bt_obex_put(&test_client, false, buf), buf);
			buf = test_goep_expect(TEST_GOEP_PUT, BT_OBEX_RSP_CODE_CONTINUE);
			zassert_equal(buf->len, 0);
			buf = test_goep_pdu();
			test_goep_send(bt_obex_abort(&test_client, buf), buf);
			buf = test_goep_expect(TEST_GOEP_ABORT, BT_OBEX_RSP_CODE_SUCCESS);
			if (description) {
				zassert_equal(buf->len, sizeof(expected));
				zassert_mem_equal(buf->data, expected, sizeof(expected));
			} else {
				zassert_equal(buf->len, 0);
			}
		} else {
			buf = test_goep_expect(TEST_GOEP_PUT, false);
			zassert_ok(bt_obex_get_header_body(buf, &length, &body));
			zassert_equal(length, sizeof(message));
			zassert_mem_equal(body, message, length);
			buf = test_goep_pdu();
			test_goep_send(
				bt_obex_put_rsp(&test_server, BT_OBEX_RSP_CODE_CONTINUE, buf), buf);
			buf = test_goep_expect(TEST_GOEP_ABORT, 0);
			zassert_equal(buf->len, 0);
			buf = test_goep_pdu();
			if (description) {
				zassert_ok(bt_obex_add_header_description(buf, sizeof(text), text));
				zassert_equal(buf->len, sizeof(expected));
				zassert_mem_equal(buf->data, expected, sizeof(expected));
			}
			test_goep_send(bt_obex_abort_rsp(&test_server,
							BT_OBEX_RSP_CODE_SUCCESS, buf), buf);
		}
		test_goep_close(repetition != 0);
	}
}

ZTEST(goep_client_regressions, test_abort_empty_response_control)
{
	abort_response(false);
}

ZTEST(goep_server_regressions, test_abort_empty_response_control)
{
	abort_response(false);
}

ZTEST(goep_client_regressions, test_abort_description_response)
{
	abort_response(true);
}

ZTEST(goep_server_regressions, test_abort_description_response)
{
	abort_response(true);
}

ZTEST_SUITE(goep_client_regressions, NULL, test_goep_client_setup, NULL, NULL, test_goep_teardown);
ZTEST_SUITE(goep_server_regressions, NULL, test_goep_server_setup, NULL, NULL, test_goep_teardown);

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

static bool count_tlv(struct bt_obex_tlv *tlv, void *user_data)
{
	size_t *count = user_data;

	ARG_UNUSED(tlv);
	(*count)++;
	return true;
}

static bool stop_tlv(struct bt_obex_tlv *tlv, void *user_data)
{
	count_tlv(tlv, user_data);
	return false;
}

static void truncated_tlv(void)
{
	static const uint8_t complete[] = {0, 0};
	static const uint8_t short_header[] = {0};
	static const uint8_t short_value[] = {0, 2, 0xaa};
	static const uint8_t short_suffix[] = {0, 0, 0};
	size_t count = 0;
	int header_result;
	int value_result;
	int suffix_result;

	test_goep_open();
	zassert_equal(bt_obex_tlv_parse(0, complete, count_tlv, &count), -EINVAL);
	zassert_ok(bt_obex_tlv_parse(sizeof(short_suffix), short_suffix, stop_tlv, &count));
	zassert_equal(count, 1);
	count = 0;
	zassert_ok(bt_obex_tlv_parse(sizeof(complete), complete, count_tlv, &count));
	zassert_equal(count, 1);
	header_result = bt_obex_tlv_parse(sizeof(short_header), short_header, count_tlv, &count);
	value_result = bt_obex_tlv_parse(sizeof(short_value), short_value, count_tlv, &count);
	suffix_result = bt_obex_tlv_parse(sizeof(short_suffix), short_suffix, count_tlv, &count);
	printk("CONTRACT TLV short_header=%d short_value=%d short_suffix=%d callbacks=%zu\n",
	       header_result, value_result, suffix_result, count);
	test_goep_close(false);
	zassert_true(header_result < 0, "truncated TLV header accepted");
	zassert_true(value_result < 0, "truncated TLV value accepted");
	zassert_true(suffix_result < 0, "truncated TLV suffix accepted");
}

static void second_server(void)
{
	static const uint8_t first_name[] = "1111111111111111";
	static const uint8_t second_name[] = "2222222222222222";
	union bt_obex_uuid first_uuid;
	union bt_obex_uuid second_uuid;
	struct bt_obex local_obex = {0};
	struct bt_obex_server first = {.obex = &local_obex};
	struct bt_obex_server second = {.obex = &local_obex};
	int second_result = 0;
	int null_result = -EINVAL;

	test_goep_open();
	if (!test_goep_is_client()) {
		first.ops = test_server.ops;
		second.ops = test_server.ops;
		zassert_ok(bt_obex_make_uuid(&first_uuid, first_name, sizeof(first_name) - 1));
		zassert_ok(bt_obex_make_uuid(&second_uuid, second_name, sizeof(second_name) - 1));
		zassert_ok(bt_obex_server_register(&first, &first_uuid.u128));
		second_result = bt_obex_server_register(&second, &second_uuid.u128);
		if (second_result == 0) {
			zassert_ok(bt_obex_server_unregister(&second));
			null_result = bt_obex_server_register(&second, NULL);
			if (null_result == 0) {
				zassert_ok(bt_obex_server_unregister(&second));
			}
		}
		zassert_ok(bt_obex_server_unregister(&first));
		printk("CONTRACT REGISTER first=0 second=%d\n", second_result);
	}
	test_goep_close(false);
	zassert_ok(second_result, "second fresh UUID server must register");
	zassert_equal(null_result, -EINVAL, "NULL parameter must not reuse stale UUID");
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

ZTEST(goep_client_contracts, test_truncated_tlv)
{
	truncated_tlv();
}

ZTEST(goep_server_contracts, test_truncated_tlv)
{
	truncated_tlv();
}

ZTEST(goep_client_contracts, test_second_server)
{
	second_server();
}

ZTEST(goep_server_contracts, test_second_server)
{
	second_server();
}

ZTEST_SUITE(goep_client_contracts, NULL, test_goep_client_setup, NULL, NULL, test_goep_teardown);
ZTEST_SUITE(goep_server_contracts, NULL, test_goep_server_setup, NULL, NULL, test_goep_teardown);

enum abort_sequence {
	ABORT_PUT_PENDING,
	ABORT_GET_PENDING,
	ABORT_GET_TERMINAL,
	ABORT_GET_ERROR,
	ABORT_GET_SRM_CONTINUE,
	ABORT_GET_SRM_TERMINAL,
	ABORT_GET_SRM_SINGLE,
	ABORT_GET_SRM_ERROR,
	ABORT_GET_SRM_FAILURE,
	ABORT_GET_SRM_REPEATED_ERROR,
	ABORT_GET_SRM_ERROR_CONTINUE,
	ABORT_GET_SRM_TERMINAL_FAILURE,
	ABORT_GET_SRM_ERROR_HEADERS,
	ABORT_GET_SRM_ERROR_REENTRY,
	ABORT_GET_BETWEEN,
	ABORT_ACTION_BETWEEN,
	ABORT_ACTION_CONTINUED,
	ABORT_ACTION_AFTER_SRM,
	ABORT_PUT_SRM,
	ABORT_LEGACY_SINGLE,
};

static void abort_get_reentry(void)
{
	struct net_buf *buf = test_goep_pdu();
	int err = bt_obex_put(&test_client, true, buf);

	if (err != 0) {
		net_buf_unref(buf);
	}
	zassert_equal(err, -EBUSY);
}

static void abort_sequence(enum abort_sequence sequence)
{
	static const uint8_t description[] = {0, 'x', 0, 0};
	static const uint8_t expected[] = {0x05, 0, 7, 0, 'x', 0, 0};
	static const uint8_t error_headers[] = {0x05, 0, 11, 0, 'o', 0, 'l', 0, 'd', 0, 0};
	static const uint8_t object_type[] = "text/plain";
	static const uint8_t source_name[] = {0, 'a', 0, 0};
	static const uint8_t destination_name[] = {0, 'b', 0, 0};
	bool srm_get = sequence == ABORT_GET_SRM_CONTINUE || sequence == ABORT_GET_SRM_TERMINAL ||
		       sequence == ABORT_GET_SRM_SINGLE || sequence == ABORT_GET_SRM_ERROR ||
		       sequence == ABORT_GET_SRM_FAILURE ||
		       sequence == ABORT_GET_SRM_REPEATED_ERROR ||
		       sequence == ABORT_GET_SRM_ERROR_CONTINUE ||
		       sequence == ABORT_GET_SRM_TERMINAL_FAILURE ||
		       sequence == ABORT_GET_SRM_ERROR_HEADERS ||
		       sequence == ABORT_GET_SRM_ERROR_REENTRY;
	bool abort_failure = sequence == ABORT_GET_SRM_FAILURE ||
			     sequence == ABORT_GET_SRM_REPEATED_ERROR ||
			     sequence == ABORT_GET_SRM_ERROR_CONTINUE ||
			     sequence == ABORT_GET_SRM_TERMINAL_FAILURE;
	bool get = sequence == ABORT_GET_PENDING || sequence == ABORT_GET_TERMINAL ||
		   sequence == ABORT_GET_BETWEEN || sequence == ABORT_GET_ERROR || srm_get;
	bool action = sequence == ABORT_ACTION_BETWEEN || sequence == ABORT_ACTION_CONTINUED ||
		      sequence == ABORT_ACTION_AFTER_SRM;
	bool srm = sequence == ABORT_PUT_SRM || srm_get;
	bool continued = sequence == ABORT_ACTION_CONTINUED;
	bool between = sequence == ABORT_GET_BETWEEN || sequence == ABORT_ACTION_BETWEEN ||
		       continued || srm;
	bool legacy = sequence == ABORT_LEGACY_SINGLE;
	bool empty_abort = legacy;
	bool old_response = (!between && !legacy) || continued ||
			    (srm_get && sequence != ABORT_GET_SRM_SINGLE && !abort_failure);
	bool terminal = sequence == ABORT_GET_TERMINAL || sequence == ABORT_GET_SRM_TERMINAL;
	bool empty_end = sequence == ABORT_GET_SRM_TERMINAL;
	bool old_error = sequence == ABORT_GET_ERROR || sequence == ABORT_GET_SRM_ERROR ||
			 sequence == ABORT_GET_SRM_ERROR_HEADERS ||
			 sequence == ABORT_GET_SRM_ERROR_REENTRY;
	enum test_goep_operation operation = get ? TEST_GOEP_GET :
					   action ? TEST_GOEP_ACTION : TEST_GOEP_PUT;
	uint8_t old_code = terminal ? BT_OBEX_RSP_CODE_SUCCESS : BT_OBEX_RSP_CODE_CONTINUE;
	uint16_t old_length = old_error || !get ? 0 : empty_end ? 3 : 4;
	struct net_buf *buf;

	if (old_error) {
		old_code = BT_OBEX_RSP_CODE_FORBIDDEN;
	}
	if (sequence == ABORT_GET_SRM_ERROR_HEADERS) {
		old_length = sizeof(error_headers);
	}
	test_goep_open();
	if (sequence == ABORT_ACTION_AFTER_SRM) {
		if (test_goep_is_client()) {
			buf = test_goep_pdu();
			zassert_ok(bt_obex_add_header_srm(buf, 1));
			test_goep_send(bt_obex_put(&test_client, true, buf), buf);
			test_goep_expect(TEST_GOEP_PUT, BT_OBEX_RSP_CODE_SUCCESS);
		} else {
			test_goep_expect(TEST_GOEP_PUT, true);
			buf = test_goep_pdu();
			zassert_ok(bt_obex_add_header_srm(buf, 1));
			test_goep_send(bt_obex_put_rsp(&test_server, BT_OBEX_RSP_CODE_SUCCESS, buf),
				       buf);
		}
	}
	if (test_goep_is_client()) {
		buf = test_goep_pdu();
		if (srm) {
			zassert_ok(bt_obex_add_header_srm(buf, 1));
		}
		if (get) {
			zassert_ok(bt_obex_add_header_type(buf, sizeof(object_type), object_type));
			test_goep_send(bt_obex_get(&test_client, true, buf), buf);
		} else if (action) {
			zassert_ok(bt_obex_add_header_action_id(buf, BT_OBEX_ACTION_COPY));
			zassert_ok(bt_obex_add_header_name(buf, sizeof(source_name), source_name));
			zassert_ok(bt_obex_add_header_dest_name(buf, sizeof(destination_name),
						     destination_name));
			test_goep_send(bt_obex_action(&test_client, false, buf), buf);
		} else {
			test_goep_send(bt_obex_put(&test_client, false, buf), buf);
		}
		if (between) {
			buf = test_goep_expect(operation, BT_OBEX_RSP_CODE_CONTINUE);
			if (srm) {
				uint8_t value;

				zassert_ok(bt_obex_get_header_srm(buf, &value));
				zassert_equal(value, 1);
				if (!get) {
					buf = test_goep_pdu();
					test_goep_send(bt_obex_put(&test_client, false, buf), buf);
				}
			} else if (continued) {
				buf = test_goep_pdu();
				test_goep_send(bt_obex_action(&test_client, false, buf), buf);
			}
		}
	} else {
		test_goep_expect(operation, get);
		if (between) {
			buf = test_goep_pdu();
			if (srm) {
				zassert_ok(bt_obex_add_header_srm(buf, 1));
				if (get) {
					test_goep_send(bt_obex_get_rsp(&test_server,
							BT_OBEX_RSP_CODE_CONTINUE, buf), buf);
				} else {
					test_goep_send(bt_obex_put_rsp(&test_server,
							BT_OBEX_RSP_CODE_CONTINUE, buf), buf);
					test_goep_expect(TEST_GOEP_PUT, false);
				}
			} else if (get) {
				test_goep_send(bt_obex_get_rsp(&test_server,
							     BT_OBEX_RSP_CODE_CONTINUE, buf), buf);
			} else {
				test_goep_send(bt_obex_action_rsp(&test_server,
								BT_OBEX_RSP_CODE_CONTINUE, buf),
					       buf);
				if (continued) {
					test_goep_expect(TEST_GOEP_ACTION, false);
				}
			}
		}
	}
	test_br_sync();
	if (test_goep_is_client()) {
		int64_t started = k_uptime_get();

		if (sequence == ABORT_GET_SRM_ERROR_REENTRY) {
			test_goep_get_hook = abort_get_reentry;
		}
		buf = test_goep_pdu();
		test_goep_send(bt_obex_abort(&test_client, buf), buf);
		if (abort_failure) {
			if (sequence == ABORT_GET_SRM_TERMINAL_FAILURE) {
				buf = test_goep_expect(TEST_GOEP_GET, BT_OBEX_RSP_CODE_SUCCESS);
				zassert_equal(buf->len, 3);
			}
			test_goep_expect_transport_closed();
			if (sequence != ABORT_GET_SRM_FAILURE) {
				zassert_true(k_uptime_get() - started < 5000,
					     "Unambiguous failure waited for quarantine timeout");
			}
			test_goep_close(false);
			return;
		}
		if (old_response) {
			buf = test_goep_expect(operation, old_code);
			zassert_equal(buf->len, old_length);
			if (sequence == ABORT_GET_SRM_ERROR_HEADERS) {
				zassert_mem_equal(buf->data, error_headers, sizeof(error_headers));
			}
			if (sequence == ABORT_GET_SRM_ERROR_REENTRY) {
				zassert_is_null(test_goep_get_hook);
			}
			if (empty_end) {
				const uint8_t *body;
				uint16_t length = UINT16_MAX;

				zassert_ok(bt_obex_get_header_end_body(buf, &length, &body));
				zassert_equal(length, 0);
			}
		}
		buf = test_goep_expect(TEST_GOEP_ABORT, BT_OBEX_RSP_CODE_SUCCESS);
		zassert_equal(buf->len, empty_abort ? 0 : sizeof(expected));
		if (!empty_abort) {
			zassert_mem_equal(buf->data, expected, sizeof(expected));
		}
		buf = test_goep_pdu();
		test_goep_send(bt_obex_put(&test_client, true, buf), buf);
		test_goep_expect(TEST_GOEP_PUT, BT_OBEX_RSP_CODE_SUCCESS);
	} else {
		buf = test_goep_expect(TEST_GOEP_ABORT, 0);
		if (abort_failure) {
			int64_t started;

			if (sequence == ABORT_GET_SRM_TERMINAL_FAILURE) {
				buf = test_goep_pdu();
				net_buf_add_u8(buf, BT_OBEX_RSP_CODE_SUCCESS);
				net_buf_add_be16(buf, 6);
				net_buf_add_u8(buf, 0x49);
				net_buf_add_be16(buf, 3);
				test_goep_send_raw(buf);
			}
			started = k_uptime_get();
			buf = test_goep_pdu();
			net_buf_add_u8(buf, BT_OBEX_RSP_CODE_FORBIDDEN);
			net_buf_add_be16(buf, 3);
			test_goep_send_raw(buf);
			if (sequence == ABORT_GET_SRM_REPEATED_ERROR ||
			    sequence == ABORT_GET_SRM_ERROR_CONTINUE) {
				uint8_t code = sequence == ABORT_GET_SRM_REPEATED_ERROR ?
					       BT_OBEX_RSP_CODE_FORBIDDEN :
					       BT_OBEX_RSP_CODE_CONTINUE;

				buf = test_goep_pdu();
				net_buf_add_u8(buf, code);
				net_buf_add_be16(buf, 3);
				test_goep_send_raw(buf);
			}
			test_goep_expect_transport_closed();
			if (sequence == ABORT_GET_SRM_FAILURE) {
				zassert_true(k_uptime_get() - started >= 5000,
					     "Ambiguous error disconnected before the approved deadline");
				zassert_true(k_uptime_get() - started < 7000,
					     "Five-second timeout exceeded two-second delivery tolerance");
			}
			test_goep_close(false);
			return;
		}
		if (old_response) {
			buf = test_goep_pdu();
			net_buf_add_u8(buf, old_code);
			net_buf_add_be16(buf, old_length + 3);
			if (sequence == ABORT_GET_SRM_ERROR_HEADERS) {
				net_buf_add_mem(buf, error_headers, sizeof(error_headers));
			}
			if (get && !old_error) {
				net_buf_add_u8(buf, terminal ? 0x49 : 0x48);
				net_buf_add_be16(buf, old_length);
				if (!empty_end) {
					net_buf_add_u8(buf, 0x7b);
				}
			}
			test_goep_send_raw(buf);
		}
		buf = test_goep_pdu();
		if (!empty_abort) {
			zassert_ok(bt_obex_add_header_description(buf, sizeof(description),
							description));
		}
		test_goep_send(bt_obex_abort_rsp(&test_server, BT_OBEX_RSP_CODE_SUCCESS, buf), buf);
		test_goep_expect(TEST_GOEP_PUT, true);
		buf = test_goep_pdu();
		test_goep_send(bt_obex_put_rsp(&test_server, BT_OBEX_RSP_CODE_SUCCESS, buf), buf);
	}
	test_goep_close(false);
}

ZTEST(goep_client_abort_matrix, test_put_pending)
{
	abort_sequence(ABORT_PUT_PENDING);
}
ZTEST(goep_server_abort_matrix, test_put_pending)
{
	abort_sequence(ABORT_PUT_PENDING);
}
ZTEST(goep_client_abort_matrix, test_get_pending)
{
	abort_sequence(ABORT_GET_PENDING);
}
ZTEST(goep_server_abort_matrix, test_get_pending)
{
	abort_sequence(ABORT_GET_PENDING);
}
ZTEST(goep_client_abort_matrix, test_get_terminal)
{
	abort_sequence(ABORT_GET_TERMINAL);
}
ZTEST(goep_server_abort_matrix, test_get_terminal)
{
	abort_sequence(ABORT_GET_TERMINAL);
}
ZTEST(goep_client_abort_matrix, test_get_between)
{
	abort_sequence(ABORT_GET_BETWEEN);
}
ZTEST(goep_server_abort_matrix, test_get_between)
{
	abort_sequence(ABORT_GET_BETWEEN);
}
ZTEST(goep_client_abort_matrix, test_action_between)
{
	abort_sequence(ABORT_ACTION_BETWEEN);
}
ZTEST(goep_server_abort_matrix, test_action_between)
{
	abort_sequence(ABORT_ACTION_BETWEEN);
}
ZTEST(goep_client_abort_matrix, test_action_continued)
{
	abort_sequence(ABORT_ACTION_CONTINUED);
}
ZTEST(goep_server_abort_matrix, test_action_continued)
{
	abort_sequence(ABORT_ACTION_CONTINUED);
}
ZTEST(goep_client_abort_matrix, test_action_after_srm)
{
	abort_sequence(ABORT_ACTION_AFTER_SRM);
}
ZTEST(goep_server_abort_matrix, test_action_after_srm)
{
	abort_sequence(ABORT_ACTION_AFTER_SRM);
}
ZTEST(goep_client_abort_matrix, test_srm_put)
{
	abort_sequence(ABORT_PUT_SRM);
}
ZTEST(goep_server_abort_matrix, test_srm_put)
{
	abort_sequence(ABORT_PUT_SRM);
}
ZTEST(goep_client_abort_matrix, test_legacy_single)
{
	abort_sequence(ABORT_LEGACY_SINGLE);
}
ZTEST(goep_server_abort_matrix, test_legacy_single)
{
	abort_sequence(ABORT_LEGACY_SINGLE);
}

ZTEST(goep_client_abort_matrix, test_get_error)
{
	abort_sequence(ABORT_GET_ERROR);
}
ZTEST(goep_server_abort_matrix, test_get_error)
{
	abort_sequence(ABORT_GET_ERROR);
}
ZTEST(goep_client_abort_matrix, test_srm_get_continue)
{
	abort_sequence(ABORT_GET_SRM_CONTINUE);
}
ZTEST(goep_server_abort_matrix, test_srm_get_continue)
{
	abort_sequence(ABORT_GET_SRM_CONTINUE);
}
ZTEST(goep_client_abort_matrix, test_srm_get_terminal)
{
	abort_sequence(ABORT_GET_SRM_TERMINAL);
}
ZTEST(goep_server_abort_matrix, test_srm_get_terminal)
{
	abort_sequence(ABORT_GET_SRM_TERMINAL);
}
ZTEST(goep_client_abort_matrix, test_srm_get_single)
{
	abort_sequence(ABORT_GET_SRM_SINGLE);
}
ZTEST(goep_server_abort_matrix, test_srm_get_single)
{
	abort_sequence(ABORT_GET_SRM_SINGLE);
}

ZTEST_SUITE(goep_client_abort_matrix, NULL, test_goep_client_setup, NULL, NULL, test_goep_teardown);
ZTEST_SUITE(goep_server_abort_matrix, NULL, test_goep_server_setup, NULL, NULL, test_goep_teardown);

ZTEST(goep_client_abort_errors, test_old_get_error)
{
	abort_sequence(ABORT_GET_SRM_ERROR);
}

ZTEST(goep_server_abort_errors, test_old_get_error)
{
	abort_sequence(ABORT_GET_SRM_ERROR);
}

ZTEST(goep_client_abort_errors, test_abort_failure)
{
	abort_sequence(ABORT_GET_SRM_FAILURE);
}

ZTEST(goep_server_abort_errors, test_abort_failure)
{
	abort_sequence(ABORT_GET_SRM_FAILURE);
}

ZTEST(goep_client_abort_errors, test_repeated_error)
{
	abort_sequence(ABORT_GET_SRM_REPEATED_ERROR);
}

ZTEST(goep_server_abort_errors, test_repeated_error)
{
	abort_sequence(ABORT_GET_SRM_REPEATED_ERROR);
}

ZTEST(goep_client_abort_errors, test_error_then_continue)
{
	abort_sequence(ABORT_GET_SRM_ERROR_CONTINUE);
}

ZTEST(goep_server_abort_errors, test_error_then_continue)
{
	abort_sequence(ABORT_GET_SRM_ERROR_CONTINUE);
}

ZTEST(goep_client_abort_errors, test_terminal_then_failure)
{
	abort_sequence(ABORT_GET_SRM_TERMINAL_FAILURE);
}

ZTEST(goep_server_abort_errors, test_terminal_then_failure)
{
	abort_sequence(ABORT_GET_SRM_TERMINAL_FAILURE);
}

ZTEST_SUITE(goep_client_abort_errors, NULL, test_goep_client_setup, NULL, NULL, test_goep_teardown);
ZTEST(goep_client_abort_errors, test_error_headers)
{
	abort_sequence(ABORT_GET_SRM_ERROR_HEADERS);
}

ZTEST(goep_server_abort_errors, test_error_headers)
{
	abort_sequence(ABORT_GET_SRM_ERROR_HEADERS);
}

ZTEST(goep_client_abort_errors, test_error_reentry)
{
	abort_sequence(ABORT_GET_SRM_ERROR_REENTRY);
}

ZTEST(goep_server_abort_errors, test_error_reentry)
{
	abort_sequence(ABORT_GET_SRM_ERROR_REENTRY);
}

ZTEST_SUITE(goep_server_abort_errors, NULL, test_goep_server_setup, NULL, NULL, test_goep_teardown);
