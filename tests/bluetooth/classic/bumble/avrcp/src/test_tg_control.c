/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Control channel tests of the TG (device 1), counterpart of test_ct_control.c: the TG checks the
 * commands as the board-to-board avrcp_ct tests do and sends the responses of the avrcp_tg tests.
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
	struct bt_avrcp_unit_info_rsp rsp = {
		.unit_type = BT_AVRCP_SUBUNIT_TYPE_PANEL,
		.company_id = BT_AVRCP_COMPANY_ID_BLUETOOTH_SIG,
	};
	struct test_event evt;

	TEST_EXPECT(EVT_TG_UNIT_INFO, &evt);
	TEST_OK(bt_avrcp_tg_send_unit_info_rsp(test_tg, evt.tid, &rsp));
}

ZTEST(avrcp_tg_control, test_001_unit_info)
{
	test_tg_run_twice(unit_info, false);
}

static void subunit_info(void)
{
	struct test_event evt;

	TEST_EXPECT(EVT_TG_SUBUNIT_INFO, &evt);
	TEST_OK(bt_avrcp_tg_send_subunit_info_rsp(test_tg, evt.tid));
}

ZTEST(avrcp_tg_control, test_002_subunit_info)
{
	test_tg_run_twice(subunit_info, false);
}

/* Wait for a passthrough command and answer it with @p result, @p opid and @p state. */
static void passthrough_rsp(uint8_t opid, uint8_t state, const uint8_t *payload, uint8_t len,
			    bt_avrcp_rsp_t result, uint8_t rsp_opid, uint8_t rsp_state)
{
	const struct bt_avrcp_passthrough_cmd *cmd;
	struct bt_avrcp_passthrough_rsp *rsp;
	struct test_event evt;
	struct net_buf *buf;

	TEST_EXPECT(EVT_TG_PASSTHROUGH, &evt);
	zassert_equal(evt.len, sizeof(*cmd) + len, "Command length %u", evt.len);
	cmd = (const struct bt_avrcp_passthrough_cmd *)evt.data;
	zassert_equal(BT_AVRCP_PASSTHROUGH_GET_OPID(cmd), opid, "Operation 0x%02x",
		      BT_AVRCP_PASSTHROUGH_GET_OPID(cmd));
	zassert_equal(BT_AVRCP_PASSTHROUGH_GET_STATE(cmd), state, "State %u",
		      BT_AVRCP_PASSTHROUGH_GET_STATE(cmd));
	zassert_equal(cmd->data_len, len, "Data length %u", cmd->data_len);
	if (len > 0U) {
		zassert_mem_equal(cmd->data, payload, len, "Unexpected operation data");
	}

	buf = test_pdu_buf();
	rsp = net_buf_add(buf, sizeof(*rsp));
	BT_AVRCP_PASSTHROUGH_SET_STATE_OPID(rsp, rsp_state, rsp_opid);
	rsp->data_len = len;
	if (len > 0U) {
		net_buf_add_mem(buf, payload, len);
	}

	TEST_SEND(bt_avrcp_tg_send_passthrough_rsp(test_tg, evt.tid, result, buf), buf);
}

static void passthrough(void)
{
	struct bt_avrcp_passthrough_opvu_data opvu;

	passthrough_rsp(BT_AVRCP_OPID_PLAY, BT_AVRCP_BUTTON_PRESSED, NULL, 0U,
			BT_AVRCP_RSP_ACCEPTED, BT_AVRCP_OPID_PLAY, BT_AVRCP_BUTTON_PRESSED);
	passthrough_rsp(BT_AVRCP_OPID_PAUSE, BT_AVRCP_BUTTON_PRESSED, NULL, 0U,
			BT_AVRCP_RSP_ACCEPTED, BT_AVRCP_OPID_PAUSE, BT_AVRCP_BUTTON_RELEASED);

	sys_put_be24(BT_AVRCP_COMPANY_ID_BLUETOOTH_SIG, opvu.company_id);
	opvu.opid_vu = sys_cpu_to_be16(TEST_OPID_VU);
	passthrough_rsp(BT_AVRCP_OPID_VENDOR_UNIQUE, BT_AVRCP_BUTTON_RELEASED,
			(const uint8_t *)&opvu, sizeof(opvu), BT_AVRCP_RSP_NOT_IMPLEMENTED,
			BT_AVRCP_OPID_VENDOR_UNIQUE, BT_AVRCP_BUTTON_RELEASED);

	passthrough_rsp(BT_AVRCP_OPID_STOP, BT_AVRCP_BUTTON_PRESSED, NULL, 0U,
			BT_AVRCP_RSP_REJECTED, BT_AVRCP_OPID_STOP, BT_AVRCP_BUTTON_PRESSED);
}

ZTEST(avrcp_tg_control, test_003_passthrough)
{
	test_tg_run_twice(passthrough, false);
}

static void caps_rsp(uint8_t cap_id, uint8_t status)
{
	static const uint8_t events[] = {TEST_EVENTS_SUPPORTED};
	struct bt_avrcp_get_caps_rsp *rsp;
	struct test_event evt;
	struct net_buf *buf;

	TEST_EXPECT(EVT_TG_GET_CAPS, &evt);
	zassert_equal(evt.value, cap_id, "Capability 0x%02x", evt.value);

	buf = test_vendor_buf();
	rsp = net_buf_add(buf, sizeof(*rsp));
	rsp->cap_id = cap_id;
	if (cap_id == BT_AVRCP_CAP_COMPANY_ID) {
		rsp->cap_cnt = 1U;
		net_buf_add_be24(buf, BT_AVRCP_COMPANY_ID_BLUETOOTH_SIG);
	} else {
		rsp->cap_cnt = sizeof(events);
		net_buf_add_mem(buf, events, sizeof(events));
	}

	TEST_SEND(bt_avrcp_tg_get_caps(test_tg, evt.tid, status, buf), buf);
}

static void get_caps(void)
{
	caps_rsp(BT_AVRCP_CAP_COMPANY_ID, BT_AVRCP_STATUS_SUCCESS);
	caps_rsp(BT_AVRCP_CAP_EVENTS_SUPPORTED, BT_AVRCP_STATUS_SUCCESS);
	caps_rsp(BT_AVRCP_CAP_EVENTS_SUPPORTED, BT_AVRCP_STATUS_INVALID_PARAMETER);

	/* The unknown capability is rejected by the stack, the application is not called. */
	test_br_sync();
	test_evt_none(EVT_TG_GET_CAPS, K_NO_WAIT);
}

ZTEST(avrcp_tg_control, test_004_get_caps)
{
	test_tg_run_twice(get_caps, false);
}

static void expect_registration(uint8_t event_id, uint32_t interval, struct test_event *evt)
{
	TEST_EXPECT(EVT_TG_REGISTER_NOTIFICATION, evt);
	zassert_equal(evt->event_id, event_id, "Event 0x%02x", evt->event_id);
	zassert_equal(evt->value, interval, "Interval %u", evt->value);
}

static void notify(uint8_t tid, uint8_t status, uint8_t event_id, bool changed)
{
	struct bt_avrcp_event_data data;

	test_event_data(event_id, changed, &data);
	TEST_OK(bt_avrcp_tg_notification(test_tg, tid, status, event_id, &data));
}

static void interim_and_changed(uint8_t event_id, uint32_t interval)
{
	struct test_event evt;

	expect_registration(event_id, interval, &evt);
	notify(evt.tid, BT_AVRCP_STATUS_SUCCESS, event_id, false);
	notify(evt.tid, BT_AVRCP_STATUS_SUCCESS, event_id, true);
}

static void register_notification(void)
{
	struct bt_avrcp_event_data data;
	struct test_event evt;

	interim_and_changed(BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED, 0U);

	/* After the reject, the event is no longer registered. */
	expect_registration(BT_AVRCP_EVT_VOLUME_CHANGED, 0U, &evt);
	notify(evt.tid, BT_AVRCP_STATUS_SUCCESS, BT_AVRCP_EVT_VOLUME_CHANGED, false);
	notify(evt.tid, BT_AVRCP_STATUS_INVALID_PARAMETER, BT_AVRCP_EVT_VOLUME_CHANGED, false);
	test_event_data(BT_AVRCP_EVT_VOLUME_CHANGED, true, &data);
	TEST_ERR(bt_avrcp_tg_notification(test_tg, evt.tid, BT_AVRCP_STATUS_SUCCESS,
					  BT_AVRCP_EVT_VOLUME_CHANGED, &data),
		 -ENOENT);

	interim_and_changed(BT_AVRCP_EVT_VOLUME_CHANGED, 0U);

	/* The corrupted notification of the board-to-board TG. */
	expect_registration(BT_AVRCP_EVT_TRACK_CHANGED, 0U, &evt);
	memset(&data, 0xff, sizeof(data));
	TEST_ERR(bt_avrcp_tg_notification(test_tg, evt.tid, BT_AVRCP_STATUS_SUCCESS,
					  TEST_EVENT_ID_CORRUPTED, &data),
		 -EINVAL);
	notify(evt.tid, BT_AVRCP_STATUS_SUCCESS, BT_AVRCP_EVT_TRACK_CHANGED, false);
	notify(evt.tid, BT_AVRCP_STATUS_SUCCESS, BT_AVRCP_EVT_TRACK_CHANGED, true);

	/* The CT refuses its second registration, only one is received. */
	expect_registration(BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED, 0U, &evt);
	test_br_sync();
	notify(evt.tid, BT_AVRCP_STATUS_SUCCESS, BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED, false);
	notify(evt.tid, BT_AVRCP_STATUS_SUCCESS, BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED, true);
}

ZTEST(avrcp_tg_control, test_005_register_notification)
{
	test_tg_run_twice(register_notification, false);
}

static void notification_events(void)
{
	static const uint8_t events[] = {TEST_NOTIFICATION_EVENTS};
	struct test_event evt_a;
	struct test_event evt_b;

	ARRAY_FOR_EACH(events, i) {
		interim_and_changed(events[i], test_event_interval(events[i]));
	}

	expect_registration(BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED, 0U, &evt_a);
	notify(evt_a.tid, BT_AVRCP_STATUS_NOT_IMPLEMENTED, BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED,
	       false);
	interim_and_changed(BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED, 0U);

	expect_registration(BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED, 0U, &evt_a);
	expect_registration(BT_AVRCP_EVT_VOLUME_CHANGED, 0U, &evt_b);
	notify(evt_b.tid, BT_AVRCP_STATUS_SUCCESS, BT_AVRCP_EVT_VOLUME_CHANGED, false);
	notify(evt_a.tid, BT_AVRCP_STATUS_SUCCESS, BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED, false);
	notify(evt_a.tid, BT_AVRCP_STATUS_SUCCESS, BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED, true);
	notify(evt_b.tid, BT_AVRCP_STATUS_SUCCESS, BT_AVRCP_EVT_VOLUME_CHANGED, true);
}

ZTEST(avrcp_tg_control, test_006_notification_events)
{
	test_tg_run_twice(notification_events, false);
}

static void expect_volume(uint8_t volume, struct test_event *evt)
{
	TEST_EXPECT(EVT_TG_ABSOLUTE_VOLUME, evt);
	zassert_equal(evt->value, volume, "Volume 0x%02x", evt->value);
}

static void set_absolute_volume(void)
{
	struct test_event evt;

	expect_volume(TEST_VOLUME_HALF, &evt);
	TEST_OK(bt_avrcp_tg_absolute_volume(test_tg, evt.tid, BT_AVRCP_STATUS_SUCCESS,
					    TEST_VOLUME_HALF));

	expect_volume(TEST_VOLUME_MAX, &evt);
	TEST_ERR(bt_avrcp_tg_absolute_volume(test_tg, evt.tid, BT_AVRCP_STATUS_SUCCESS,
					     TEST_VOLUME_MAX + 1U),
		 -EINVAL);
	TEST_OK(bt_avrcp_tg_absolute_volume(test_tg, evt.tid, BT_AVRCP_STATUS_SUCCESS,
					    TEST_VOLUME_MAX));

	expect_volume(0U, &evt);
	TEST_OK(bt_avrcp_tg_absolute_volume(test_tg, evt.tid, BT_AVRCP_STATUS_INTERNAL_ERROR, 0U));
}

ZTEST(avrcp_tg_control, test_007_set_absolute_volume)
{
	test_tg_run_twice(set_absolute_volume, false);
}

static void element_attrs_rsp(uint64_t identifier, const uint32_t *attr_ids, uint8_t num,
			      const struct test_media_attr *attrs, size_t count)
{
	const struct bt_avrcp_get_element_attrs_cmd *cmd;
	struct test_event evt;
	struct net_buf *buf;

	TEST_EXPECT(EVT_TG_ELEMENT_ATTRS, &evt);
	zassert_equal(evt.len, sizeof(*cmd) + (num * sizeof(uint32_t)), "Command length %u",
		      evt.len);
	cmd = (const struct bt_avrcp_get_element_attrs_cmd *)evt.data;
	zassert_equal(sys_get_be64(cmd->identifier), identifier, "Identifier 0x%llx",
		      (unsigned long long)sys_get_be64(cmd->identifier));
	zassert_equal(cmd->num_attrs, num, "%u attributes requested", cmd->num_attrs);
	for (uint8_t i = 0U; i < num; i++) {
		zassert_equal(sys_get_be32(&evt.data[sizeof(*cmd) + (i * sizeof(uint32_t))]),
			      attr_ids[i], "Attribute %u", i);
	}

	buf = test_vendor_buf();
	net_buf_add_u8(buf, count);
	test_add_media_attrs(buf, attrs, count);
	TEST_SEND(bt_avrcp_tg_get_element_attrs(test_tg, evt.tid, BT_AVRCP_STATUS_SUCCESS, buf),
		  buf);
}

static void get_element_attrs(void)
{
	static const uint32_t ids[] = {BT_AVRCP_MEDIA_ATTR_ID_TITLE, BT_AVRCP_MEDIA_ATTR_ID_ARTIST};

	element_attrs_rsp(TEST_ELEMENT_ID_LARGE, NULL, 0U, test_attrs_large,
			  ARRAY_SIZE(test_attrs_large));
	element_attrs_rsp(TEST_ELEMENT_ID_STD, NULL, 0U, test_attrs_std,
			  ARRAY_SIZE(test_attrs_std));
	element_attrs_rsp(TEST_ELEMENT_ID_LIST, ids, ARRAY_SIZE(ids), test_attrs_std,
			  ARRAY_SIZE(ids));
}

ZTEST(avrcp_tg_control, test_008_get_element_attrs)
{
	test_tg_run_twice(get_element_attrs, false);
}

static void play_status_rsp(uint8_t status)
{
	struct test_event evt;
	struct net_buf *buf;

	TEST_EXPECT(EVT_TG_PLAY_STATUS, &evt);

	/* As the board-to-board TG, the values are also given with an error status. */
	buf = test_vendor_buf();
	net_buf_add_be32(buf, TEST_SONG_LENGTH);
	net_buf_add_be32(buf, TEST_SONG_POSITION);
	net_buf_add_u8(buf, BT_AVRCP_PLAYBACK_STATUS_PLAYING);
	TEST_SEND(bt_avrcp_tg_get_play_status(test_tg, evt.tid, status, buf), buf);
}

static void get_play_status(void)
{
	play_status_rsp(BT_AVRCP_STATUS_SUCCESS);
	play_status_rsp(BT_AVRCP_STATUS_INVALID_PARAMETER);
}

ZTEST(avrcp_tg_control, test_009_get_play_status)
{
	test_tg_run_twice(get_play_status, false);
}

static void list_app_attrs(void)
{
	static const struct test_list_case cases[] = {TEST_LIST_ATTRS_CASES};
	struct test_event evt;

	ARRAY_FOR_EACH(cases, i) {
		struct net_buf *buf;

		TEST_EXPECT(EVT_TG_LIST_ATTRS, &evt);

		buf = test_vendor_buf();
		net_buf_add_u8(buf, cases[i].num);
		net_buf_add_mem(buf, cases[i].ids, cases[i].num);
		TEST_SEND(bt_avrcp_tg_list_player_app_setting_attrs(test_tg, evt.tid,
								    BT_AVRCP_STATUS_SUCCESS, buf),
			  buf);
	}
}

ZTEST(avrcp_tg_control, test_010_list_player_app_setting_attrs)
{
	test_tg_run_twice(list_app_attrs, false);
}

static void list_app_vals(void)
{
	static const struct test_list_vals_case cases[] = {TEST_LIST_VALS_CASES};
	struct test_event evt;

	ARRAY_FOR_EACH(cases, i) {
		struct net_buf *buf;

		TEST_EXPECT(EVT_TG_LIST_VALS, &evt);
		zassert_equal(evt.value, cases[i].attr_id, "Attribute 0x%02x", evt.value);

		buf = test_vendor_buf();
		net_buf_add_u8(buf, cases[i].num);
		net_buf_add_mem(buf, cases[i].vals, cases[i].num);
		TEST_SEND(bt_avrcp_tg_list_player_app_setting_vals(test_tg, evt.tid,
								   BT_AVRCP_STATUS_SUCCESS, buf),
			  buf);
	}
}

ZTEST(avrcp_tg_control, test_011_list_player_app_setting_vals)
{
	test_tg_run_twice(list_app_vals, false);
}

static void get_app_curr(void)
{
	static const struct test_get_curr_case cases[] = {TEST_GET_CURR_CASES};
	struct test_event evt;

	ARRAY_FOR_EACH(cases, i) {
		struct net_buf *buf;

		TEST_EXPECT(EVT_TG_GET_CURR_VAL, &evt);
		zassert_equal(evt.len, 1U + cases[i].num_attrs, "Command length %u", evt.len);
		zassert_equal(evt.data[0], cases[i].num_attrs, "%u attributes", evt.data[0]);
		zassert_mem_equal(&evt.data[1], cases[i].attr_ids, cases[i].num_attrs,
				  "Unexpected attributes");

		buf = test_vendor_buf();
		net_buf_add_u8(buf, cases[i].num_pairs);
		net_buf_add_mem(buf, cases[i].pairs, cases[i].num_pairs * 2U);
		TEST_SEND(bt_avrcp_tg_get_curr_player_app_setting_val(test_tg, evt.tid,
								      BT_AVRCP_STATUS_SUCCESS, buf),
			  buf);
	}
}

ZTEST(avrcp_tg_control, test_012_get_curr_player_app_setting_val)
{
	test_tg_run_twice(get_app_curr, false);
}

static void set_app_val(void)
{
	static const struct test_set_val_case cases[] = {TEST_SET_VAL_CASES};
	struct test_event evt;

	ARRAY_FOR_EACH(cases, i) {
		TEST_EXPECT(EVT_TG_SET_VAL, &evt);
		zassert_equal(evt.len, 1U + (cases[i].num * 2U), "Command length %u", evt.len);
		zassert_equal(evt.data[0], cases[i].num, "%u pairs", evt.data[0]);
		zassert_mem_equal(&evt.data[1], cases[i].pairs, cases[i].num * 2U,
				  "Unexpected pairs");

		TEST_OK(bt_avrcp_tg_set_player_app_setting_val(test_tg, evt.tid, cases[i].status));
	}
}

ZTEST(avrcp_tg_control, test_013_set_player_app_setting_val)
{
	test_tg_run_twice(set_app_val, false);
}

/* Text response: one entry (ID, character set, length, text) per ID. */
static struct net_buf *texts_buf(const uint8_t *ids, uint8_t num, bool val_texts)
{
	struct net_buf *buf = test_vendor_buf();

	net_buf_add_u8(buf, num);
	for (uint8_t i = 0U; i < num; i++) {
		const char *text = val_texts ? test_val_text(ids[i]) : test_attr_text(ids[i]);

		net_buf_add_u8(buf, ids[i]);
		net_buf_add_be16(buf, BT_AVRCP_CHARSET_UTF8);
		net_buf_add_u8(buf, strlen(text));
		net_buf_add_mem(buf, text, strlen(text));
	}

	return buf;
}

static void get_app_attr_text(void)
{
	static const struct test_attr_text_case cases[] = {TEST_ATTR_TEXT_CASES};
	struct test_event evt;

	ARRAY_FOR_EACH(cases, i) {
		struct net_buf *buf;

		TEST_EXPECT(EVT_TG_ATTR_TEXT, &evt);
		zassert_equal(evt.len, 1U + cases[i].num, "Command length %u", evt.len);
		zassert_equal(evt.data[0], cases[i].num, "%u attributes", evt.data[0]);
		zassert_mem_equal(&evt.data[1], cases[i].attr_ids, cases[i].num,
				  "Unexpected attributes");

		buf = texts_buf(cases[i].attr_ids, cases[i].num, false);
		TEST_SEND(bt_avrcp_tg_get_player_app_setting_attr_text(test_tg, evt.tid,
								       cases[i].status, buf),
			  buf);
	}
}

ZTEST(avrcp_tg_control, test_014_get_player_app_setting_attr_text)
{
	test_tg_run_twice(get_app_attr_text, false);
}

static void get_app_val_text(void)
{
	static const struct test_val_text_case cases[] = {TEST_VAL_TEXT_CASES};
	struct test_event evt;

	ARRAY_FOR_EACH(cases, i) {
		struct net_buf *buf;

		TEST_EXPECT(EVT_TG_VAL_TEXT, &evt);
		zassert_equal(evt.len, 2U + cases[i].num, "Command length %u", evt.len);
		zassert_equal(evt.data[0], cases[i].attr_id, "Attribute 0x%02x", evt.data[0]);
		zassert_equal(evt.data[1], cases[i].num, "%u values", evt.data[1]);
		zassert_mem_equal(&evt.data[2], cases[i].val_ids, cases[i].num,
				  "Unexpected values");

		buf = texts_buf(cases[i].val_ids, cases[i].num, true);
		TEST_SEND(bt_avrcp_tg_get_player_app_setting_val_text(test_tg, evt.tid,
								      cases[i].status, buf),
			  buf);
	}
}

ZTEST(avrcp_tg_control, test_015_get_player_app_setting_val_text)
{
	test_tg_run_twice(get_app_val_text, false);
}

static void inform_displayable_char_set(void)
{
	static const struct test_charset_case cases[] = {TEST_CHARSET_CASES};
	struct test_event evt;

	ARRAY_FOR_EACH(cases, i) {
		TEST_EXPECT(EVT_TG_DISPLAYABLE_CHAR_SET, &evt);
		zassert_equal(evt.len, 1U + (cases[i].num * 2U), "Command length %u", evt.len);
		zassert_equal(evt.data[0], cases[i].num, "%u character sets", evt.data[0]);
		for (uint8_t j = 0U; j < cases[i].num; j++) {
			zassert_equal(sys_get_be16(&evt.data[1U + (j * 2U)]), cases[i].charsets[j],
				      "Character set %u", j);
		}

		TEST_OK(bt_avrcp_tg_inform_displayable_char_set(test_tg, evt.tid, cases[i].status));
	}
}

ZTEST(avrcp_tg_control, test_016_inform_displayable_char_set)
{
	test_tg_run_twice(inform_displayable_char_set, false);
}

static void inform_batt_status(void)
{
	static const struct test_status_case cases[] = {TEST_BATT_CASES};
	struct test_event evt;

	ARRAY_FOR_EACH(cases, i) {
		TEST_EXPECT(EVT_TG_BATT_STATUS, &evt);
		zassert_equal(evt.value, cases[i].param, "Battery status %u", evt.value);

		TEST_OK(bt_avrcp_tg_inform_batt_status_of_ct(test_tg, evt.tid, cases[i].status));
	}
}

ZTEST(avrcp_tg_control, test_017_inform_batt_status_of_ct)
{
	test_tg_run_twice(inform_batt_status, false);
}

static void set_addressed_player(void)
{
	static const struct test_status_case cases[] = {TEST_ADDRESSED_CASES};
	struct test_event evt;

	ARRAY_FOR_EACH(cases, i) {
		TEST_EXPECT(EVT_TG_ADDRESSED_PLAYER, &evt);
		zassert_equal(evt.value, cases[i].param, "Player 0x%04x", evt.value);

		TEST_OK(bt_avrcp_tg_set_addressed_player(test_tg, evt.tid, cases[i].status));
	}
}

ZTEST(avrcp_tg_control, test_018_set_addressed_player)
{
	test_tg_run_twice(set_addressed_player, false);
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

ZTEST_SUITE(avrcp_tg_control, NULL, suite_setup, suite_before, NULL, suite_teardown);
