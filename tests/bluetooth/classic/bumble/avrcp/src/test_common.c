/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/classic/avrcp.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"
#include "test_values.h"

LOG_MODULE_REGISTER(test_avrcp_common, CONFIG_TEST_CLASSIC_AVRCP_SIM_LOG_LEVEL);

#define EVT_SLOTS 32

/* Buffers of the commands and responses built by the tests, large enough for 575 bytes. */
#define TEST_TX_BUF_COUNT 6
#define TEST_TX_BUF_SIZE  1024

NET_BUF_POOL_DEFINE(test_tx_pool, TEST_TX_BUF_COUNT, TEST_TX_BUF_SIZE,
		    CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

struct bt_avrcp_ct *test_ct;
struct bt_avrcp_tg *test_tg;

static struct test_event evt_slots[EVT_SLOTS];
static bool evt_used[EVT_SLOTS];
static uint32_t evt_seq[EVT_SLOTS];
static uint32_t evt_next_seq;
static int evt_error;

static K_MUTEX_DEFINE(evt_lock);
static K_CONDVAR_DEFINE(evt_cond);

static uint8_t ct_tid;

static const char *const evt_names[] = {
	[EVT_CT_CONNECTED] = "ct_connected",
	[EVT_CT_DISCONNECTED] = "ct_disconnected",
	[EVT_CT_BROWSING_CONNECTED] = "ct_browsing_connected",
	[EVT_CT_BROWSING_DISCONNECTED] = "ct_browsing_disconnected",
	[EVT_TG_CONNECTED] = "tg_connected",
	[EVT_TG_DISCONNECTED] = "tg_disconnected",
	[EVT_TG_BROWSING_CONNECTED] = "tg_browsing_connected",
	[EVT_TG_BROWSING_DISCONNECTED] = "tg_browsing_disconnected",
	[EVT_CT_UNIT_INFO] = "ct_unit_info",
	[EVT_CT_SUBUNIT_INFO] = "ct_subunit_info",
	[EVT_CT_PASSTHROUGH] = "ct_passthrough",
	[EVT_CT_GET_CAPS] = "ct_get_caps",
	[EVT_CT_NOTIFICATION] = "ct_notification",
	[EVT_CT_NOTIFY_CHANGED] = "ct_notify_changed",
	[EVT_CT_LIST_ATTRS] = "ct_list_attrs",
	[EVT_CT_LIST_VALS] = "ct_list_vals",
	[EVT_CT_GET_CURR_VAL] = "ct_get_curr_val",
	[EVT_CT_SET_VAL] = "ct_set_val",
	[EVT_CT_ATTR_TEXT] = "ct_attr_text",
	[EVT_CT_VAL_TEXT] = "ct_val_text",
	[EVT_CT_DISPLAYABLE_CHAR_SET] = "ct_displayable_char_set",
	[EVT_CT_BATT_STATUS] = "ct_batt_status",
	[EVT_CT_ABSOLUTE_VOLUME] = "ct_absolute_volume",
	[EVT_CT_ELEMENT_ATTRS] = "ct_element_attrs",
	[EVT_CT_PLAY_STATUS] = "ct_play_status",
	[EVT_CT_ADDRESSED_PLAYER] = "ct_addressed_player",
	[EVT_CT_PLAY_ITEM] = "ct_play_item",
	[EVT_CT_ADD_TO_NOW_PLAYING] = "ct_add_to_now_playing",
	[EVT_CT_BROWSED_PLAYER] = "ct_browsed_player",
	[EVT_CT_FOLDER_ITEMS] = "ct_folder_items",
	[EVT_CT_CHANGE_PATH] = "ct_change_path",
	[EVT_CT_ITEM_ATTRS] = "ct_item_attrs",
	[EVT_CT_TOTAL_ITEMS] = "ct_total_items",
	[EVT_CT_SEARCH] = "ct_search",
	[EVT_CT_GENERAL_REJECT] = "ct_general_reject",
	[EVT_TG_UNIT_INFO] = "tg_unit_info",
	[EVT_TG_SUBUNIT_INFO] = "tg_subunit_info",
	[EVT_TG_PASSTHROUGH] = "tg_passthrough",
	[EVT_TG_GET_CAPS] = "tg_get_caps",
	[EVT_TG_REGISTER_NOTIFICATION] = "tg_register_notification",
	[EVT_TG_LIST_ATTRS] = "tg_list_attrs",
	[EVT_TG_LIST_VALS] = "tg_list_vals",
	[EVT_TG_GET_CURR_VAL] = "tg_get_curr_val",
	[EVT_TG_SET_VAL] = "tg_set_val",
	[EVT_TG_ATTR_TEXT] = "tg_attr_text",
	[EVT_TG_VAL_TEXT] = "tg_val_text",
	[EVT_TG_DISPLAYABLE_CHAR_SET] = "tg_displayable_char_set",
	[EVT_TG_BATT_STATUS] = "tg_batt_status",
	[EVT_TG_ABSOLUTE_VOLUME] = "tg_absolute_volume",
	[EVT_TG_ELEMENT_ATTRS] = "tg_element_attrs",
	[EVT_TG_PLAY_STATUS] = "tg_play_status",
	[EVT_TG_ADDRESSED_PLAYER] = "tg_addressed_player",
	[EVT_TG_PLAY_ITEM] = "tg_play_item",
	[EVT_TG_ADD_TO_NOW_PLAYING] = "tg_add_to_now_playing",
	[EVT_TG_BROWSED_PLAYER] = "tg_browsed_player",
	[EVT_TG_FOLDER_ITEMS] = "tg_folder_items",
	[EVT_TG_CHANGE_PATH] = "tg_change_path",
	[EVT_TG_ITEM_ATTRS] = "tg_item_attrs",
	[EVT_TG_TOTAL_ITEMS] = "tg_total_items",
	[EVT_TG_SEARCH] = "tg_search",
};

BUILD_ASSERT(ARRAY_SIZE(evt_names) == EVT_COUNT);

const char *test_evt_str(enum test_evt type)
{
	return ((type < EVT_COUNT) && (evt_names[type] != NULL)) ? evt_names[type] : "?";
}

void test_evt_reset(void)
{
	enum test_evt unexpected = EVT_COUNT;
	int err;

	k_mutex_lock(&evt_lock, K_FOREVER);
	err = evt_error;
	for (size_t slot = 0; slot < EVT_SLOTS; slot++) {
		if (evt_used[slot] && (evt_slots[slot].type != EVT_CT_DISCONNECTED) &&
		    (evt_slots[slot].type != EVT_TG_DISCONNECTED) &&
		    (evt_slots[slot].type != EVT_CT_BROWSING_DISCONNECTED) &&
		    (evt_slots[slot].type != EVT_TG_BROWSING_DISCONNECTED)) {
			unexpected = evt_slots[slot].type;
			break;
		}
	}
	memset(evt_used, 0, sizeof(evt_used));
	evt_error = 0;
	k_mutex_unlock(&evt_lock);

	zassert_equal(err, 0, "Event store error %d", err);
	zassert_equal(unexpected, EVT_COUNT, "Unconsumed %s event", test_evt_str(unexpected));
}

struct evt_args {
	uint8_t tid;
	uint8_t status;
	uint8_t event_id;
	uint32_t value;
	uint32_t value2;
	const void *data;
	size_t len;
	bool has_data;
};

static void evt_post(enum test_evt type, const struct evt_args *args)
{
	struct test_event *evt;
	size_t slot = EVT_SLOTS;

	LOG_DBG("event %s tid %u status 0x%02x value 0x%x len %zu", test_evt_str(type), args->tid,
		args->status, args->value, args->len);

	k_mutex_lock(&evt_lock, K_FOREVER);

	for (size_t i = 0; i < EVT_SLOTS; i++) {
		if (!evt_used[i]) {
			slot = i;
			break;
		}
	}

	if (slot == EVT_SLOTS) {
		evt_error = -ENOBUFS;
		k_condvar_broadcast(&evt_cond);
		k_mutex_unlock(&evt_lock);
		LOG_ERR("Event store full, %s dropped", test_evt_str(type));
		return;
	}

	evt = &evt_slots[slot];
	evt->type = type;
	evt->tid = args->tid;
	evt->status = args->status;
	evt->event_id = args->event_id;
	evt->value = args->value;
	evt->value2 = args->value2;
	evt->has_data = args->has_data;
	evt->len = MIN(args->len, sizeof(evt->data));
	if (evt->len < args->len) {
		evt_error = -EMSGSIZE;
		LOG_ERR("%s: %zu bytes truncated to %u", test_evt_str(type), args->len, evt->len);
	}
	if (evt->len > 0U) {
		memcpy(evt->data, args->data, evt->len);
	}

	evt_seq[slot] = evt_next_seq++;
	evt_used[slot] = true;

	k_condvar_broadcast(&evt_cond);
	k_mutex_unlock(&evt_lock);
}

static void evt_post_simple(enum test_evt type, uint8_t tid, uint32_t value)
{
	struct evt_args args = {
		.tid = tid,
		.value = value,
	};

	evt_post(type, &args);
}

static void evt_post_buf(enum test_evt type, uint8_t tid, uint8_t status, struct net_buf *buf)
{
	struct evt_args args = {
		.tid = tid,
		.status = status,
		.has_data = (buf != NULL),
		.data = (buf != NULL) ? buf->data : NULL,
		.len = (buf != NULL) ? buf->len : 0U,
	};

	evt_post(type, &args);
}

/* Browsing responses: the status is the first byte of the buffer. */
static void evt_post_browsing_rsp(enum test_evt type, uint8_t tid, struct net_buf *buf)
{
	uint8_t status = BT_AVRCP_STATUS_INTERNAL_ERROR;

	if ((buf != NULL) && (buf->len > 0U)) {
		status = buf->data[0];
	}

	evt_post_buf(type, tid, status, buf);
}

/* Index of the oldest unconsumed event of @p type, or -1. Called with evt_lock held. */
static int evt_find(enum test_evt type)
{
	int found = -1;

	for (int i = 0; i < EVT_SLOTS; i++) {
		if (evt_used[i] && (evt_slots[i].type == type) &&
		    ((found < 0) || ((int32_t)(evt_seq[i] - evt_seq[found]) < 0))) {
			found = i;
		}
	}

	return found;
}

int test_evt_wait(enum test_evt type, k_timeout_t timeout, struct test_event *evt)
{
	k_timepoint_t end = sys_timepoint_calc(timeout);
	int idx;

	k_mutex_lock(&evt_lock, K_FOREVER);

	if (evt_error != 0) {
		int err = evt_error;

		k_mutex_unlock(&evt_lock);
		return err;
	}
	while ((idx = evt_find(type)) < 0) {
		if (evt_error != 0) {
			int err = evt_error;

			k_mutex_unlock(&evt_lock);
			return err;
		}
		if (!test_br_connected()) {
			k_mutex_unlock(&evt_lock);
			return -ENOTCONN;
		}

		if (sys_timepoint_expired(end)) {
			k_mutex_unlock(&evt_lock);
			return -EAGAIN;
		}

		(void)k_condvar_wait(&evt_cond, &evt_lock, K_MSEC(50));
	}

	if (evt != NULL) {
		*evt = evt_slots[idx];
	}
	evt_used[idx] = false;

	k_mutex_unlock(&evt_lock);

	return 0;
}

void test_evt_none(enum test_evt type, k_timeout_t timeout)
{
	struct test_event evt;
	int err;

	err = test_evt_wait(type, timeout, &evt);
	zassert_equal(err, -EAGAIN, "Expected no %s event, got %d", test_evt_str(type), err);
}

/* Event data of a notification: the scalar values, the identifier or the setting pairs. */
static void evt_post_event_data(enum test_evt type, uint8_t tid, uint8_t status, uint8_t event_id,
				const struct bt_avrcp_event_data *data)
{
	struct evt_args args = {
		.tid = tid,
		.status = status,
		.event_id = event_id,
		.has_data = (data != NULL),
	};

	if (data == NULL) {
		evt_post(type, &args);
		return;
	}

	switch (event_id) {
	case BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED:
		args.value = data->play_status;
		break;
	case BT_AVRCP_EVT_TRACK_CHANGED:
		args.data = data->identifier;
		args.len = sizeof(data->identifier);
		break;
	case BT_AVRCP_EVT_PLAYBACK_POS_CHANGED:
		args.value = data->playback_pos;
		break;
	case BT_AVRCP_EVT_BATT_STATUS_CHANGED:
		args.value = data->battery_status;
		break;
	case BT_AVRCP_EVT_SYSTEM_STATUS_CHANGED:
		args.value = data->system_status;
		break;
	case BT_AVRCP_EVT_PLAYER_APP_SETTING_CHANGED:
		args.value = data->setting_changed.num_of_attr;
		args.data = data->setting_changed.attr_vals;
		args.len = args.value * sizeof(struct bt_avrcp_app_setting_attr_val);
		break;
	case BT_AVRCP_EVT_ADDRESSED_PLAYER_CHANGED:
		args.value = data->addressed_player_changed.player_id;
		args.value2 = data->addressed_player_changed.uid_counter;
		break;
	case BT_AVRCP_EVT_UIDS_CHANGED:
		args.value = data->uid_counter;
		break;
	case BT_AVRCP_EVT_VOLUME_CHANGED:
		args.value = data->absolute_volume;
		break;
	default:
		break;
	}

	evt_post(type, &args);
}

/* CT callbacks. */

static void ct_connected(struct bt_conn *conn, struct bt_avrcp_ct *ct)
{
	test_ct = ct;
	evt_post_simple(EVT_CT_CONNECTED, 0U, 0U);
}

static void ct_disconnected(struct bt_avrcp_ct *ct)
{
	test_ct = NULL;
	evt_post_simple(EVT_CT_DISCONNECTED, 0U, 0U);
}

static void ct_browsing_connected(struct bt_conn *conn, struct bt_avrcp_ct *ct)
{
	evt_post_simple(EVT_CT_BROWSING_CONNECTED, 0U, 0U);
}

static void ct_browsing_disconnected(struct bt_avrcp_ct *ct)
{
	evt_post_simple(EVT_CT_BROWSING_DISCONNECTED, 0U, 0U);
}

static void ct_get_caps(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t status, struct net_buf *buf)
{
	evt_post_buf(EVT_CT_GET_CAPS, tid, status, buf);
}

static void ct_unit_info_rsp(struct bt_avrcp_ct *ct, uint8_t tid,
			     struct bt_avrcp_unit_info_rsp *rsp)
{
	struct evt_args args = {
		.tid = tid,
		.value = rsp->unit_type,
		.value2 = rsp->company_id,
	};

	evt_post(EVT_CT_UNIT_INFO, &args);
}

static void ct_subunit_info_rsp(struct bt_avrcp_ct *ct, uint8_t tid,
				struct bt_avrcp_subunit_info_rsp *rsp)
{
	struct evt_args args = {
		.tid = tid,
		.value = rsp->subunit_type,
		.value2 = rsp->max_subunit_id,
	};

	evt_post(EVT_CT_SUBUNIT_INFO, &args);
}

static void ct_passthrough_rsp(struct bt_avrcp_ct *ct, uint8_t tid, bt_avrcp_rsp_t result,
			       const struct bt_avrcp_passthrough_rsp *rsp)
{
	struct evt_args args = {
		.tid = tid,
		.value = result,
		.has_data = true,
		.data = rsp,
		.len = sizeof(*rsp) + rsp->data_len,
	};

	evt_post(EVT_CT_PASSTHROUGH, &args);
}

static void ct_set_browsed_player(struct bt_avrcp_ct *ct, uint8_t tid, struct net_buf *buf)
{
	evt_post_browsing_rsp(EVT_CT_BROWSED_PLAYER, tid, buf);
}

static void ct_get_folder_items(struct bt_avrcp_ct *ct, uint8_t tid, struct net_buf *buf)
{
	evt_post_browsing_rsp(EVT_CT_FOLDER_ITEMS, tid, buf);
}

static void ct_change_path(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t status, uint32_t num_items)
{
	struct evt_args args = {
		.tid = tid,
		.status = status,
		.value = num_items,
	};

	evt_post(EVT_CT_CHANGE_PATH, &args);
}

static void ct_get_item_attrs(struct bt_avrcp_ct *ct, uint8_t tid, struct net_buf *buf)
{
	evt_post_browsing_rsp(EVT_CT_ITEM_ATTRS, tid, buf);
}

static void ct_get_total_number_of_items(struct bt_avrcp_ct *ct, uint8_t tid, struct net_buf *buf)
{
	evt_post_browsing_rsp(EVT_CT_TOTAL_ITEMS, tid, buf);
}

static void ct_search(struct bt_avrcp_ct *ct, uint8_t tid, struct net_buf *buf)
{
	evt_post_browsing_rsp(EVT_CT_SEARCH, tid, buf);
}

static void ct_browsing_general_reject(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t status)
{
	struct evt_args args = {
		.tid = tid,
		.status = status,
	};

	evt_post(EVT_CT_GENERAL_REJECT, &args);
}

static void ct_notification(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t status, uint8_t event_id,
			    struct bt_avrcp_event_data *data)
{
	evt_post_event_data(EVT_CT_NOTIFICATION, tid, status, event_id, data);
}

static void ct_list_player_app_setting_attrs(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t status,
					     struct net_buf *buf)
{
	evt_post_buf(EVT_CT_LIST_ATTRS, tid, status, buf);
}

static void ct_list_player_app_setting_vals(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t status,
					    struct net_buf *buf)
{
	evt_post_buf(EVT_CT_LIST_VALS, tid, status, buf);
}

static void ct_get_curr_player_app_setting_val(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t status,
					       struct net_buf *buf)
{
	evt_post_buf(EVT_CT_GET_CURR_VAL, tid, status, buf);
}

static void ct_set_player_app_setting_val(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t status)
{
	evt_post_buf(EVT_CT_SET_VAL, tid, status, NULL);
}

static void ct_get_player_app_setting_attr_text(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t status,
						struct net_buf *buf)
{
	evt_post_buf(EVT_CT_ATTR_TEXT, tid, status, buf);
}

static void ct_get_player_app_setting_val_text(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t status,
					       struct net_buf *buf)
{
	evt_post_buf(EVT_CT_VAL_TEXT, tid, status, buf);
}

static void ct_inform_displayable_char_set(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t status)
{
	evt_post_buf(EVT_CT_DISPLAYABLE_CHAR_SET, tid, status, NULL);
}

static void ct_inform_batt_status_of_ct(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t status)
{
	evt_post_buf(EVT_CT_BATT_STATUS, tid, status, NULL);
}

static void ct_set_absolute_volume(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t status,
				   uint8_t absolute_volume)
{
	struct evt_args args = {
		.tid = tid,
		.status = status,
		.value = absolute_volume,
	};

	evt_post(EVT_CT_ABSOLUTE_VOLUME, &args);
}

static void ct_get_element_attrs(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t status,
				 struct net_buf *buf)
{
	evt_post_buf(EVT_CT_ELEMENT_ATTRS, tid, status, buf);
}

static void ct_get_play_status(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t status,
			       struct net_buf *buf)
{
	evt_post_buf(EVT_CT_PLAY_STATUS, tid, status, buf);
}

static void ct_set_addressed_player(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t status)
{
	evt_post_buf(EVT_CT_ADDRESSED_PLAYER, tid, status, NULL);
}

static void ct_play_item(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t status)
{
	evt_post_buf(EVT_CT_PLAY_ITEM, tid, status, NULL);
}

static void ct_add_to_now_playing(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t status)
{
	evt_post_buf(EVT_CT_ADD_TO_NOW_PLAYING, tid, status, NULL);
}

void test_ct_notify_changed(struct bt_avrcp_ct *ct, uint8_t event_id,
			    struct bt_avrcp_event_data *data)
{
	evt_post_event_data(EVT_CT_NOTIFY_CHANGED, 0U, BT_AVRCP_STATUS_SUCCESS, event_id, data);
}

struct bt_avrcp_ct_cb test_ct_cb;

static const struct bt_avrcp_ct_cb ct_cb_default = {
	.connected = ct_connected,
	.disconnected = ct_disconnected,
	.browsing_connected = ct_browsing_connected,
	.browsing_disconnected = ct_browsing_disconnected,
	.get_caps = ct_get_caps,
	.unit_info_rsp = ct_unit_info_rsp,
	.subunit_info_rsp = ct_subunit_info_rsp,
	.passthrough_rsp = ct_passthrough_rsp,
	.set_browsed_player = ct_set_browsed_player,
	.get_folder_items = ct_get_folder_items,
	.change_path = ct_change_path,
	.get_item_attrs = ct_get_item_attrs,
	.get_total_number_of_items = ct_get_total_number_of_items,
	.search = ct_search,
	.browsing_general_reject = ct_browsing_general_reject,
	.notification = ct_notification,
	.list_player_app_setting_attrs = ct_list_player_app_setting_attrs,
	.list_player_app_setting_vals = ct_list_player_app_setting_vals,
	.get_curr_player_app_setting_val = ct_get_curr_player_app_setting_val,
	.set_player_app_setting_val = ct_set_player_app_setting_val,
	.get_player_app_setting_attr_text = ct_get_player_app_setting_attr_text,
	.get_player_app_setting_val_text = ct_get_player_app_setting_val_text,
	.inform_displayable_char_set = ct_inform_displayable_char_set,
	.inform_batt_status_of_ct = ct_inform_batt_status_of_ct,
	.set_absolute_volume = ct_set_absolute_volume,
	.get_element_attrs = ct_get_element_attrs,
	.get_play_status = ct_get_play_status,
	.set_addressed_player = ct_set_addressed_player,
	.play_item = ct_play_item,
	.add_to_now_playing = ct_add_to_now_playing,
};

/* TG callbacks. */

static void tg_connected(struct bt_conn *conn, struct bt_avrcp_tg *tg)
{
	test_tg = tg;
	evt_post_simple(EVT_TG_CONNECTED, 0U, 0U);
}

static void tg_disconnected(struct bt_avrcp_tg *tg)
{
	test_tg = NULL;
	evt_post_simple(EVT_TG_DISCONNECTED, 0U, 0U);
}

static void tg_unit_info_req(struct bt_avrcp_tg *tg, uint8_t tid)
{
	evt_post_simple(EVT_TG_UNIT_INFO, tid, 0U);
}

static void tg_register_notification(struct bt_avrcp_tg *tg, uint8_t tid, uint8_t event_id,
				     uint32_t interval)
{
	struct evt_args args = {
		.tid = tid,
		.event_id = event_id,
		.value = interval,
	};

	evt_post(EVT_TG_REGISTER_NOTIFICATION, &args);
}

static void tg_subunit_info_req(struct bt_avrcp_tg *tg, uint8_t tid)
{
	evt_post_simple(EVT_TG_SUBUNIT_INFO, tid, 0U);
}

static void tg_get_caps(struct bt_avrcp_tg *tg, uint8_t tid, uint8_t cap_id)
{
	evt_post_simple(EVT_TG_GET_CAPS, tid, cap_id);
}

static void tg_browsing_connected(struct bt_conn *conn, struct bt_avrcp_tg *tg)
{
	evt_post_simple(EVT_TG_BROWSING_CONNECTED, 0U, 0U);
}

static void tg_browsing_disconnected(struct bt_avrcp_tg *tg)
{
	evt_post_simple(EVT_TG_BROWSING_DISCONNECTED, 0U, 0U);
}

static void tg_set_browsed_player(struct bt_avrcp_tg *tg, uint8_t tid, uint16_t player_id)
{
	evt_post_simple(EVT_TG_BROWSED_PLAYER, tid, player_id);
}

static void tg_get_folder_items(struct bt_avrcp_tg *tg, uint8_t tid, struct net_buf *buf)
{
	evt_post_buf(EVT_TG_FOLDER_ITEMS, tid, 0U, buf);
}

static void tg_change_path(struct bt_avrcp_tg *tg, uint8_t tid, struct net_buf *buf)
{
	evt_post_buf(EVT_TG_CHANGE_PATH, tid, 0U, buf);
}

static void tg_get_item_attrs(struct bt_avrcp_tg *tg, uint8_t tid, struct net_buf *buf)
{
	evt_post_buf(EVT_TG_ITEM_ATTRS, tid, 0U, buf);
}

static void tg_get_total_number_of_items(struct bt_avrcp_tg *tg, uint8_t tid, uint8_t scope)
{
	evt_post_simple(EVT_TG_TOTAL_ITEMS, tid, scope);
}

static void tg_search(struct bt_avrcp_tg *tg, uint8_t tid, struct net_buf *buf)
{
	evt_post_buf(EVT_TG_SEARCH, tid, 0U, buf);
}

static void tg_passthrough_req(struct bt_avrcp_tg *tg, uint8_t tid, struct net_buf *buf)
{
	evt_post_buf(EVT_TG_PASSTHROUGH, tid, 0U, buf);
}

static void tg_list_player_app_setting_attrs(struct bt_avrcp_tg *tg, uint8_t tid)
{
	evt_post_simple(EVT_TG_LIST_ATTRS, tid, 0U);
}

static void tg_list_player_app_setting_vals(struct bt_avrcp_tg *tg, uint8_t tid, uint8_t attr_id)
{
	evt_post_simple(EVT_TG_LIST_VALS, tid, attr_id);
}

static void tg_get_curr_player_app_setting_val(struct bt_avrcp_tg *tg, uint8_t tid,
					       struct net_buf *buf)
{
	evt_post_buf(EVT_TG_GET_CURR_VAL, tid, 0U, buf);
}

static void tg_set_player_app_setting_val(struct bt_avrcp_tg *tg, uint8_t tid, struct net_buf *buf)
{
	evt_post_buf(EVT_TG_SET_VAL, tid, 0U, buf);
}

static void tg_get_player_app_setting_attr_text(struct bt_avrcp_tg *tg, uint8_t tid,
						struct net_buf *buf)
{
	evt_post_buf(EVT_TG_ATTR_TEXT, tid, 0U, buf);
}

static void tg_get_player_app_setting_val_text(struct bt_avrcp_tg *tg, uint8_t tid,
					       struct net_buf *buf)
{
	evt_post_buf(EVT_TG_VAL_TEXT, tid, 0U, buf);
}

static void tg_inform_displayable_char_set(struct bt_avrcp_tg *tg, uint8_t tid, struct net_buf *buf)
{
	evt_post_buf(EVT_TG_DISPLAYABLE_CHAR_SET, tid, 0U, buf);
}

static void tg_inform_batt_status_of_ct(struct bt_avrcp_tg *tg, uint8_t tid, uint8_t battery_status)
{
	evt_post_simple(EVT_TG_BATT_STATUS, tid, battery_status);
}

static void tg_get_element_attrs(struct bt_avrcp_tg *tg, uint8_t tid, struct net_buf *buf)
{
	evt_post_buf(EVT_TG_ELEMENT_ATTRS, tid, 0U, buf);
}

static void tg_set_absolute_volume(struct bt_avrcp_tg *tg, uint8_t tid, uint8_t absolute_volume)
{
	evt_post_simple(EVT_TG_ABSOLUTE_VOLUME, tid, absolute_volume);
}

static void tg_get_play_status(struct bt_avrcp_tg *tg, uint8_t tid)
{
	evt_post_simple(EVT_TG_PLAY_STATUS, tid, 0U);
}

static void tg_set_addressed_player(struct bt_avrcp_tg *tg, uint8_t tid, uint16_t player_id)
{
	evt_post_simple(EVT_TG_ADDRESSED_PLAYER, tid, player_id);
}

static void tg_play_item(struct bt_avrcp_tg *tg, uint8_t tid, struct net_buf *buf)
{
	evt_post_buf(EVT_TG_PLAY_ITEM, tid, 0U, buf);
}

static void tg_add_to_now_playing(struct bt_avrcp_tg *tg, uint8_t tid, struct net_buf *buf)
{
	evt_post_buf(EVT_TG_ADD_TO_NOW_PLAYING, tid, 0U, buf);
}

struct bt_avrcp_tg_cb test_tg_cb;

static const struct bt_avrcp_tg_cb tg_cb_default = {
	.connected = tg_connected,
	.disconnected = tg_disconnected,
	.unit_info_req = tg_unit_info_req,
	.register_notification = tg_register_notification,
	.subunit_info_req = tg_subunit_info_req,
	.get_caps = tg_get_caps,
	.browsing_connected = tg_browsing_connected,
	.browsing_disconnected = tg_browsing_disconnected,
	.set_browsed_player = tg_set_browsed_player,
	.get_folder_items = tg_get_folder_items,
	.change_path = tg_change_path,
	.get_item_attrs = tg_get_item_attrs,
	.get_total_number_of_items = tg_get_total_number_of_items,
	.search = tg_search,
	.passthrough_req = tg_passthrough_req,
	.list_player_app_setting_attrs = tg_list_player_app_setting_attrs,
	.list_player_app_setting_vals = tg_list_player_app_setting_vals,
	.get_curr_player_app_setting_val = tg_get_curr_player_app_setting_val,
	.set_player_app_setting_val = tg_set_player_app_setting_val,
	.get_player_app_setting_attr_text = tg_get_player_app_setting_attr_text,
	.get_player_app_setting_val_text = tg_get_player_app_setting_val_text,
	.inform_displayable_char_set = tg_inform_displayable_char_set,
	.inform_batt_status_of_ct = tg_inform_batt_status_of_ct,
	.get_element_attrs = tg_get_element_attrs,
	.set_absolute_volume = tg_set_absolute_volume,
	.get_play_status = tg_get_play_status,
	.set_addressed_player = tg_set_addressed_player,
	.play_item = tg_play_item,
	.add_to_now_playing = tg_add_to_now_playing,
};

void test_avrcp_restore_cb(void)
{
	test_ct_cb = ct_cb_default;
	test_tg_cb = tg_cb_default;
}

void test_avrcp_suite_setup(enum test_br_role role)
{
	/* The AVCTP control and browsing channels require security level 2. */
	test_br_setup(role, BT_SECURITY_L2);

	test_avrcp_restore_cb();
	TEST_OK(bt_avrcp_ct_register_cb(&test_ct_cb));
	TEST_OK(bt_avrcp_tg_register_cb(&test_tg_cb));
}

void test_avrcp_suite_teardown(void)
{
	test_evt_reset();
	test_br_teardown();
}

void test_avrcp_before(void)
{
	test_avrcp_restore_cb();
	test_evt_reset();
	ct_tid = 0U;
}

uint8_t test_next_tid(void)
{
	uint8_t tid = ct_tid;

	ct_tid = (ct_tid + 1U) & 0x0FU;

	return tid;
}

void test_avrcp_connect(void)
{
	TEST_OK(bt_avrcp_connect(test_br_conn()));
	test_avrcp_wait_connected();
}

void test_avrcp_wait_connected(void)
{
	TEST_EXPECT(EVT_CT_CONNECTED, NULL);
	TEST_EXPECT(EVT_TG_CONNECTED, NULL);
	zassert_not_null(test_ct, "No AVRCP CT");
	zassert_not_null(test_tg, "No AVRCP TG");
}

void test_avrcp_disconnect(void)
{
	TEST_OK(bt_avrcp_disconnect(test_br_conn()));
	test_avrcp_wait_disconnected();
}

void test_avrcp_wait_disconnected(void)
{
	TEST_EXPECT(EVT_CT_DISCONNECTED, NULL);
	TEST_EXPECT(EVT_TG_DISCONNECTED, NULL);
}

void test_browsing_connect(void)
{
	TEST_OK(bt_avrcp_browsing_connect(test_br_conn()));
	test_browsing_wait_connected();
}

void test_browsing_wait_connected(void)
{
	TEST_EXPECT(EVT_CT_BROWSING_CONNECTED, NULL);
	TEST_EXPECT(EVT_TG_BROWSING_CONNECTED, NULL);
}

void test_browsing_disconnect(void)
{
	TEST_OK(bt_avrcp_browsing_disconnect(test_br_conn()));
	test_browsing_wait_disconnected();
}

void test_browsing_wait_disconnected(void)
{
	TEST_EXPECT(EVT_CT_BROWSING_DISCONNECTED, NULL);
	TEST_EXPECT(EVT_TG_BROWSING_DISCONNECTED, NULL);
}

static void ct_connect(bool browsing)
{
	test_avrcp_connect();
	if (browsing) {
		test_browsing_connect();
	}
}

static void tg_wait_connected(bool browsing)
{
	test_avrcp_wait_connected();
	if (browsing) {
		test_browsing_wait_connected();
	}
}

void test_ct_run_twice(void (*body)(void), bool browsing)
{
	test_br_sync();
	ct_connect(browsing);
	body();

	/* The CT releases the first connection. */
	if (browsing) {
		test_browsing_disconnect();
	}
	test_avrcp_disconnect();

	test_br_sync();
	ct_connect(browsing);
	body();

	/* The TG sends its last response before it releases the second connection. */
	test_br_sync();
	if (browsing) {
		test_browsing_wait_disconnected();
	}
	test_avrcp_wait_disconnected();

	test_br_sync();
	ct_connect(browsing);
	body();
	test_avrcp_disconnect();
	if (browsing) {
		test_browsing_wait_disconnected();
	}

	test_br_sync();
}

void test_tg_run_twice(void (*body)(void), bool browsing)
{
	test_br_sync();
	tg_wait_connected(browsing);
	body();

	if (browsing) {
		test_browsing_wait_disconnected();
	}
	test_avrcp_wait_disconnected();

	test_br_sync();
	tg_wait_connected(browsing);
	body();

	/*
	 * The responses are sent from a work item: the CT confirms the last one before the TG
	 * releases the connection. bt_avrcp_disconnect() closes the browsing channel first.
	 */
	test_br_sync();
	if (browsing) {
		test_browsing_disconnect();
	}
	test_avrcp_disconnect();

	test_br_sync();
	tg_wait_connected(browsing);
	body();
	test_avrcp_wait_disconnected();
	if (browsing) {
		test_browsing_wait_disconnected();
	}

	test_br_sync();
}

struct net_buf *test_vendor_buf(void)
{
	struct net_buf *buf = bt_avrcp_create_vendor_pdu(&test_tx_pool);

	zassert_not_null(buf, "No vendor dependent PDU buffer");

	return buf;
}

struct net_buf *test_pdu_buf(void)
{
	struct net_buf *buf = bt_avrcp_create_pdu(&test_tx_pool);

	zassert_not_null(buf, "No PDU buffer");

	return buf;
}

void test_ct_expect(enum test_evt type, uint8_t tid, struct test_event *evt)
{
	TEST_EXPECT(type, evt);
	zassert_equal(evt->tid, tid, "%s: tid %u, expected %u", test_evt_str(type), evt->tid, tid);
}

void test_ct_expect_status(enum test_evt type, uint8_t tid, uint8_t status)
{
	struct test_event evt;

	test_ct_expect(type, tid, &evt);
	zassert_equal(evt.status, status, "%s: status 0x%02x, expected 0x%02x", test_evt_str(type),
		      evt.status, status);
	zassert_false(evt.has_data, "%s: data with status 0x%02x", test_evt_str(type), status);
}

void test_ct_expect_browsing_status(enum test_evt type, uint8_t tid, uint8_t status)
{
	struct test_event evt;

	test_ct_expect(type, tid, &evt);
	zassert_equal(evt.len, 1U, "%s: %u bytes, expected the status only", test_evt_str(type),
		      evt.len);
	zassert_equal(evt.status, status, "%s: status 0x%02x, expected 0x%02x", test_evt_str(type),
		      evt.status, status);
}

/* Notification values: interim, then changed. */
static struct bt_avrcp_app_setting_attr_val setting_interim[] = {
	{BT_AVRCP_PLAYER_ATTR_EQUALIZER, BT_AVRCP_EQUALIZER_ON},
	{BT_AVRCP_PLAYER_ATTR_REPEAT_MODE, BT_AVRCP_REPEAT_MODE_ALL_TRACKS},
};

static struct bt_avrcp_app_setting_attr_val setting_changed[] = {
	{BT_AVRCP_PLAYER_ATTR_SHUFFLE, BT_AVRCP_SHUFFLE_GROUP},
};

#define TEST_POS_INTERIM       1000U
#define TEST_POS_CHANGED       2000U
#define TEST_PLAYER_ID_INTERIM 0x0102U
#define TEST_UID_CTR_INTERIM   0x0304U
#define TEST_PLAYER_ID_CHANGED 0x0005U
#define TEST_UID_CTR_CHANGED   0x0006U
#define TEST_UIDS_INTERIM      0x1234U
#define TEST_UIDS_CHANGED      0x1235U

void test_event_data(uint8_t event_id, bool changed, struct bt_avrcp_event_data *data)
{
	memset(data, 0, sizeof(*data));

	switch (event_id) {
	case BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED:
		data->play_status = changed ? BT_AVRCP_PLAYBACK_STATUS_PAUSED
					    : BT_AVRCP_PLAYBACK_STATUS_PLAYING;
		break;
	case BT_AVRCP_EVT_TRACK_CHANGED:
		/* The TG gives the identifier in big-endian order. */
		sys_put_be64(changed ? TEST_TRACK_CHANGED : TEST_TRACK_INTERIM, data->identifier);
		break;
	case BT_AVRCP_EVT_PLAYBACK_POS_CHANGED:
		data->playback_pos = changed ? TEST_POS_CHANGED : TEST_POS_INTERIM;
		break;
	case BT_AVRCP_EVT_BATT_STATUS_CHANGED:
		data->battery_status =
			changed ? BT_AVRCP_BATTERY_STATUS_FULL : BT_AVRCP_BATTERY_STATUS_WARNING;
		break;
	case BT_AVRCP_EVT_SYSTEM_STATUS_CHANGED:
		data->system_status = changed ? BT_AVRCP_SYSTEM_STATUS_UNPLUGGED
					      : BT_AVRCP_SYSTEM_STATUS_POWER_ON;
		break;
	case BT_AVRCP_EVT_PLAYER_APP_SETTING_CHANGED:
		if (changed) {
			data->setting_changed.num_of_attr = ARRAY_SIZE(setting_changed);
			data->setting_changed.attr_vals = setting_changed;
		} else {
			data->setting_changed.num_of_attr = ARRAY_SIZE(setting_interim);
			data->setting_changed.attr_vals = setting_interim;
		}
		break;
	case BT_AVRCP_EVT_ADDRESSED_PLAYER_CHANGED:
		data->addressed_player_changed.player_id =
			changed ? TEST_PLAYER_ID_CHANGED : TEST_PLAYER_ID_INTERIM;
		data->addressed_player_changed.uid_counter =
			changed ? TEST_UID_CTR_CHANGED : TEST_UID_CTR_INTERIM;
		break;
	case BT_AVRCP_EVT_UIDS_CHANGED:
		data->uid_counter = changed ? TEST_UIDS_CHANGED : TEST_UIDS_INTERIM;
		break;
	case BT_AVRCP_EVT_VOLUME_CHANGED:
		data->absolute_volume = changed ? TEST_VOLUME_CHANGED : TEST_VOLUME_INTERIM;
		break;
	default:
		/* TRACK_REACHED_END/START, NOW_PLAYING_CONTENT and AVAILABLE_PLAYERS: no data. */
		break;
	}
}

void test_check_event_data(const struct test_event *evt, bool changed)
{
	struct bt_avrcp_event_data exp;
	uint64_t identifier;

	test_event_data(evt->event_id, changed, &exp);

	switch (evt->event_id) {
	case BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED:
		zassert_equal(evt->value, exp.play_status, "Play status %u", evt->value);
		break;
	case BT_AVRCP_EVT_TRACK_CHANGED:
		/* The CT reports the identifier in host order (avrcp.c converts it). */
		zassert_equal(evt->len, sizeof(identifier), "Identifier length %u", evt->len);
		memcpy(&identifier, evt->data, sizeof(identifier));
		zassert_equal(identifier, sys_get_be64(exp.identifier), "Identifier 0x%llx",
			      (unsigned long long)identifier);
		break;
	case BT_AVRCP_EVT_PLAYBACK_POS_CHANGED:
		zassert_equal(evt->value, exp.playback_pos, "Position %u", evt->value);
		break;
	case BT_AVRCP_EVT_BATT_STATUS_CHANGED:
		zassert_equal(evt->value, exp.battery_status, "Battery status %u", evt->value);
		break;
	case BT_AVRCP_EVT_SYSTEM_STATUS_CHANGED:
		zassert_equal(evt->value, exp.system_status, "System status %u", evt->value);
		break;
	case BT_AVRCP_EVT_PLAYER_APP_SETTING_CHANGED:
		zassert_equal(evt->value, exp.setting_changed.num_of_attr, "%u settings",
			      evt->value);
		zassert_equal(evt->len, evt->value * sizeof(struct bt_avrcp_app_setting_attr_val),
			      "Setting length %u", evt->len);
		zassert_mem_equal(evt->data, exp.setting_changed.attr_vals, evt->len,
				  "Unexpected settings");
		break;
	case BT_AVRCP_EVT_ADDRESSED_PLAYER_CHANGED:
		zassert_equal(evt->value, exp.addressed_player_changed.player_id, "Player 0x%04x",
			      evt->value);
		zassert_equal(evt->value2, exp.addressed_player_changed.uid_counter,
			      "UID counter 0x%04x", evt->value2);
		break;
	case BT_AVRCP_EVT_UIDS_CHANGED:
		zassert_equal(evt->value, exp.uid_counter, "UID counter 0x%04x", evt->value);
		break;
	case BT_AVRCP_EVT_VOLUME_CHANGED:
		zassert_equal(evt->value, exp.absolute_volume, "Volume 0x%02x", evt->value);
		break;
	default:
		zassert_false(evt->has_data, "Data for event 0x%02x", evt->event_id);
		return;
	}

	zassert_true(evt->has_data, "No data for event 0x%02x", evt->event_id);
}

/* The GetElementAttributes values of the board-to-board TG. */
const struct test_media_attr test_attrs_std[7] = {
	{BT_AVRCP_MEDIA_ATTR_ID_TITLE, "Test Title", 11U},
	{BT_AVRCP_MEDIA_ATTR_ID_ARTIST, "Test Artist", 11U},
	{BT_AVRCP_MEDIA_ATTR_ID_ALBUM, "Test Album", 10U},
	{BT_AVRCP_MEDIA_ATTR_ID_TRACK_NUMBER, "1", 1U},
	{BT_AVRCP_MEDIA_ATTR_ID_TOTAL_TRACKS, "10", 2U},
	{BT_AVRCP_MEDIA_ATTR_ID_GENRE, "Rock", 4U},
	{BT_AVRCP_MEDIA_ATTR_ID_PLAYING_TIME, "240000", 6U},
};

const struct test_media_attr test_attrs_large[3] = {
	{BT_AVRCP_MEDIA_ATTR_ID_TITLE,
	 "This is a long title that is designed to test the fragmentation of the AVRCP.", 200U},
	{BT_AVRCP_MEDIA_ATTR_ID_ARTIST,
	 "This is a very long artist name that is also designed to test fragmentation.", 250U},
	{BT_AVRCP_MEDIA_ATTR_ID_ALBUM,
	 "This is a long album name for testing fragmentation of AVRCP responses.", 100U},
};

const struct test_media_attr test_attr_item = {
	BT_AVRCP_MEDIA_ATTR_ID_TITLE,
	TEST_ITEM_TITLE,
	sizeof(TEST_ITEM_TITLE) - 1U,
};

void test_add_media_attrs(struct net_buf *buf, const struct test_media_attr *attrs, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		size_t val_len = MIN(strlen(attrs[i].val), attrs[i].len);

		zassert_true(net_buf_tailroom(buf) >=
				     (sizeof(struct bt_avrcp_media_attr) + attrs[i].len),
			     "No room for attribute %zu", i);
		net_buf_add_be32(buf, attrs[i].id);
		net_buf_add_be16(buf, BT_AVRCP_CHARSET_UTF8);
		net_buf_add_be16(buf, attrs[i].len);
		net_buf_add_mem(buf, attrs[i].val, val_len);
		memset(net_buf_add(buf, attrs[i].len - val_len), 0, attrs[i].len - val_len);
	}
}

void test_check_media_attrs(const uint8_t *data, size_t len, const struct test_media_attr *attrs,
			    size_t count)
{
	size_t off = 0U;

	for (size_t i = 0; i < count; i++) {
		size_t val_len = MIN(strlen(attrs[i].val), attrs[i].len);
		uint16_t attr_len;

		zassert_true((off + sizeof(struct bt_avrcp_media_attr)) <= len,
			     "Attribute %zu truncated", i);
		zassert_equal(sys_get_be32(&data[off]), attrs[i].id, "Attribute %zu ID 0x%08x", i,
			      sys_get_be32(&data[off]));
		zassert_equal(sys_get_be16(&data[off + 4U]), BT_AVRCP_CHARSET_UTF8,
			      "Attribute %zu charset 0x%04x", i, sys_get_be16(&data[off + 4U]));
		attr_len = sys_get_be16(&data[off + 6U]);
		zassert_equal(attr_len, attrs[i].len, "Attribute %zu length %u", i, attr_len);
		off += sizeof(struct bt_avrcp_media_attr);

		zassert_true((off + attr_len) <= len, "Attribute %zu value truncated", i);
		zassert_mem_equal(&data[off], attrs[i].val, val_len, "Attribute %zu value", i);
		for (size_t j = val_len; j < attr_len; j++) {
			zassert_equal(data[off + j], 0U, "Attribute %zu padding", i);
		}
		off += attr_len;
	}

	zassert_equal(off, len, "%zu bytes after the attributes", len - off);
}

static const char *const attr_texts[] = {
	[BT_AVRCP_PLAYER_ATTR_EQUALIZER] = "Equalizer",
	[BT_AVRCP_PLAYER_ATTR_REPEAT_MODE] = "Repeat Mode",
	[BT_AVRCP_PLAYER_ATTR_SHUFFLE] = "Shuffle",
	[BT_AVRCP_PLAYER_ATTR_SCAN] = "Scan",
};

static const char *const val_texts[] = {
	"Value 0", "Value 1", "Value 2", "Value 3", "Value 4",
};

const char *test_attr_text(uint8_t attr_id)
{
	zassert_true((attr_id < ARRAY_SIZE(attr_texts)) && (attr_texts[attr_id] != NULL),
		     "No text for attribute %u", attr_id);

	return attr_texts[attr_id];
}

const char *test_val_text(uint8_t val_id)
{
	zassert_true(val_id < ARRAY_SIZE(val_texts), "No text for value %u", val_id);

	return val_texts[val_id];
}
