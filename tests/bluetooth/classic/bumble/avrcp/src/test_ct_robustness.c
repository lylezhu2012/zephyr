/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Robustness tests of the CT (device 0): the board-to-board avrcp_tg test_invalid_status_buf,
 * split into the status and the empty response parts, and extra tests of the AVRCP connection
 * handling and of the API arguments.
 */

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/net_buf.h>
#include <zephyr/bluetooth/classic/avrcp.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"
#include "test_values.h"

#include "classic/avctp_internal.h"
#include "classic/avrcp_internal.h"

static struct bt_avctp *test_avctp;

int __real_bt_avctp_connect(struct bt_conn *conn, uint16_t psm, struct bt_avctp *session);
int __wrap_bt_avctp_connect(struct bt_conn *conn, uint16_t psm, struct bt_avctp *session);

int __wrap_bt_avctp_connect(struct bt_conn *conn, uint16_t psm, struct bt_avctp *session)
{
	int err = __real_bt_avctp_connect(conn, psm, session);

	if ((err == 0) && (psm == BT_L2CAP_PSM_AVRCP)) {
		test_avctp = session;
	}

	return err;
}

static void play_status_cmd(uint8_t status)
{
	uint8_t tid = test_next_tid();

	TEST_OK(bt_avrcp_ct_get_play_status(test_ct, tid));
	test_ct_expect_status(EVT_CT_PLAY_STATUS, tid, status);
}

static void invalid_status(void)
{
	play_status_cmd(BT_AVRCP_STATUS_INVALID_PARAMETER);
	/* The TG first tries two status values that are not valid. */
	play_status_cmd(BT_AVRCP_STATUS_IN_TRANSITION);
	play_status_cmd(BT_AVRCP_STATUS_NOT_IMPLEMENTED);
}

/* b2b: avrcp_tg test_invalid_status_buf (GetPlayStatus part) */
ZTEST(avrcp_ct_robustness, test_001_invalid_status)
{
	test_ct_run_twice(invalid_status, false);
}

static void element_attrs_cmd(uint8_t status)
{
	struct bt_avrcp_get_element_attrs_cmd *cmd;
	struct net_buf *buf = test_vendor_buf();
	uint8_t tid = test_next_tid();

	cmd = net_buf_add(buf, sizeof(*cmd));
	sys_put_be64(0U, cmd->identifier);
	cmd->num_attrs = 0U;

	TEST_SEND(bt_avrcp_ct_get_element_attrs(test_ct, tid, buf), buf);
	test_ct_expect_status(EVT_CT_ELEMENT_ATTRS, tid, status);
}

static void empty_rsp(void)
{
	element_attrs_cmd(BT_AVRCP_STATUS_INVALID_PARAMETER);
	/* The TG first tries an empty response with the success status. */
	element_attrs_cmd(BT_AVRCP_STATUS_IN_TRANSITION);
	element_attrs_cmd(BT_AVRCP_STATUS_NOT_IMPLEMENTED);
}

/* b2b: avrcp_tg test_invalid_status_buf (GetElementAttributes part) */
ZTEST(avrcp_ct_robustness, test_002_empty_rsp)
{
	test_ct_run_twice(empty_rsp, false);
}

static void total_items_cmd(void)
{
	const struct bt_avrcp_get_total_number_of_items_rsp *rsp;
	struct test_event evt;
	uint8_t tid = test_next_tid();

	TEST_OK(bt_avrcp_ct_get_total_number_of_items(test_ct, tid, BT_AVRCP_SCOPE_VFS));
	test_ct_expect(EVT_CT_TOTAL_ITEMS, tid, &evt);
	zassert_equal(evt.len, sizeof(*rsp), "Length %u", evt.len);
	rsp = (const struct bt_avrcp_get_total_number_of_items_rsp *)evt.data;
	zassert_equal(evt.status, BT_AVRCP_STATUS_SUCCESS, "Status 0x%02x", evt.status);
	zassert_equal(sys_be16_to_cpu(rsp->uid_counter), TEST_UID_COUNTER, "UID counter %u",
		      sys_be16_to_cpu(rsp->uid_counter));
	zassert_equal(sys_be32_to_cpu(rsp->num_items), TEST_TOTAL_ITEMS, "%u items",
		      sys_be32_to_cpu(rsp->num_items));
}

/* Wait until the ACL connection object of the peer is released, before a new connection. */
static void wait_acl_released(void)
{
	for (int i = 0; i < 100; i++) {
		struct bt_conn *conn = bt_conn_lookup_addr_br(&peer_addr);

		if (conn == NULL) {
			return;
		}

		bt_conn_unref(conn);
		k_sleep(K_MSEC(50));
	}

	zassert_unreachable("ACL connection object not released");
}

/* Extra: the ACL link is lost with the control and the browsing channel connected. */
ZTEST(avrcp_ct_robustness, test_003_acl_disconnect_browsing)
{
	test_br_sync();
	test_avrcp_connect();
	test_browsing_connect();
	test_br_sync();
	TEST_OK(bt_avrcp_ct_get_total_number_of_items(test_ct, test_next_tid(),
						      BT_AVRCP_SCOPE_VFS));

	/* The TG releases the ACL link. */
	test_br_wait_disconnected(K_SECONDS(10));
	test_avrcp_wait_disconnected();
	test_browsing_wait_disconnected();
	zassert_is_null(test_ct, "AVRCP CT not released");
	zassert_is_null(test_tg, "AVRCP TG not released");

	/* Both channels connect again on a new link, paired again. */
	(void)bt_br_unpair(&peer_addr);
	wait_acl_released();
	test_br_connect();
	test_br_security(BT_SECURITY_L2);
	test_br_sync();
	test_avrcp_connect();
	test_browsing_connect();
	total_items_cmd();

	test_br_sync();
}

/*
 * Extra: the CT callbacks of this device have no browsing_disconnected callback (the TG callbacks
 * of the peer neither); the browsing channel is released and connected again.
 */
ZTEST(avrcp_ct_robustness, test_004_no_browsing_disconnected_cb)
{
	test_ct_cb.browsing_disconnected = NULL;

	test_br_sync();
	test_avrcp_connect();
	test_browsing_connect();

	/* Only the TG callbacks of this device report the release. */
	TEST_OK(bt_avrcp_browsing_disconnect(test_br_conn()));
	TEST_EXPECT(EVT_TG_BROWSING_DISCONNECTED, NULL);
	test_br_sync();
	test_evt_none(EVT_CT_BROWSING_DISCONNECTED, K_NO_WAIT);

	/* The control channel still works, then a new browsing channel works. */
	play_status_cmd(BT_AVRCP_STATUS_INVALID_PARAMETER);
	test_avrcp_restore_cb();
	test_browsing_connect();
	total_items_cmd();

	test_avrcp_disconnect();
	test_browsing_wait_disconnected();
	test_br_sync();
}

/* Extra: every CT API refuses a NULL CT; the connection APIs refuse a NULL connection. */
ZTEST(avrcp_ct_robustness, test_005_null_instance)
{
	struct net_buf *buf;

	test_br_sync();

	TEST_ERR(bt_avrcp_connect(NULL), -ENOTCONN);
	TEST_ERR(bt_avrcp_disconnect(NULL), -ENOTCONN);
	TEST_ERR(bt_avrcp_browsing_connect(NULL), -ENOTCONN);
	TEST_ERR(bt_avrcp_browsing_disconnect(NULL), -ENOTCONN);

	TEST_ERR(bt_avrcp_ct_get_caps(NULL, 0U, BT_AVRCP_CAP_COMPANY_ID), -EINVAL);
	TEST_ERR(bt_avrcp_ct_get_unit_info(NULL, 0U), -EINVAL);
	TEST_ERR(bt_avrcp_ct_get_subunit_info(NULL, 0U), -EINVAL);
	TEST_ERR(bt_avrcp_ct_passthrough(NULL, 0U, BT_AVRCP_OPID_PLAY, BT_AVRCP_BUTTON_PRESSED,
					 NULL, 0U),
		 -EINVAL);
	TEST_ERR(bt_avrcp_ct_set_browsed_player(NULL, 0U, TEST_PLAYER_UTF8), -EINVAL);
	TEST_ERR(bt_avrcp_ct_register_notification(NULL, 0U, BT_AVRCP_EVT_VOLUME_CHANGED, 0U,
						   test_ct_notify_changed),
		 -EINVAL);
	TEST_ERR(bt_avrcp_ct_list_player_app_setting_attrs(NULL, 0U), -EINVAL);
	TEST_ERR(bt_avrcp_ct_list_player_app_setting_vals(NULL, 0U, BT_AVRCP_PLAYER_ATTR_SCAN),
		 -EINVAL);
	TEST_ERR(bt_avrcp_ct_inform_batt_status_of_ct(NULL, 0U, BT_AVRCP_BATTERY_STATUS_NORMAL),
		 -EINVAL);
	TEST_ERR(bt_avrcp_ct_set_absolute_volume(NULL, 0U, TEST_VOLUME_HALF), -EINVAL);
	TEST_ERR(bt_avrcp_ct_get_play_status(NULL, 0U), -EINVAL);
	TEST_ERR(bt_avrcp_ct_set_addressed_player(NULL, 0U, 1U), -EINVAL);
	TEST_ERR(bt_avrcp_ct_get_total_number_of_items(NULL, 0U, BT_AVRCP_SCOPE_VFS), -EINVAL);

	/* The buffer stays with the caller when the call fails. */
	buf = test_vendor_buf();
	(void)memset(net_buf_add(buf, 16U), 0, 16U);
	TEST_ERR(bt_avrcp_ct_get_curr_player_app_setting_val(NULL, 0U, buf), -EINVAL);
	TEST_ERR(bt_avrcp_ct_set_player_app_setting_val(NULL, 0U, buf), -EINVAL);
	TEST_ERR(bt_avrcp_ct_get_player_app_setting_attr_text(NULL, 0U, buf), -EINVAL);
	TEST_ERR(bt_avrcp_ct_get_player_app_setting_val_text(NULL, 0U, buf), -EINVAL);
	TEST_ERR(bt_avrcp_ct_inform_displayable_char_set(NULL, 0U, buf), -EINVAL);
	TEST_ERR(bt_avrcp_ct_get_element_attrs(NULL, 0U, buf), -EINVAL);
	TEST_ERR(bt_avrcp_ct_play_item(NULL, 0U, buf), -EINVAL);
	TEST_ERR(bt_avrcp_ct_add_to_now_playing(NULL, 0U, buf), -EINVAL);
	TEST_ERR(bt_avrcp_ct_get_folder_items(NULL, 0U, buf), -EINVAL);
	TEST_ERR(bt_avrcp_ct_change_path(NULL, 0U, buf), -EINVAL);
	TEST_ERR(bt_avrcp_ct_get_item_attrs(NULL, 0U, buf), -EINVAL);
	TEST_ERR(bt_avrcp_ct_search(NULL, 0U, buf), -EINVAL);
	net_buf_unref(buf);

	test_br_sync();
}

/* Extra: connection requests in the wrong state. */
ZTEST(avrcp_ct_robustness, test_006_connection_state)
{
	struct bt_conn *conn;

	test_br_sync();
	conn = test_br_conn();

	TEST_ERR(bt_avrcp_browsing_connect(conn), -ENOTCONN);
	TEST_OK(bt_avrcp_connect(conn));
	TEST_ERR(bt_avrcp_connect(conn), -EALREADY);
	test_avrcp_wait_connected();

	TEST_OK(bt_avrcp_browsing_connect(conn));
	TEST_ERR(bt_avrcp_browsing_connect(conn), -EALREADY);
	test_browsing_wait_connected();
	total_items_cmd();

	/* bt_avrcp_disconnect() releases the browsing channel first. */
	test_avrcp_disconnect();
	test_browsing_wait_disconnected();
	test_br_sync();
}

ZTEST(avrcp_ct_robustness, test_007_send_failure_ownership)
{
	const size_t headroom_cases[] = {0U, 10U};
	struct net_buf *buf;

	test_br_sync();
	test_avrcp_connect();

	for (size_t case_index = 0; case_index < ARRAY_SIZE(headroom_cases); case_index++) {
		buf = test_vendor_buf();
		net_buf_reset(buf);
		net_buf_reserve(buf, headroom_cases[case_index]);
		net_buf_add_u8(buf, 1U);
		net_buf_add_u8(buf, BT_AVRCP_PLAYER_ATTR_SCAN);
		TEST_ERR(bt_avrcp_ct_get_curr_player_app_setting_val(test_ct, test_next_tid(), buf),
			 -ENOMEM);
		zassert_equal(buf->ref, 1U, "Caller buffer released after failed send");
		net_buf_unref(buf);
	}

	play_status_cmd(BT_AVRCP_STATUS_INVALID_PARAMETER);
	test_br_sync();
}

ZTEST(avrcp_ct_robustness, test_008_response_headroom)
{
	struct bt_avrcp_get_element_attrs_cmd *cmd;
	struct test_event evt;
	struct net_buf *buf;
	uint8_t tid;

	test_br_sync();
	test_avrcp_connect();

	buf = test_vendor_buf();
	cmd = net_buf_add(buf, sizeof(*cmd));
	sys_put_be64(0U, cmd->identifier);
	cmd->num_attrs = 0U;
	tid = test_next_tid();
	TEST_SEND(bt_avrcp_ct_get_element_attrs(test_ct, tid, buf), buf);
	test_ct_expect(EVT_CT_ELEMENT_ATTRS, tid, &evt);
	zassert_equal(evt.status, BT_AVRCP_STATUS_SUCCESS, "Status 0x%02x", evt.status);
	zassert_equal(evt.len, 1U, "Length %u", evt.len);
	zassert_equal(evt.data[0], 0U, "Unexpected attribute count");

	test_br_sync();
}

static void notification_data_cmd(uint8_t event_id)
{
	struct test_event evt;
	uint8_t tid = test_next_tid();

	TEST_OK(bt_avrcp_ct_register_notification(test_ct, tid, event_id, 0U,
						  test_ct_notify_changed));
	test_ct_expect(EVT_CT_NOTIFICATION, tid, &evt);
	zassert_equal(evt.status, BT_AVRCP_STATUS_SUCCESS, "Status 0x%02x", evt.status);
	zassert_equal(evt.event_id, event_id, "Event 0x%02x", evt.event_id);
	test_check_event_data(&evt, false);
}

ZTEST(avrcp_ct_robustness, test_009_null_notification_data)
{
	test_br_sync();
	test_avrcp_connect();
	notification_data_cmd(BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED);
	test_br_sync();
}

ZTEST(avrcp_ct_robustness, test_010_null_notification_attrs)
{
	test_br_sync();
	test_avrcp_connect();
	notification_data_cmd(BT_AVRCP_EVT_PLAYER_APP_SETTING_CHANGED);
	test_br_sync();
}

static void inject_truncated_notification(uint8_t tid, bool missing_count)
{
	struct bt_avrcp_avc_vendor_pdu *pdu;
	struct bt_avrcp_header *header;
	struct net_buf buf = {0};
	size_t payload_len = missing_count ? 1U : 3U;
	size_t packet_len = sizeof(*header) + sizeof(*pdu) + payload_len;
	long page_size = sysconf(_SC_PAGESIZE);
	uint8_t *pages;
	uint8_t *packet;

	zassert_true(page_size > 0, "Invalid host page size");
	pages = mmap(NULL, 2U * page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1,
		     0);
	zassert_true(pages != MAP_FAILED, "Cannot allocate guarded input");
	zassert_ok(mprotect(pages + page_size, page_size, PROT_NONE),
		   "Cannot protect input boundary");
	packet = pages + page_size - packet_len;
	memset(packet, 0, packet_len);
	header = (void *)packet;
	BT_AVRCP_HDR_SET_CTYPE_OR_RSP(header, BT_AVRCP_RSP_INTERIM);
	BT_AVRCP_HDR_SET_SUBUNIT_TYPE(header, BT_AVRCP_SUBUNIT_TYPE_PANEL);
	BT_AVRCP_HDR_SET_SUBUNIT_ID(header, BT_AVRCP_SUBUNIT_ID_ZERO);
	header->opcode = BT_AVRCP_OPC_VENDOR_DEPENDENT;
	pdu = (void *)(packet + sizeof(*header));
	sys_put_be24(BT_AVRCP_COMPANY_ID_BLUETOOTH_SIG, pdu->company_id);
	pdu->pdu_id = 0x31U;
	pdu->pkt_type = BT_AVRCP_PKT_TYPE_SINGLE;
	pdu->param_len = sys_cpu_to_be16(payload_len);
	pdu->param[0] = BT_AVRCP_EVT_PLAYER_APP_SETTING_CHANGED;
	if (!missing_count) {
		pdu->param[1] = 1U;
		pdu->param[2] = BT_AVRCP_PLAYER_ATTR_SCAN;
	}

	net_buf_simple_init_with_data(&buf.b, packet, packet_len);
	zassert_not_null(test_avctp, "Control AVCTP session not captured");
	TEST_OK(test_avctp->ops->recv(test_avctp, &buf, BT_AVCTP_RESPONSE, tid));
	zassert_ok(munmap(pages, 2U * page_size), "Cannot release guarded input");
	test_evt_reset();
}

static void truncated_notification(bool missing_count)
{
	struct test_event evt;
	uint8_t event_id = BT_AVRCP_EVT_PLAYER_APP_SETTING_CHANGED;
	uint8_t tid = test_next_tid();

	test_br_sync();
	test_avrcp_connect();
	TEST_OK(bt_avrcp_ct_register_notification(test_ct, tid, event_id, 0U,
						  test_ct_notify_changed));
	test_br_sync();
	inject_truncated_notification(tid, missing_count);
	test_br_sync();
	test_ct_expect(EVT_CT_NOTIFICATION, tid, &evt);
	zassert_equal(evt.status, BT_AVRCP_STATUS_SUCCESS, "Status 0x%02x", evt.status);
	zassert_equal(evt.event_id, event_id, "Event 0x%02x", evt.event_id);
	test_check_event_data(&evt, false);
	test_br_sync();
}

ZTEST(avrcp_ct_robustness, test_011_missing_notification_count)
{
	truncated_notification(true);
}

ZTEST(avrcp_ct_robustness, test_012_truncated_notification_attrs)
{
	truncated_notification(false);
}

ZTEST(avrcp_ct_robustness, test_013_pending_vendor_responses)
{
	uint8_t tids[3];
	struct test_event evt;

	test_br_sync();
	test_avrcp_connect();
	for (size_t index = 0; index < ARRAY_SIZE(tids); index++) {
		tids[index] = test_next_tid();
		TEST_OK(bt_avrcp_ct_get_caps(test_ct, tids[index], BT_AVRCP_CAP_COMPANY_ID));
	}
	test_br_sync();
	for (size_t index = 0; index < ARRAY_SIZE(tids); index++) {
		const struct bt_avrcp_get_caps_rsp *rsp;

		test_ct_expect(EVT_CT_GET_CAPS, tids[index], &evt);
		zassert_equal(evt.status, BT_AVRCP_STATUS_SUCCESS, "Response status");
		zassert_equal(evt.len, sizeof(*rsp) + 3U, "Response length");
		rsp = (const struct bt_avrcp_get_caps_rsp *)evt.data;
		zassert_equal(rsp->cap_id, BT_AVRCP_CAP_COMPANY_ID, "Capability ID");
		zassert_equal(rsp->cap_cnt, 1U, "Capability count");
		zassert_equal(sys_get_be24(&evt.data[sizeof(*rsp)]),
			      BT_AVRCP_COMPANY_ID_BLUETOOTH_SIG, "Company ID");
	}
	test_br_sync();
}

static void *suite_setup(void)
{
	test_avrcp_suite_setup(TEST_BR_INITIATOR);

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

ZTEST_SUITE(avrcp_ct_robustness, NULL, suite_setup, suite_before, NULL, suite_teardown);
