/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Robustness tests of the TG (device 1), counterpart of test_ct_robustness.c: responses with
 * status values that are not valid or without parameters, as the board-to-board avrcp_tg
 * test_invalid_status_buf, and extra tests of the connection handling and of the API arguments.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/net_buf.h>
#include <zephyr/bluetooth/classic/avrcp.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"
#include "test_values.h"

/* Status values outside the AVRCP status codes, refused by the TG. */
#define TEST_STATUS_INVALID_1 0x17U
#define TEST_STATUS_INVALID_2 0xffU

static void invalid_status(void)
{
	struct test_event evt;

	/* Without parameters: the status of a reject does not need a buffer. */
	TEST_EXPECT(EVT_TG_PLAY_STATUS, &evt);
	TEST_OK(bt_avrcp_tg_get_play_status(test_tg, evt.tid, BT_AVRCP_STATUS_INVALID_PARAMETER,
					    NULL));

	TEST_EXPECT(EVT_TG_PLAY_STATUS, &evt);
	TEST_ERR(bt_avrcp_tg_get_play_status(test_tg, evt.tid, TEST_STATUS_INVALID_1, NULL),
		 -EINVAL);
	TEST_ERR(bt_avrcp_tg_get_play_status(test_tg, evt.tid, TEST_STATUS_INVALID_2, NULL),
		 -EINVAL);
	TEST_OK(bt_avrcp_tg_get_play_status(test_tg, evt.tid, BT_AVRCP_STATUS_IN_TRANSITION, NULL));

	TEST_EXPECT(EVT_TG_PLAY_STATUS, &evt);
	TEST_OK(bt_avrcp_tg_get_play_status(test_tg, evt.tid, BT_AVRCP_STATUS_NOT_IMPLEMENTED,
					    NULL));
}

ZTEST(avrcp_tg_robustness, test_001_invalid_status)
{
	test_tg_run_twice(invalid_status, false);
}

static void send_empty(uint8_t tid, uint8_t status)
{
	struct net_buf *buf = test_vendor_buf();

	TEST_SEND(bt_avrcp_tg_get_element_attrs(test_tg, tid, status, buf), buf);
}

static void empty_rsp(void)
{
	struct test_event evt;
	struct net_buf *buf;

	TEST_EXPECT(EVT_TG_ELEMENT_ATTRS, &evt);
	send_empty(evt.tid, BT_AVRCP_STATUS_INVALID_PARAMETER);

	/* A success response needs the number of attributes. */
	TEST_EXPECT(EVT_TG_ELEMENT_ATTRS, &evt);
	buf = test_vendor_buf();
	TEST_ERR(bt_avrcp_tg_get_element_attrs(test_tg, evt.tid, BT_AVRCP_STATUS_SUCCESS, buf),
		 -EINVAL);
	net_buf_unref(buf);
	send_empty(evt.tid, BT_AVRCP_STATUS_IN_TRANSITION);

	TEST_EXPECT(EVT_TG_ELEMENT_ATTRS, &evt);
	send_empty(evt.tid, BT_AVRCP_STATUS_NOT_IMPLEMENTED);
}

ZTEST(avrcp_tg_robustness, test_002_empty_rsp)
{
	test_tg_run_twice(empty_rsp, false);
}

static void total_items_rsp(void)
{
	struct test_event evt;
	struct net_buf *buf;

	TEST_EXPECT(EVT_TG_TOTAL_ITEMS, &evt);
	buf = test_pdu_buf();
	net_buf_add_u8(buf, BT_AVRCP_STATUS_SUCCESS);
	net_buf_add_be16(buf, TEST_UID_COUNTER);
	net_buf_add_be32(buf, TEST_TOTAL_ITEMS);
	TEST_SEND(bt_avrcp_tg_get_total_number_of_items(test_tg, evt.tid, buf), buf);
}

ZTEST(avrcp_tg_robustness, test_003_acl_disconnect_browsing)
{
	test_br_sync();
	test_avrcp_wait_connected();
	test_browsing_wait_connected();
	test_br_sync();
	TEST_EXPECT(EVT_TG_TOTAL_ITEMS, NULL);

	/* Release the ACL link and the link key, so that the CT pairs again. */
	TEST_OK(bt_br_unpair(&peer_addr));
	test_br_wait_disconnected(K_SECONDS(10));
	test_avrcp_wait_disconnected();
	test_browsing_wait_disconnected();
	zassert_is_null(test_ct, "AVRCP CT not released");
	zassert_is_null(test_tg, "AVRCP TG not released");

	test_br_wait_connected(K_SECONDS(30));
	test_br_security(BT_SECURITY_L2);
	test_br_sync();
	test_avrcp_wait_connected();
	test_browsing_wait_connected();
	total_items_rsp();

	test_br_sync();
}

ZTEST(avrcp_tg_robustness, test_004_no_browsing_disconnected_cb)
{
	struct test_event evt;

	test_tg_cb.browsing_disconnected = NULL;

	test_br_sync();
	test_avrcp_wait_connected();
	test_browsing_wait_connected();

	/* Only the CT callbacks of this device report the release. */
	TEST_EXPECT(EVT_CT_BROWSING_DISCONNECTED, NULL);
	test_br_sync();
	test_evt_none(EVT_TG_BROWSING_DISCONNECTED, K_NO_WAIT);

	TEST_EXPECT(EVT_TG_PLAY_STATUS, &evt);
	TEST_OK(bt_avrcp_tg_get_play_status(test_tg, evt.tid, BT_AVRCP_STATUS_INVALID_PARAMETER,
					    NULL));
	test_avrcp_restore_cb();
	test_browsing_wait_connected();
	total_items_rsp();

	test_avrcp_wait_disconnected();
	test_browsing_wait_disconnected();
	test_br_sync();
}

ZTEST(avrcp_tg_robustness, test_005_null_instance)
{
	struct bt_avrcp_unit_info_rsp unit = {
		.unit_type = BT_AVRCP_SUBUNIT_TYPE_PANEL,
		.company_id = BT_AVRCP_COMPANY_ID_BLUETOOTH_SIG,
	};
	struct bt_avrcp_event_data data;
	struct net_buf *buf;

	test_br_sync();

	test_event_data(BT_AVRCP_EVT_VOLUME_CHANGED, false, &data);

	TEST_ERR(bt_avrcp_tg_send_unit_info_rsp(NULL, 0U, &unit), -EINVAL);
	TEST_ERR(bt_avrcp_tg_send_subunit_info_rsp(NULL, 0U), -EINVAL);
	TEST_ERR(bt_avrcp_tg_notification(NULL, 0U, BT_AVRCP_STATUS_SUCCESS,
					  BT_AVRCP_EVT_VOLUME_CHANGED, &data),
		 -EINVAL);
	TEST_ERR(bt_avrcp_tg_set_player_app_setting_val(NULL, 0U, BT_AVRCP_STATUS_SUCCESS),
		 -EINVAL);
	TEST_ERR(bt_avrcp_tg_inform_displayable_char_set(NULL, 0U, BT_AVRCP_STATUS_SUCCESS),
		 -EINVAL);
	TEST_ERR(bt_avrcp_tg_inform_batt_status_of_ct(NULL, 0U, BT_AVRCP_STATUS_SUCCESS), -EINVAL);
	TEST_ERR(bt_avrcp_tg_absolute_volume(NULL, 0U, BT_AVRCP_STATUS_SUCCESS, TEST_VOLUME_HALF),
		 -EINVAL);
	TEST_ERR(bt_avrcp_tg_set_addressed_player(NULL, 0U, BT_AVRCP_STATUS_SUCCESS), -EINVAL);
	TEST_ERR(bt_avrcp_tg_play_item(NULL, 0U, BT_AVRCP_STATUS_SUCCESS), -EINVAL);
	TEST_ERR(bt_avrcp_tg_add_to_now_playing(NULL, 0U, BT_AVRCP_STATUS_SUCCESS), -EINVAL);
	TEST_ERR(bt_avrcp_tg_change_path(NULL, 0U, BT_AVRCP_STATUS_SUCCESS, TEST_CHANGE_PATH_ITEMS),
		 -EINVAL);
	TEST_ERR(bt_avrcp_tg_browsing_general_reject(NULL, 0U, BT_AVRCP_STATUS_INVALID_COMMAND),
		 -EINVAL);

	/* The buffer stays with the caller when the call fails. */
	buf = test_vendor_buf();
	(void)memset(net_buf_add(buf, 16U), 0, 16U);
	TEST_ERR(bt_avrcp_tg_get_caps(NULL, 0U, BT_AVRCP_STATUS_SUCCESS, buf), -EINVAL);
	TEST_ERR(bt_avrcp_tg_list_player_app_setting_attrs(NULL, 0U, BT_AVRCP_STATUS_SUCCESS, buf),
		 -EINVAL);
	TEST_ERR(bt_avrcp_tg_list_player_app_setting_vals(NULL, 0U, BT_AVRCP_STATUS_SUCCESS, buf),
		 -EINVAL);
	TEST_ERR(
		bt_avrcp_tg_get_curr_player_app_setting_val(NULL, 0U, BT_AVRCP_STATUS_SUCCESS, buf),
		-EINVAL);
	TEST_ERR(bt_avrcp_tg_get_player_app_setting_attr_text(NULL, 0U, BT_AVRCP_STATUS_SUCCESS,
							      buf),
		 -EINVAL);
	TEST_ERR(
		bt_avrcp_tg_get_player_app_setting_val_text(NULL, 0U, BT_AVRCP_STATUS_SUCCESS, buf),
		-EINVAL);
	TEST_ERR(bt_avrcp_tg_get_element_attrs(NULL, 0U, BT_AVRCP_STATUS_SUCCESS, buf), -EINVAL);
	TEST_ERR(bt_avrcp_tg_get_play_status(NULL, 0U, BT_AVRCP_STATUS_SUCCESS, buf), -EINVAL);
	TEST_ERR(bt_avrcp_tg_send_passthrough_rsp(NULL, 0U, BT_AVRCP_RSP_ACCEPTED, buf), -EINVAL);
	TEST_ERR(bt_avrcp_tg_set_browsed_player(NULL, 0U, buf), -EINVAL);
	TEST_ERR(bt_avrcp_tg_get_folder_items(NULL, 0U, buf), -EINVAL);
	TEST_ERR(bt_avrcp_tg_get_item_attrs(NULL, 0U, buf), -EINVAL);
	TEST_ERR(bt_avrcp_tg_get_total_number_of_items(NULL, 0U, buf), -EINVAL);
	TEST_ERR(bt_avrcp_tg_search(NULL, 0U, buf), -EINVAL);
	net_buf_unref(buf);

	test_br_sync();
}

ZTEST(avrcp_tg_robustness, test_006_connection_state)
{
	struct bt_conn *conn;

	test_br_sync();
	conn = test_br_conn();

	/* The CT has connected the channels; requests from the TG are refused. */
	test_avrcp_wait_connected();
	TEST_ERR(bt_avrcp_connect(conn), -EALREADY);
	test_browsing_wait_connected();
	TEST_ERR(bt_avrcp_browsing_connect(conn), -EALREADY);
	total_items_rsp();

	test_avrcp_wait_disconnected();
	test_browsing_wait_disconnected();
	test_br_sync();
}

ZTEST(avrcp_tg_robustness, test_007_send_failure_ownership)
{
	struct test_event evt;

	test_br_sync();
	test_avrcp_wait_connected();
	TEST_EXPECT(EVT_TG_PLAY_STATUS, &evt);
	TEST_OK(bt_avrcp_tg_get_play_status(test_tg, evt.tid, BT_AVRCP_STATUS_INVALID_PARAMETER,
					    NULL));
	test_br_sync();
}

ZTEST(avrcp_tg_robustness, test_008_response_headroom)
{
	const size_t headroom_cases[] = {0U, 10U};
	struct test_event evt;
	struct net_buf *buf;

	test_br_sync();
	test_avrcp_wait_connected();
	TEST_EXPECT(EVT_TG_ELEMENT_ATTRS, &evt);

	for (size_t case_index = 0; case_index < ARRAY_SIZE(headroom_cases); case_index++) {
		buf = test_vendor_buf();
		net_buf_reset(buf);
		net_buf_reserve(buf, headroom_cases[case_index]);
		net_buf_add_u8(buf, 0U);
		TEST_SEND(bt_avrcp_tg_get_element_attrs(test_tg, evt.tid, BT_AVRCP_STATUS_SUCCESS,
							buf),
			  buf);
	}

	buf = test_vendor_buf();
	net_buf_add_u8(buf, 0U);
	TEST_SEND(bt_avrcp_tg_get_element_attrs(test_tg, evt.tid, BT_AVRCP_STATUS_SUCCESS, buf),
		  buf);
	test_br_sync();
}

ZTEST(avrcp_tg_robustness, test_009_null_notification_data)
{
	struct bt_avrcp_event_data data;
	struct test_event evt;
	uint8_t event_id = BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED;

	test_br_sync();
	test_avrcp_wait_connected();
	TEST_EXPECT(EVT_TG_REGISTER_NOTIFICATION, &evt);
	zassert_equal(evt.event_id, event_id, "Event 0x%02x", evt.event_id);
	TEST_ERR(
		bt_avrcp_tg_notification(test_tg, evt.tid, BT_AVRCP_STATUS_SUCCESS, event_id, NULL),
		-EINVAL);
	TEST_ERR(bt_avrcp_tg_notification(test_tg, evt.tid, BT_AVRCP_STATUS_NOT_IMPLEMENTED,
					  event_id, NULL),
		 -EINVAL);
	test_event_data(event_id, false, &data);
	TEST_OK(bt_avrcp_tg_notification(test_tg, evt.tid, BT_AVRCP_STATUS_SUCCESS, event_id,
					 &data));
	test_br_sync();
}

ZTEST(avrcp_tg_robustness, test_010_null_notification_attrs)
{
	struct bt_avrcp_event_data data = {0};
	struct test_event evt;
	uint8_t event_id = BT_AVRCP_EVT_PLAYER_APP_SETTING_CHANGED;

	test_br_sync();
	test_avrcp_wait_connected();
	TEST_EXPECT(EVT_TG_REGISTER_NOTIFICATION, &evt);
	zassert_equal(evt.event_id, event_id, "Event 0x%02x", evt.event_id);
	data.setting_changed.num_of_attr = 1U;
	data.setting_changed.attr_vals = NULL;
	TEST_ERR(bt_avrcp_tg_notification(test_tg, evt.tid, BT_AVRCP_STATUS_SUCCESS, event_id,
					  &data),
		 -EINVAL);
	test_event_data(event_id, false, &data);
	TEST_OK(bt_avrcp_tg_notification(test_tg, evt.tid, BT_AVRCP_STATUS_SUCCESS, event_id,
					 &data));
	test_br_sync();
}

static void truncated_notification_peer(void)
{
	struct bt_avrcp_event_data data;
	struct test_event evt;
	uint8_t event_id = BT_AVRCP_EVT_PLAYER_APP_SETTING_CHANGED;

	test_br_sync();
	test_avrcp_wait_connected();
	TEST_EXPECT(EVT_TG_REGISTER_NOTIFICATION, &evt);
	zassert_equal(evt.event_id, event_id, "Event 0x%02x", evt.event_id);
	test_br_sync();
	test_br_sync();
	test_event_data(event_id, false, &data);
	TEST_OK(bt_avrcp_tg_notification(test_tg, evt.tid, BT_AVRCP_STATUS_SUCCESS, event_id,
					 &data));
	test_br_sync();
}

ZTEST(avrcp_tg_robustness, test_011_missing_notification_count)
{
	truncated_notification_peer();
}

ZTEST(avrcp_tg_robustness, test_012_truncated_notification_attrs)
{
	truncated_notification_peer();
}

ZTEST(avrcp_tg_robustness, test_013_pending_vendor_responses)
{
	struct net_buf *bufs[3];
	uint8_t tids[ARRAY_SIZE(bufs)];
	int errors[ARRAY_SIZE(bufs)];
	struct test_event evt;

	test_br_sync();
	test_avrcp_wait_connected();
	for (size_t index = 0; index < ARRAY_SIZE(bufs); index++) {
		struct bt_avrcp_get_caps_rsp *rsp;

		TEST_EXPECT(EVT_TG_GET_CAPS, &evt);
		zassert_equal(evt.value, BT_AVRCP_CAP_COMPANY_ID, "Capability ID");
		tids[index] = evt.tid;
		bufs[index] = test_vendor_buf();
		rsp = net_buf_add(bufs[index], sizeof(*rsp));
		rsp->cap_id = BT_AVRCP_CAP_COMPANY_ID;
		rsp->cap_cnt = 1U;
		net_buf_add_be24(bufs[index], BT_AVRCP_COMPANY_ID_BLUETOOTH_SIG);
	}
	test_br_sync();
	k_sched_lock();
	for (size_t index = 0; index < ARRAY_SIZE(bufs); index++) {
		errors[index] = bt_avrcp_tg_get_caps(test_tg, tids[index], BT_AVRCP_STATUS_SUCCESS,
						     bufs[index]);
	}
	k_sched_unlock();
	for (size_t index = 0; index < ARRAY_SIZE(bufs); index++) {
		if (errors[index] != 0) {
			net_buf_unref(bufs[index]);
		}
	}
	for (size_t index = 0; index < ARRAY_SIZE(bufs); index++) {
		zassert_ok(errors[index], "Failed response %zu", index);
	}
	test_br_sync();
}

static void *suite_setup(void)
{
	test_avrcp_suite_setup(TEST_BR_ACCEPTOR);

	return NULL;
}

static void suite_before(void *f)
{
	ARG_UNUSED(f);

	test_avrcp_before();
}

static void suite_teardown(void *f)
{
	ARG_UNUSED(f);

	test_avrcp_suite_teardown();
}

ZTEST_SUITE(avrcp_tg_robustness, NULL, suite_setup, suite_before, NULL, suite_teardown);
