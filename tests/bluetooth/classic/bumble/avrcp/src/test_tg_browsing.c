/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Browsing tests of the TG (device 1), counterpart of test_ct_browsing.c: the TG checks the
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

static void expect_browsed_player(uint16_t player_id, struct test_event *evt)
{
	TEST_EXPECT(EVT_TG_BROWSED_PLAYER, evt);
	zassert_equal(evt->value, player_id, "Player %u", evt->value);
}

static void browsed_player_rsp(uint16_t player_id, uint16_t charset, const uint8_t *name,
			       size_t name_len)
{
	struct test_event evt;
	struct net_buf *buf;

	expect_browsed_player(player_id, &evt);

	buf = test_pdu_buf();
	net_buf_add_u8(buf, BT_AVRCP_STATUS_SUCCESS);
	net_buf_add_be16(buf, TEST_BROWSED_UID_COUNTER);
	net_buf_add_be32(buf, TEST_TOTAL_ITEMS);
	net_buf_add_be16(buf, charset);
	net_buf_add_u8(buf, 1U);
	net_buf_add_be16(buf, name_len);
	net_buf_add_mem(buf, name, name_len);
	TEST_SEND(bt_avrcp_tg_set_browsed_player(test_tg, evt.tid, buf), buf);
}

/* Browsing response with only a status. */
static void send_status(int (*send)(struct bt_avrcp_tg *tg, uint8_t tid, struct net_buf *buf),
			uint8_t tid, uint8_t status)
{
	struct net_buf *buf = test_pdu_buf();

	net_buf_add_u8(buf, status);
	TEST_SEND(send(test_tg, tid, buf), buf);
}

static void set_browsed_player(void)
{
	static const uint8_t raw_name[] = TEST_BROWSED_FOLDER_RAW;
	struct test_event evt;

	browsed_player_rsp(TEST_PLAYER_UTF8, BT_AVRCP_CHARSET_UTF8,
			   (const uint8_t *)TEST_BROWSED_FOLDER, strlen(TEST_BROWSED_FOLDER));
	browsed_player_rsp(TEST_PLAYER_CHARSET, TEST_BROWSED_CHARSET, raw_name, sizeof(raw_name));

	expect_browsed_player(TEST_PLAYER_REJECT, &evt);
	send_status(bt_avrcp_tg_set_browsed_player, evt.tid, BT_AVRCP_STATUS_INVALID_PARAMETER);
}

ZTEST(avrcp_tg_browsing, test_001_set_browsed_player)
{
	test_tg_run_twice(set_browsed_player, true);
}

static void expect_folder_items(uint8_t scope, uint32_t end, const uint32_t *attr_ids, uint8_t num,
				struct test_event *evt)
{
	const struct bt_avrcp_get_folder_items_cmd *cmd;

	TEST_EXPECT(EVT_TG_FOLDER_ITEMS, evt);
	zassert_equal(evt->len, sizeof(*cmd) + (num * sizeof(uint32_t)), "Command length %u",
		      evt->len);
	cmd = (const struct bt_avrcp_get_folder_items_cmd *)evt->data;
	zassert_equal(cmd->scope, scope, "Scope %u", cmd->scope);
	zassert_equal(sys_be32_to_cpu(cmd->start_item), 0U, "Start item %u",
		      sys_be32_to_cpu(cmd->start_item));
	zassert_equal(sys_be32_to_cpu(cmd->end_item), end, "End item %u",
		      sys_be32_to_cpu(cmd->end_item));
	zassert_equal(cmd->attr_count, num, "%u attributes", cmd->attr_count);
	for (uint8_t i = 0U; i < num; i++) {
		zassert_equal(sys_get_be32(&evt->data[sizeof(*cmd) + (i * sizeof(uint32_t))]),
			      attr_ids[i], "Attribute %u", i);
	}
}

static void add_folder_items_hdr(struct net_buf *buf, uint16_t num_items)
{
	net_buf_add_u8(buf, BT_AVRCP_STATUS_SUCCESS);
	net_buf_add_be16(buf, TEST_UID_COUNTER);
	net_buf_add_be16(buf, num_items);
}

/* The media player item of the board-to-board TG. */
static void add_media_player_item(struct net_buf *buf)
{
	size_t name_len = strlen(TEST_PLAYER_NAME);

	net_buf_add_u8(buf, BT_AVRCP_ITEM_TYPE_MEDIA_PLAYER);
	net_buf_add_be16(buf, sizeof(struct bt_avrcp_media_player_item) -
				      sizeof(struct bt_avrcp_item_hdr) + name_len);
	net_buf_add_be16(buf, 1U);
	net_buf_add_u8(buf, 1U);
	net_buf_add_be32(buf, 0U);
	net_buf_add_u8(buf, BT_AVRCP_PLAYBACK_STATUS_STOPPED);
	(void)memset(net_buf_add(buf, 16U), 0, 16U);
	net_buf_add_be16(buf, BT_AVRCP_CHARSET_UTF8);
	net_buf_add_be16(buf, name_len);
	net_buf_add_mem(buf, TEST_PLAYER_NAME, name_len);
}

static void add_folder_and_element(struct net_buf *buf)
{
	size_t folder_name_len = strlen(TEST_FOLDER_NAME);
	size_t song_name_len = strlen(TEST_SONG_NAME);

	net_buf_add_u8(buf, BT_AVRCP_ITEM_TYPE_FOLDER);
	net_buf_add_be16(buf, sizeof(struct bt_avrcp_folder_item) -
				      sizeof(struct bt_avrcp_item_hdr) + folder_name_len);
	net_buf_add_be64(buf, TEST_FOLDER_UID);
	net_buf_add_u8(buf, BT_AVRCP_FOLDER_TYPE_TITLES);
	net_buf_add_u8(buf, 1U);
	net_buf_add_be16(buf, BT_AVRCP_CHARSET_UTF8);
	net_buf_add_be16(buf, folder_name_len);
	net_buf_add_mem(buf, TEST_FOLDER_NAME, folder_name_len);

	net_buf_add_u8(buf, BT_AVRCP_ITEM_TYPE_MEDIA_ELEMENT);
	net_buf_add_be16(buf, sizeof(struct bt_avrcp_media_element_item) -
				      sizeof(struct bt_avrcp_item_hdr) +
				      sizeof(struct media_element_item_name) + song_name_len + 1U +
				      sizeof(struct bt_avrcp_media_attr) + test_attr_item.len);
	net_buf_add_be64(buf, TEST_SONG_UID);
	net_buf_add_u8(buf, BT_AVRCP_MEDIA_TYPE_AUDIO);
	net_buf_add_be16(buf, BT_AVRCP_CHARSET_UTF8);
	net_buf_add_be16(buf, song_name_len);
	net_buf_add_mem(buf, TEST_SONG_NAME, song_name_len);
	net_buf_add_u8(buf, 1U);
	test_add_media_attrs(buf, &test_attr_item, 1U);
}

static void get_folder_items(void)
{
	static const uint32_t ids[] = {BT_AVRCP_MEDIA_ATTR_ID_TITLE};
	struct test_event evt;
	struct net_buf *buf;

	expect_folder_items(BT_AVRCP_SCOPE_VFS, TEST_FOLDER_ITEMS_END, NULL, 0U, &evt);
	buf = test_pdu_buf();
	add_folder_items_hdr(buf, 1U);
	add_media_player_item(buf);
	TEST_SEND(bt_avrcp_tg_get_folder_items(test_tg, evt.tid, buf), buf);

	expect_folder_items(BT_AVRCP_SCOPE_VFS, TEST_FOLDER_ITEMS_END, NULL, 0U, &evt);
	send_status(bt_avrcp_tg_get_folder_items, evt.tid, BT_AVRCP_STATUS_INTERNAL_ERROR);

	expect_folder_items(BT_AVRCP_SCOPE_NOW_PLAYING, 1U, ids, ARRAY_SIZE(ids), &evt);
	buf = test_pdu_buf();
	add_folder_items_hdr(buf, 2U);
	add_folder_and_element(buf);
	TEST_SEND(bt_avrcp_tg_get_folder_items(test_tg, evt.tid, buf), buf);
}

ZTEST(avrcp_tg_browsing, test_002_get_folder_items)
{
	test_tg_run_twice(get_folder_items, true);
}

static void expect_change_path(uint8_t direction, uint64_t uid, struct test_event *evt)
{
	const struct bt_avrcp_change_path_cmd *cmd;

	TEST_EXPECT(EVT_TG_CHANGE_PATH, evt);
	zassert_equal(evt->len, sizeof(*cmd), "Command length %u", evt->len);
	cmd = (const struct bt_avrcp_change_path_cmd *)evt->data;
	zassert_equal(sys_be16_to_cpu(cmd->uid_counter), TEST_UID_COUNTER, "UID counter %u",
		      sys_be16_to_cpu(cmd->uid_counter));
	zassert_equal(cmd->direction, direction, "Direction %u", cmd->direction);
	zassert_equal(sys_get_be64(cmd->folder_uid), uid, "Folder UID");
}

static void change_path_rsp(uint8_t direction, uint64_t uid, uint8_t status, uint32_t num_items)
{
	struct test_event evt;

	expect_change_path(direction, uid, &evt);
	TEST_OK(bt_avrcp_tg_change_path(test_tg, evt.tid, status, num_items));
}

static void change_path(void)
{
	change_path_rsp(BT_AVRCP_CHANGE_PATH_CHILD, TEST_UID_01, BT_AVRCP_STATUS_SUCCESS,
			TEST_CHANGE_PATH_ITEMS);
	change_path_rsp(BT_AVRCP_CHANGE_PATH_CHILD, TEST_UID_01, BT_AVRCP_STATUS_INVALID_PARAMETER,
			TEST_CHANGE_PATH_ITEMS);
	change_path_rsp(BT_AVRCP_CHANGE_PATH_PARENT, 0U, BT_AVRCP_STATUS_SUCCESS,
			TEST_CHANGE_PATH_PARENT_ITEMS);
}

ZTEST(avrcp_tg_browsing, test_003_change_path)
{
	test_tg_run_twice(change_path, true);
}

static void expect_item_attrs(uint8_t scope, struct test_event *evt)
{
	const struct bt_avrcp_get_item_attrs_cmd *cmd;

	TEST_EXPECT(EVT_TG_ITEM_ATTRS, evt);
	zassert_equal(evt->len, sizeof(*cmd), "Command length %u", evt->len);
	cmd = (const struct bt_avrcp_get_item_attrs_cmd *)evt->data;
	zassert_equal(cmd->scope, scope, "Scope %u", cmd->scope);
	zassert_equal(sys_get_be64(cmd->uid), TEST_UID_01, "UID");
	zassert_equal(sys_be16_to_cpu(cmd->uid_counter), TEST_UID_COUNTER, "UID counter %u",
		      sys_be16_to_cpu(cmd->uid_counter));
	zassert_equal(cmd->num_attrs, 0U, "%u attributes", cmd->num_attrs);
}

static void get_item_attrs(void)
{
	struct test_event evt;
	struct net_buf *buf;

	expect_item_attrs(BT_AVRCP_SCOPE_VFS, &evt);
	buf = test_pdu_buf();
	net_buf_add_u8(buf, BT_AVRCP_STATUS_SUCCESS);
	net_buf_add_u8(buf, 1U);
	test_add_media_attrs(buf, &test_attr_item, 1U);
	TEST_SEND(bt_avrcp_tg_get_item_attrs(test_tg, evt.tid, buf), buf);

	expect_item_attrs(BT_AVRCP_SCOPE_VFS, &evt);
	send_status(bt_avrcp_tg_get_item_attrs, evt.tid, BT_AVRCP_STATUS_INVALID_PARAMETER);

	expect_item_attrs(TEST_SCOPE_INVALID, &evt);
	send_status(bt_avrcp_tg_get_item_attrs, evt.tid, BT_AVRCP_STATUS_INVALID_SCOPE);
}

ZTEST(avrcp_tg_browsing, test_004_get_item_attrs)
{
	test_tg_run_twice(get_item_attrs, true);
}

static void expect_total_items(uint8_t scope, struct test_event *evt)
{
	TEST_EXPECT(EVT_TG_TOTAL_ITEMS, evt);
	zassert_equal(evt->value, scope, "Scope %u", evt->value);
}

static void get_total_number_of_items(void)
{
	struct test_event evt;

	for (uint8_t scope = BT_AVRCP_SCOPE_MEDIA_PLAYER_LIST; scope <= BT_AVRCP_SCOPE_NOW_PLAYING;
	     scope++) {
		struct net_buf *buf;

		expect_total_items(scope, &evt);
		buf = test_pdu_buf();
		net_buf_add_u8(buf, BT_AVRCP_STATUS_SUCCESS);
		net_buf_add_be16(buf, TEST_UID_COUNTER);
		net_buf_add_be32(buf, TEST_TOTAL_ITEMS);
		TEST_SEND(bt_avrcp_tg_get_total_number_of_items(test_tg, evt.tid, buf), buf);
	}

	expect_total_items(BT_AVRCP_SCOPE_VFS, &evt);
	send_status(bt_avrcp_tg_get_total_number_of_items, evt.tid,
		    BT_AVRCP_STATUS_PARAMETER_CONTENT_ERROR);
}

ZTEST(avrcp_tg_browsing, test_005_get_total_number_of_items)
{
	test_tg_run_twice(get_total_number_of_items, true);
}

static void expect_search(const char *str, struct test_event *evt)
{
	size_t len = strlen(str);

	TEST_EXPECT(EVT_TG_SEARCH, evt);
	zassert_equal(evt->len, sizeof(struct bt_avrcp_search_cmd) + len, "Command length %u",
		      evt->len);
	zassert_equal(sys_get_be16(&evt->data[0]), BT_AVRCP_CHARSET_UTF8, "Charset 0x%04x",
		      sys_get_be16(&evt->data[0]));
	zassert_equal(sys_get_be16(&evt->data[2]), len, "String length %u",
		      sys_get_be16(&evt->data[2]));
	zassert_mem_equal(&evt->data[4], str, len, "Unexpected search string");
}

static void search(void)
{
	struct test_event evt;
	struct net_buf *buf;

	expect_search(TEST_SEARCH_STR_1, &evt);
	buf = test_pdu_buf();
	net_buf_add_u8(buf, BT_AVRCP_STATUS_SUCCESS);
	net_buf_add_be16(buf, TEST_UID_COUNTER);
	net_buf_add_be32(buf, TEST_SEARCH_ITEMS);
	TEST_SEND(bt_avrcp_tg_search(test_tg, evt.tid, buf), buf);

	expect_search(TEST_SEARCH_STR_2, &evt);
	send_status(bt_avrcp_tg_search, evt.tid, BT_AVRCP_STATUS_PARAMETER_CONTENT_ERROR);
}

ZTEST(avrcp_tg_browsing, test_006_search)
{
	test_tg_run_twice(search, true);
}

static void item_rsps(enum test_evt req,
		      int (*send)(struct bt_avrcp_tg *tg, uint8_t tid, uint8_t status),
		      const struct test_item_case *cases, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		const struct bt_avrcp_play_item_cmd *cmd;
		struct test_event evt;

		TEST_EXPECT(req, &evt);
		zassert_equal(evt.len, sizeof(*cmd), "Command length %u", evt.len);
		cmd = (const struct bt_avrcp_play_item_cmd *)evt.data;
		zassert_equal(cmd->scope, cases[i].scope, "Case %zu scope %u", i, cmd->scope);
		zassert_equal(sys_get_be64(cmd->uid), cases[i].uid, "Case %zu UID", i);
		zassert_equal(sys_be16_to_cpu(cmd->uid_counter), cases[i].uid_counter,
			      "Case %zu UID counter %u", i, sys_be16_to_cpu(cmd->uid_counter));

		TEST_OK(send(test_tg, evt.tid, cases[i].status));
	}
}

static void play_item(void)
{
	static const struct test_item_case cases[] = {TEST_PLAY_ITEM_CASES};

	item_rsps(EVT_TG_PLAY_ITEM, bt_avrcp_tg_play_item, cases, ARRAY_SIZE(cases));
}

ZTEST(avrcp_tg_browsing, test_007_play_item)
{
	test_tg_run_twice(play_item, false);
	test_tg_run_twice(play_item, true);
}

static void add_to_now_playing(void)
{
	static const struct test_item_case cases[] = {TEST_ADD_TO_NOW_PLAYING_CASES};

	item_rsps(EVT_TG_ADD_TO_NOW_PLAYING, bt_avrcp_tg_add_to_now_playing, cases,
		  ARRAY_SIZE(cases));
}

ZTEST(avrcp_tg_browsing, test_008_add_to_now_playing)
{
	test_tg_run_twice(add_to_now_playing, false);
	test_tg_run_twice(add_to_now_playing, true);
}

static void reject(enum test_evt req, uint8_t status)
{
	struct test_event evt;

	TEST_EXPECT(req, &evt);
	TEST_OK(bt_avrcp_tg_browsing_general_reject(test_tg, evt.tid, status));
}

static void general_reject(void)
{
	struct test_event evt;

	/* Extra: a reject needs a reject status. */
	TEST_EXPECT(EVT_TG_BROWSED_PLAYER, &evt);
	TEST_ERR(bt_avrcp_tg_browsing_general_reject(test_tg, evt.tid, BT_AVRCP_STATUS_SUCCESS),
		 -EINVAL);
	TEST_OK(bt_avrcp_tg_browsing_general_reject(test_tg, evt.tid,
						    BT_AVRCP_STATUS_INVALID_COMMAND));

	reject(EVT_TG_FOLDER_ITEMS, BT_AVRCP_STATUS_INVALID_PARAMETER);
	reject(EVT_TG_CHANGE_PATH, BT_AVRCP_STATUS_INVALID_COMMAND);
	reject(EVT_TG_ITEM_ATTRS, BT_AVRCP_STATUS_INVALID_COMMAND);
	reject(EVT_TG_SEARCH, BT_AVRCP_STATUS_INVALID_COMMAND);
	reject(EVT_TG_TOTAL_ITEMS, BT_AVRCP_STATUS_INVALID_COMMAND);
}

ZTEST(avrcp_tg_browsing, test_009_general_reject)
{
	test_tg_run_twice(general_reject, true);
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

ZTEST_SUITE(avrcp_tg_browsing, NULL, suite_setup, suite_before, NULL, suite_teardown);
