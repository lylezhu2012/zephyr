/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Control channel tests of the CT (device 0): the board-to-board avrcp_ct tests, merged with the
 * avrcp_tg tests of the same commands, whose checks of the responses are made here. Each test
 * body runs twice on a new AVRCP connection, released by the CT and then by the TG.
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

static void unit_info(void)
{
	struct test_event evt;
	uint8_t tid = test_next_tid();

	TEST_OK(bt_avrcp_ct_get_unit_info(test_ct, tid));
	test_ct_expect(EVT_CT_UNIT_INFO, tid, &evt);
	zassert_equal(evt.value, BT_AVRCP_SUBUNIT_TYPE_PANEL, "Unit type 0x%02x", evt.value);
	zassert_equal(evt.value2, BT_AVRCP_COMPANY_ID_BLUETOOTH_SIG, "Company ID 0x%06x",
		      evt.value2);
}

/* b2b: avrcp_ct unit_info_cmd, avrcp_tg unit_info_rsp */
ZTEST(avrcp_ct_control, test_001_unit_info)
{
	test_ct_run_twice(unit_info, false);
}

static void subunit_info(void)
{
	struct test_event evt;
	uint8_t tid = test_next_tid();

	TEST_OK(bt_avrcp_ct_get_subunit_info(test_ct, tid));
	test_ct_expect(EVT_CT_SUBUNIT_INFO, tid, &evt);
	zassert_equal(evt.value, BT_AVRCP_SUBUNIT_TYPE_PANEL, "Subunit type 0x%02x", evt.value);
	zassert_equal(evt.value2, 0U, "Max subunit ID %u", evt.value2);
}

/* b2b: avrcp_ct subunit_info_cmd, avrcp_tg subunit_info_rsp */
ZTEST(avrcp_ct_control, test_002_subunit_info)
{
	test_ct_run_twice(subunit_info, false);
}

static void passthrough_cmd(uint8_t opid, uint8_t state, const uint8_t *payload, uint8_t len,
			    bt_avrcp_rsp_t result, uint8_t rsp_opid, uint8_t rsp_state)
{
	const struct bt_avrcp_passthrough_rsp *rsp;
	struct test_event evt;
	uint8_t tid = test_next_tid();

	TEST_OK(bt_avrcp_ct_passthrough(test_ct, tid, opid, state, payload, len));
	test_ct_expect(EVT_CT_PASSTHROUGH, tid, &evt);
	zassert_equal(evt.value, result, "Result 0x%x", evt.value);

	rsp = (const struct bt_avrcp_passthrough_rsp *)evt.data;
	zassert_equal(BT_AVRCP_PASSTHROUGH_GET_OPID(rsp), rsp_opid, "Operation 0x%02x",
		      BT_AVRCP_PASSTHROUGH_GET_OPID(rsp));
	zassert_equal(BT_AVRCP_PASSTHROUGH_GET_STATE(rsp), rsp_state, "State %u",
		      BT_AVRCP_PASSTHROUGH_GET_STATE(rsp));
	zassert_equal(rsp->data_len, len, "Data length %u", rsp->data_len);
	zassert_equal(evt.len, sizeof(*rsp) + len, "Response length %u", evt.len);
	if (len > 0U) {
		zassert_mem_equal(rsp->data, payload, len, "Unexpected operation data");
	}
}

static void passthrough(void)
{
	struct bt_avrcp_passthrough_opvu_data opvu;

	passthrough_cmd(BT_AVRCP_OPID_PLAY, BT_AVRCP_BUTTON_PRESSED, NULL, 0U,
			BT_AVRCP_RSP_ACCEPTED, BT_AVRCP_OPID_PLAY, BT_AVRCP_BUTTON_PRESSED);

	/* The TG answers with the released state, as the board-to-board TG. */
	passthrough_cmd(BT_AVRCP_OPID_PAUSE, BT_AVRCP_BUTTON_PRESSED, NULL, 0U,
			BT_AVRCP_RSP_ACCEPTED, BT_AVRCP_OPID_PAUSE, BT_AVRCP_BUTTON_RELEASED);

	/* Extra: a vendor unique operation, not implemented by the TG. */
	sys_put_be24(BT_AVRCP_COMPANY_ID_BLUETOOTH_SIG, opvu.company_id);
	opvu.opid_vu = sys_cpu_to_be16(TEST_OPID_VU);
	passthrough_cmd(BT_AVRCP_OPID_VENDOR_UNIQUE, BT_AVRCP_BUTTON_RELEASED,
			(const uint8_t *)&opvu, sizeof(opvu), BT_AVRCP_RSP_NOT_IMPLEMENTED,
			BT_AVRCP_OPID_VENDOR_UNIQUE, BT_AVRCP_BUTTON_RELEASED);

	/* Extra: rejected by the TG. */
	passthrough_cmd(BT_AVRCP_OPID_STOP, BT_AVRCP_BUTTON_PRESSED, NULL, 0U,
			BT_AVRCP_RSP_REJECTED, BT_AVRCP_OPID_STOP, BT_AVRCP_BUTTON_PRESSED);
}

/* b2b: avrcp_ct play, pause; avrcp_tg play_rsp, pause_rsp */
ZTEST(avrcp_ct_control, test_003_passthrough)
{
	test_ct_run_twice(passthrough, false);
}

static void get_caps(void)
{
	static const uint8_t events[] = {TEST_EVENTS_SUPPORTED};
	const struct bt_avrcp_get_caps_rsp *rsp;
	struct test_event evt;
	uint8_t tid;

	tid = test_next_tid();
	TEST_OK(bt_avrcp_ct_get_caps(test_ct, tid, BT_AVRCP_CAP_COMPANY_ID));
	test_ct_expect(EVT_CT_GET_CAPS, tid, &evt);
	zassert_equal(evt.status, BT_AVRCP_STATUS_SUCCESS, "Status 0x%02x", evt.status);
	zassert_equal(evt.len, sizeof(*rsp) + BT_AVRCP_COMPANY_ID_SIZE, "Length %u", evt.len);
	rsp = (const struct bt_avrcp_get_caps_rsp *)evt.data;
	zassert_equal(rsp->cap_id, BT_AVRCP_CAP_COMPANY_ID, "Capability 0x%02x", rsp->cap_id);
	zassert_equal(rsp->cap_cnt, 1U, "%u company IDs", rsp->cap_cnt);
	zassert_equal(sys_get_be24(rsp->cap), BT_AVRCP_COMPANY_ID_BLUETOOTH_SIG,
		      "Company ID 0x%06x", sys_get_be24(rsp->cap));

	tid = test_next_tid();
	TEST_OK(bt_avrcp_ct_get_caps(test_ct, tid, BT_AVRCP_CAP_EVENTS_SUPPORTED));
	test_ct_expect(EVT_CT_GET_CAPS, tid, &evt);
	zassert_equal(evt.status, BT_AVRCP_STATUS_SUCCESS, "Status 0x%02x", evt.status);
	zassert_equal(evt.len, sizeof(*rsp) + sizeof(events), "Length %u", evt.len);
	rsp = (const struct bt_avrcp_get_caps_rsp *)evt.data;
	zassert_equal(rsp->cap_id, BT_AVRCP_CAP_EVENTS_SUPPORTED, "Capability 0x%02x", rsp->cap_id);
	zassert_equal(rsp->cap_cnt, sizeof(events), "%u events", rsp->cap_cnt);
	zassert_mem_equal(rsp->cap, events, sizeof(events), "Unexpected events");

	/* Rejected by the TG application. */
	tid = test_next_tid();
	TEST_OK(bt_avrcp_ct_get_caps(test_ct, tid, BT_AVRCP_CAP_EVENTS_SUPPORTED));
	test_ct_expect_status(EVT_CT_GET_CAPS, tid, BT_AVRCP_STATUS_INVALID_PARAMETER);

	/* Extra: an unknown capability is rejected by the stack of the TG. */
	tid = test_next_tid();
	TEST_OK(bt_avrcp_ct_get_caps(test_ct, tid, TEST_CAP_ID_INVALID));
	test_ct_expect_status(EVT_CT_GET_CAPS, tid, BT_AVRCP_STATUS_INVALID_PARAMETER);

	/* The TG checks that its application did not see the last command. */
	test_br_sync();
}

/* b2b: avrcp_ct get_caps, avrcp_tg get_caps_rsp */
ZTEST(avrcp_ct_control, test_004_get_caps)
{
	test_ct_run_twice(get_caps, false);
}

static uint8_t register_event(uint8_t event_id, uint32_t interval)
{
	uint8_t tid = test_next_tid();

	TEST_OK(bt_avrcp_ct_register_notification(test_ct, tid, event_id, interval,
						  test_ct_notify_changed));

	return tid;
}

static void expect_interim(uint8_t tid, uint8_t event_id)
{
	struct test_event evt;

	test_ct_expect(EVT_CT_NOTIFICATION, tid, &evt);
	zassert_equal(evt.status, BT_AVRCP_STATUS_SUCCESS, "Interim status 0x%02x", evt.status);
	zassert_equal(evt.event_id, event_id, "Interim event 0x%02x", evt.event_id);
	test_check_event_data(&evt, false);
}

static void expect_changed(uint8_t event_id)
{
	struct test_event evt;

	TEST_EXPECT(EVT_CT_NOTIFY_CHANGED, &evt);
	zassert_equal(evt.event_id, event_id, "Changed event 0x%02x", evt.event_id);
	test_check_event_data(&evt, true);
}

static void expect_notification_status(uint8_t tid, uint8_t event_id, uint8_t status)
{
	struct test_event evt;

	test_ct_expect(EVT_CT_NOTIFICATION, tid, &evt);
	zassert_equal(evt.status, status, "Status 0x%02x", evt.status);
	zassert_equal(evt.event_id, event_id, "Event 0x%02x", evt.event_id);
	zassert_false(evt.has_data, "Event data with status 0x%02x", status);
}

static void interim_and_changed(uint8_t event_id, uint32_t interval)
{
	uint8_t tid = register_event(event_id, interval);

	expect_interim(tid, event_id);
	expect_changed(event_id);
}

static void register_notification(void)
{
	uint8_t tid;

	interim_and_changed(BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED, 0U);

	/* The TG rejects the registration after the interim response. */
	tid = register_event(BT_AVRCP_EVT_VOLUME_CHANGED, 0U);
	expect_interim(tid, BT_AVRCP_EVT_VOLUME_CHANGED);
	expect_notification_status(tid, BT_AVRCP_EVT_VOLUME_CHANGED,
				   BT_AVRCP_STATUS_INVALID_PARAMETER);

	/* The rejected event can be registered again. */
	interim_and_changed(BT_AVRCP_EVT_VOLUME_CHANGED, 0U);

	/* The TG first tries a notification with an invalid event ID. */
	interim_and_changed(BT_AVRCP_EVT_TRACK_CHANGED, 0U);

	/* Extra: a second registration of a pending event is refused by the CT. */
	tid = register_event(BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED, 0U);
	TEST_ERR(bt_avrcp_ct_register_notification(test_ct, test_next_tid(),
						   BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED, 0U,
						   test_ct_notify_changed),
		 -EBUSY);
	test_br_sync();
	expect_interim(tid, BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED);
	expect_changed(BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED);

	TEST_ERR(bt_avrcp_ct_register_notification(test_ct, test_next_tid(),
						   TEST_EVENT_ID_INVALID_1, 0U,
						   test_ct_notify_changed),
		 -EINVAL);
	TEST_ERR(bt_avrcp_ct_register_notification(test_ct, test_next_tid(),
						   TEST_EVENT_ID_INVALID_2, 0U,
						   test_ct_notify_changed),
		 -EINVAL);
}

/* b2b: avrcp_ct register_notification, avrcp_tg register_notification_rsp */
ZTEST(avrcp_ct_control, test_005_register_notification)
{
	test_ct_run_twice(register_notification, false);
}

static void notification_events(void)
{
	static const uint8_t events[] = {TEST_NOTIFICATION_EVENTS};
	uint8_t tid_a;
	uint8_t tid_b;

	ARRAY_FOR_EACH(events, i) {
		interim_and_changed(events[i], test_event_interval(events[i]));
	}

	/* The TG does not implement the event; the event can be registered again. */
	tid_a = register_event(BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED, 0U);
	expect_notification_status(tid_a, BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED,
				   BT_AVRCP_STATUS_NOT_IMPLEMENTED);
	interim_and_changed(BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED, 0U);

	/* Two events registered at once; the TG answers the second one first. */
	tid_a = register_event(BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED, 0U);
	tid_b = register_event(BT_AVRCP_EVT_VOLUME_CHANGED, 0U);
	expect_interim(tid_b, BT_AVRCP_EVT_VOLUME_CHANGED);
	expect_interim(tid_a, BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED);
	expect_changed(BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED);
	expect_changed(BT_AVRCP_EVT_VOLUME_CHANGED);
}

/* Extra: the other events, a not implemented event and two pending events. */
ZTEST(avrcp_ct_control, test_006_notification_events)
{
	test_ct_run_twice(notification_events, false);
}

static void set_volume(uint8_t volume, uint8_t status, uint8_t rsp_volume)
{
	struct test_event evt;
	uint8_t tid = test_next_tid();

	TEST_OK(bt_avrcp_ct_set_absolute_volume(test_ct, tid, volume));
	test_ct_expect(EVT_CT_ABSOLUTE_VOLUME, tid, &evt);
	zassert_equal(evt.status, status, "Status 0x%02x", evt.status);
	zassert_equal(evt.value, rsp_volume, "Volume 0x%02x", evt.value);
}

static void set_absolute_volume(void)
{
	set_volume(TEST_VOLUME_HALF, BT_AVRCP_STATUS_SUCCESS, TEST_VOLUME_HALF);
	/* The TG first tries a response with a volume out of range. */
	set_volume(TEST_VOLUME_MAX, BT_AVRCP_STATUS_SUCCESS, TEST_VOLUME_MAX);
	/* Extra: rejected by the TG. */
	set_volume(0U, BT_AVRCP_STATUS_INTERNAL_ERROR, 0U);

	TEST_ERR(bt_avrcp_ct_set_absolute_volume(test_ct, test_next_tid(), TEST_VOLUME_MAX + 1U),
		 -EINVAL);
}

/* b2b: avrcp_ct set_absolute_volume, avrcp_tg set_absolute_volume_rsp */
ZTEST(avrcp_ct_control, test_007_set_absolute_volume)
{
	test_ct_run_twice(set_absolute_volume, false);
}

static void element_attrs_cmd(uint64_t identifier, const uint32_t *attr_ids, uint8_t num,
			      const struct test_media_attr *attrs, size_t count)
{
	struct bt_avrcp_get_element_attrs_cmd *cmd;
	struct test_event evt;
	struct net_buf *buf = test_vendor_buf();
	uint8_t tid = test_next_tid();

	cmd = net_buf_add(buf, sizeof(*cmd));
	sys_put_be64(identifier, cmd->identifier);
	cmd->num_attrs = num;
	for (uint8_t i = 0U; i < num; i++) {
		net_buf_add_be32(buf, attr_ids[i]);
	}

	TEST_SEND(bt_avrcp_ct_get_element_attrs(test_ct, tid, buf), buf);
	test_ct_expect(EVT_CT_ELEMENT_ATTRS, tid, &evt);
	zassert_equal(evt.status, BT_AVRCP_STATUS_SUCCESS, "Status 0x%02x", evt.status);
	zassert_true(evt.len > 0U, "Empty response");
	zassert_equal(evt.data[0], count, "%u attributes", evt.data[0]);
	test_check_media_attrs(&evt.data[1], evt.len - 1U, attrs, count);
}

static void get_element_attrs(void)
{
	static const uint32_t ids[] = {BT_AVRCP_MEDIA_ATTR_ID_TITLE, BT_AVRCP_MEDIA_ATTR_ID_ARTIST};

	/* All attributes, in a response of 575 bytes sent in two fragments. */
	element_attrs_cmd(TEST_ELEMENT_ID_LARGE, NULL, 0U, test_attrs_large,
			  ARRAY_SIZE(test_attrs_large));
	element_attrs_cmd(TEST_ELEMENT_ID_STD, NULL, 0U, test_attrs_std,
			  ARRAY_SIZE(test_attrs_std));
	/* Extra: a list of attributes. */
	element_attrs_cmd(TEST_ELEMENT_ID_LIST, ids, ARRAY_SIZE(ids), test_attrs_std,
			  ARRAY_SIZE(ids));
}

/* b2b: avrcp_ct get_element_attrs, avrcp_tg get_element_attrs_rsp */
ZTEST(avrcp_ct_control, test_008_get_element_attrs)
{
	test_ct_run_twice(get_element_attrs, false);
}

static void get_play_status(void)
{
	const struct bt_avrcp_get_play_status_rsp *rsp;
	struct test_event evt;
	uint8_t tid;

	tid = test_next_tid();
	TEST_OK(bt_avrcp_ct_get_play_status(test_ct, tid));
	test_ct_expect(EVT_CT_PLAY_STATUS, tid, &evt);
	zassert_equal(evt.status, BT_AVRCP_STATUS_SUCCESS, "Status 0x%02x", evt.status);
	zassert_equal(evt.len, sizeof(*rsp), "Length %u", evt.len);
	rsp = (const struct bt_avrcp_get_play_status_rsp *)evt.data;
	zassert_equal(sys_be32_to_cpu(rsp->song_length), TEST_SONG_LENGTH, "Length %u",
		      sys_be32_to_cpu(rsp->song_length));
	zassert_equal(sys_be32_to_cpu(rsp->song_position), TEST_SONG_POSITION, "Position %u",
		      sys_be32_to_cpu(rsp->song_position));
	zassert_equal(rsp->play_status, BT_AVRCP_PLAYBACK_STATUS_PLAYING, "Play status %u",
		      rsp->play_status);

	tid = test_next_tid();
	TEST_OK(bt_avrcp_ct_get_play_status(test_ct, tid));
	test_ct_expect_status(EVT_CT_PLAY_STATUS, tid, BT_AVRCP_STATUS_INVALID_PARAMETER);
}

/* b2b: avrcp_ct get_play_status, avrcp_tg get_play_status_rsp */
ZTEST(avrcp_ct_control, test_009_get_play_status)
{
	test_ct_run_twice(get_play_status, false);
}

static void list_app_attrs(void)
{
	static const struct test_list_case cases[] = {TEST_LIST_ATTRS_CASES};
	struct test_event evt;

	ARRAY_FOR_EACH(cases, i) {
		uint8_t tid = test_next_tid();

		TEST_OK(bt_avrcp_ct_list_player_app_setting_attrs(test_ct, tid));
		test_ct_expect(EVT_CT_LIST_ATTRS, tid, &evt);
		zassert_equal(evt.status, BT_AVRCP_STATUS_SUCCESS, "Status 0x%02x", evt.status);
		zassert_equal(evt.len, 1U + cases[i].num, "Length %u", evt.len);
		zassert_equal(evt.data[0], cases[i].num, "%u attributes", evt.data[0]);
		zassert_mem_equal(&evt.data[1], cases[i].ids, cases[i].num, "Unexpected IDs");
	}
}

/* b2b: avrcp_ct list_app_attrs, avrcp_tg list_player_app_setting_attrs_rsp */
ZTEST(avrcp_ct_control, test_010_list_player_app_setting_attrs)
{
	test_ct_run_twice(list_app_attrs, false);
}

static void list_app_vals(void)
{
	static const struct test_list_vals_case cases[] = {TEST_LIST_VALS_CASES};
	struct test_event evt;

	ARRAY_FOR_EACH(cases, i) {
		uint8_t tid = test_next_tid();

		TEST_OK(bt_avrcp_ct_list_player_app_setting_vals(test_ct, tid, cases[i].attr_id));
		test_ct_expect(EVT_CT_LIST_VALS, tid, &evt);
		zassert_equal(evt.status, BT_AVRCP_STATUS_SUCCESS, "Status 0x%02x", evt.status);
		zassert_equal(evt.len, 1U + cases[i].num, "Length %u", evt.len);
		zassert_equal(evt.data[0], cases[i].num, "%u values", evt.data[0]);
		zassert_mem_equal(&evt.data[1], cases[i].vals, cases[i].num, "Unexpected values");
	}
}

/* b2b: avrcp_ct list_app_vals, avrcp_tg list_player_app_setting_vals_rsp */
ZTEST(avrcp_ct_control, test_011_list_player_app_setting_vals)
{
	test_ct_run_twice(list_app_vals, false);
}

static void get_app_curr(void)
{
	static const struct test_get_curr_case cases[] = {TEST_GET_CURR_CASES};
	struct test_event evt;

	ARRAY_FOR_EACH(cases, i) {
		struct net_buf *buf = test_vendor_buf();
		uint8_t tid = test_next_tid();

		net_buf_add_u8(buf, cases[i].num_attrs);
		net_buf_add_mem(buf, cases[i].attr_ids, cases[i].num_attrs);
		TEST_SEND(bt_avrcp_ct_get_curr_player_app_setting_val(test_ct, tid, buf), buf);

		test_ct_expect(EVT_CT_GET_CURR_VAL, tid, &evt);
		zassert_equal(evt.status, BT_AVRCP_STATUS_SUCCESS, "Status 0x%02x", evt.status);
		zassert_equal(evt.len, 1U + (cases[i].num_pairs * 2U), "Length %u", evt.len);
		zassert_equal(evt.data[0], cases[i].num_pairs, "%u pairs", evt.data[0]);
		zassert_mem_equal(&evt.data[1], cases[i].pairs, cases[i].num_pairs * 2U,
				  "Unexpected pairs");
	}
}

/* b2b: avrcp_ct get_app_curr, avrcp_tg get_curr_player_app_setting_val_rsp */
ZTEST(avrcp_ct_control, test_012_get_curr_player_app_setting_val)
{
	test_ct_run_twice(get_app_curr, false);
}

static void set_app_val(void)
{
	static const struct test_set_val_case cases[] = {TEST_SET_VAL_CASES};

	ARRAY_FOR_EACH(cases, i) {
		struct net_buf *buf = test_vendor_buf();
		uint8_t tid = test_next_tid();

		net_buf_add_u8(buf, cases[i].num);
		net_buf_add_mem(buf, cases[i].pairs, cases[i].num * 2U);
		TEST_SEND(bt_avrcp_ct_set_player_app_setting_val(test_ct, tid, buf), buf);

		test_ct_expect_status(EVT_CT_SET_VAL, tid, cases[i].status);
	}
}

/* b2b: avrcp_ct set_app_val, avrcp_tg set_player_app_setting_val_rsp */
ZTEST(avrcp_ct_control, test_013_set_player_app_setting_val)
{
	test_ct_run_twice(set_app_val, false);
}

/* Check the text entries (ID, character set, length, text) of a text response. */
static void check_texts(const struct test_event *evt, const uint8_t *ids, uint8_t num,
			bool val_texts)
{
	size_t off = 1U;

	zassert_true(evt->len > 0U, "Empty response");
	zassert_equal(evt->data[0], num, "%u entries", evt->data[0]);

	for (uint8_t i = 0U; i < num; i++) {
		const char *text = val_texts ? test_val_text(ids[i]) : test_attr_text(ids[i]);
		size_t text_len = strlen(text);

		zassert_true((off + 4U + text_len) <= evt->len, "Entry %u truncated", i);
		zassert_equal(evt->data[off], ids[i], "Entry %u ID 0x%02x", i, evt->data[off]);
		zassert_equal(sys_get_be16(&evt->data[off + 1U]), BT_AVRCP_CHARSET_UTF8,
			      "Entry %u charset 0x%04x", i, sys_get_be16(&evt->data[off + 1U]));
		zassert_equal(evt->data[off + 3U], text_len, "Entry %u text length %u", i,
			      evt->data[off + 3U]);
		zassert_mem_equal(&evt->data[off + 4U], text, text_len, "Entry %u text", i);
		off += 4U + text_len;
	}

	zassert_equal(off, evt->len, "%zu bytes after the entries", evt->len - off);
}

static void get_app_attr_text(void)
{
	static const struct test_attr_text_case cases[] = {TEST_ATTR_TEXT_CASES};
	struct test_event evt;

	ARRAY_FOR_EACH(cases, i) {
		struct net_buf *buf = test_vendor_buf();
		uint8_t tid = test_next_tid();

		net_buf_add_u8(buf, cases[i].num);
		net_buf_add_mem(buf, cases[i].attr_ids, cases[i].num);
		TEST_SEND(bt_avrcp_ct_get_player_app_setting_attr_text(test_ct, tid, buf), buf);

		if (cases[i].status != BT_AVRCP_STATUS_SUCCESS) {
			test_ct_expect_status(EVT_CT_ATTR_TEXT, tid, cases[i].status);
			continue;
		}

		test_ct_expect(EVT_CT_ATTR_TEXT, tid, &evt);
		zassert_equal(evt.status, BT_AVRCP_STATUS_SUCCESS, "Status 0x%02x", evt.status);
		check_texts(&evt, cases[i].attr_ids, cases[i].num, false);
	}
}

/* b2b: avrcp_ct get_app_attr_text, avrcp_tg get_player_app_setting_attr_text_rsp */
ZTEST(avrcp_ct_control, test_014_get_player_app_setting_attr_text)
{
	test_ct_run_twice(get_app_attr_text, false);
}

static void get_app_val_text(void)
{
	static const struct test_val_text_case cases[] = {TEST_VAL_TEXT_CASES};
	struct test_event evt;

	ARRAY_FOR_EACH(cases, i) {
		struct net_buf *buf = test_vendor_buf();
		uint8_t tid = test_next_tid();

		net_buf_add_u8(buf, cases[i].attr_id);
		net_buf_add_u8(buf, cases[i].num);
		net_buf_add_mem(buf, cases[i].val_ids, cases[i].num);
		TEST_SEND(bt_avrcp_ct_get_player_app_setting_val_text(test_ct, tid, buf), buf);

		if (cases[i].status != BT_AVRCP_STATUS_SUCCESS) {
			test_ct_expect_status(EVT_CT_VAL_TEXT, tid, cases[i].status);
			continue;
		}

		test_ct_expect(EVT_CT_VAL_TEXT, tid, &evt);
		zassert_equal(evt.status, BT_AVRCP_STATUS_SUCCESS, "Status 0x%02x", evt.status);
		check_texts(&evt, cases[i].val_ids, cases[i].num, true);
	}
}

/* b2b: avrcp_ct get_app_val_text, avrcp_tg get_player_app_setting_val_text_rsp */
ZTEST(avrcp_ct_control, test_015_get_player_app_setting_val_text)
{
	test_ct_run_twice(get_app_val_text, false);
}

static void inform_displayable_char_set(void)
{
	static const struct test_charset_case cases[] = {TEST_CHARSET_CASES};

	ARRAY_FOR_EACH(cases, i) {
		struct net_buf *buf = test_vendor_buf();
		uint8_t tid = test_next_tid();

		net_buf_add_u8(buf, cases[i].num);
		for (uint8_t j = 0U; j < cases[i].num; j++) {
			net_buf_add_be16(buf, cases[i].charsets[j]);
		}
		TEST_SEND(bt_avrcp_ct_inform_displayable_char_set(test_ct, tid, buf), buf);

		test_ct_expect_status(EVT_CT_DISPLAYABLE_CHAR_SET, tid, cases[i].status);
	}
}

/* b2b: avrcp_ct inform_displayable_char, avrcp_tg inform_displayable_char_rsp */
ZTEST(avrcp_ct_control, test_016_inform_displayable_char_set)
{
	test_ct_run_twice(inform_displayable_char_set, false);
}

static void inform_batt_status(void)
{
	static const struct test_status_case cases[] = {TEST_BATT_CASES};

	ARRAY_FOR_EACH(cases, i) {
		uint8_t tid = test_next_tid();

		TEST_OK(bt_avrcp_ct_inform_batt_status_of_ct(test_ct, tid, cases[i].param));
		test_ct_expect_status(EVT_CT_BATT_STATUS, tid, cases[i].status);
	}

	TEST_ERR(bt_avrcp_ct_inform_batt_status_of_ct(test_ct, test_next_tid(), TEST_BATT_INVALID),
		 -EINVAL);
}

/* b2b: avrcp_ct inform_batt, avrcp_tg inform_batt_rsp */
ZTEST(avrcp_ct_control, test_017_inform_batt_status_of_ct)
{
	test_ct_run_twice(inform_batt_status, false);
}

static void set_addressed_player(void)
{
	static const struct test_status_case cases[] = {TEST_ADDRESSED_CASES};

	ARRAY_FOR_EACH(cases, i) {
		uint8_t tid = test_next_tid();

		TEST_OK(bt_avrcp_ct_set_addressed_player(test_ct, tid, cases[i].param));
		test_ct_expect_status(EVT_CT_ADDRESSED_PLAYER, tid, cases[i].status);
	}
}

/* b2b: avrcp_ct set_addressed_player, avrcp_tg set_addressed_player_rsp */
ZTEST(avrcp_ct_control, test_018_set_addressed_player)
{
	test_ct_run_twice(set_addressed_player, false);
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

ZTEST_SUITE(avrcp_ct_control, NULL, suite_setup, suite_before, NULL, suite_teardown);
