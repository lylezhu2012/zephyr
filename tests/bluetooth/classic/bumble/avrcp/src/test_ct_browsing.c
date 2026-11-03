/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Browsing tests of the CT (device 0): the board-to-board avrcp_ct and avrcp_tg tests of the
 * browsing commands and of PlayItem and AddToNowPlaying, which run with the browsing channel
 * connected as in the avrcp_tg tests. Each test body runs twice on a new AVRCP connection with a
 * browsing channel, released by the CT and then by the TG.
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

static uint8_t browsed_player_send(uint16_t player_id)
{
	uint8_t tid = test_next_tid();

	TEST_OK(bt_avrcp_ct_set_browsed_player(test_ct, tid, player_id));

	return tid;
}

static void check_browsed_player(uint8_t tid, uint16_t charset, const uint8_t *name,
				 size_t name_len)
{
	const struct bt_avrcp_set_browsed_player_rsp *rsp;
	const struct bt_avrcp_folder_name *folder;
	struct test_event evt;

	test_ct_expect(EVT_CT_BROWSED_PLAYER, tid, &evt);
	zassert_equal(evt.len, sizeof(*rsp) + sizeof(*folder) + name_len, "Length %u", evt.len);
	rsp = (const struct bt_avrcp_set_browsed_player_rsp *)evt.data;
	zassert_equal(rsp->status, BT_AVRCP_STATUS_SUCCESS, "Status 0x%02x", rsp->status);
	zassert_equal(sys_be16_to_cpu(rsp->uid_counter), TEST_BROWSED_UID_COUNTER, "UID counter %u",
		      sys_be16_to_cpu(rsp->uid_counter));
	zassert_equal(sys_be32_to_cpu(rsp->num_items), TEST_TOTAL_ITEMS, "%u items",
		      sys_be32_to_cpu(rsp->num_items));
	zassert_equal(sys_be16_to_cpu(rsp->charset_id), charset, "Charset 0x%04x",
		      sys_be16_to_cpu(rsp->charset_id));
	zassert_equal(rsp->folder_depth, 1U, "Folder depth %u", rsp->folder_depth);

	folder = (const struct bt_avrcp_folder_name *)&evt.data[sizeof(*rsp)];
	zassert_equal(sys_be16_to_cpu(folder->folder_name_len), name_len, "Name length %u",
		      sys_be16_to_cpu(folder->folder_name_len));
	zassert_mem_equal(folder->folder_name, name, name_len, "Unexpected folder name");
}

static void set_browsed_player(void)
{
	static const uint8_t raw_name[] = TEST_BROWSED_FOLDER_RAW;

	check_browsed_player(browsed_player_send(TEST_PLAYER_UTF8), BT_AVRCP_CHARSET_UTF8,
			     (const uint8_t *)TEST_BROWSED_FOLDER, strlen(TEST_BROWSED_FOLDER));
	check_browsed_player(browsed_player_send(TEST_PLAYER_CHARSET), TEST_BROWSED_CHARSET,
			     raw_name, sizeof(raw_name));
	test_ct_expect_browsing_status(EVT_CT_BROWSED_PLAYER,
				       browsed_player_send(TEST_PLAYER_REJECT),
				       BT_AVRCP_STATUS_INVALID_PARAMETER);
}

/* b2b: avrcp_ct set_browsed_player, avrcp_tg set_browsed_player_rsp */
ZTEST(avrcp_ct_browsing, test_001_set_browsed_player)
{
	test_ct_run_twice(set_browsed_player, true);
}

static uint8_t folder_items_send(uint8_t scope, uint32_t end, const uint32_t *attr_ids, uint8_t num)
{
	struct bt_avrcp_get_folder_items_cmd *cmd;
	struct net_buf *buf = test_pdu_buf();
	uint8_t tid = test_next_tid();

	cmd = net_buf_add(buf, sizeof(*cmd));
	cmd->scope = scope;
	cmd->start_item = sys_cpu_to_be32(0U);
	cmd->end_item = sys_cpu_to_be32(end);
	cmd->attr_count = num;
	for (uint8_t i = 0U; i < num; i++) {
		net_buf_add_be32(buf, attr_ids[i]);
	}

	TEST_SEND(bt_avrcp_ct_get_folder_items(test_ct, tid, buf), buf);

	return tid;
}

/* Check the header of a GetFolderItems response and return the offset of the first item. */
static size_t check_folder_items_hdr(const struct test_event *evt, uint16_t num_items)
{
	const struct bt_avrcp_get_folder_items_rsp *rsp;

	zassert_true(evt->len >= sizeof(*rsp), "Length %u", evt->len);
	rsp = (const struct bt_avrcp_get_folder_items_rsp *)evt->data;
	zassert_equal(rsp->status, BT_AVRCP_STATUS_SUCCESS, "Status 0x%02x", rsp->status);
	zassert_equal(sys_be16_to_cpu(rsp->uid_counter), TEST_UID_COUNTER, "UID counter %u",
		      sys_be16_to_cpu(rsp->uid_counter));
	zassert_equal(sys_be16_to_cpu(rsp->num_items), num_items, "%u items",
		      sys_be16_to_cpu(rsp->num_items));

	return sizeof(*rsp);
}

/* Check the item header at @p off; return the offset of the item end. */
static size_t check_item_hdr(const struct test_event *evt, size_t off, uint8_t type,
			     size_t item_len)
{
	zassert_true((off + sizeof(struct bt_avrcp_item_hdr) + item_len) <= evt->len,
		     "Item at %zu truncated", off);
	zassert_equal(evt->data[off], type, "Item type %u", evt->data[off]);
	zassert_equal(sys_get_be16(&evt->data[off + 1U]), item_len, "Item length %u",
		      sys_get_be16(&evt->data[off + 1U]));

	return off + sizeof(struct bt_avrcp_item_hdr) + item_len;
}

static void check_media_player_item(const struct test_event *evt, size_t off)
{
	static const uint8_t no_features[16];
	const struct bt_avrcp_media_player_item *item;
	size_t name_len = strlen(TEST_PLAYER_NAME);
	size_t end;

	end = check_item_hdr(evt, off, BT_AVRCP_ITEM_TYPE_MEDIA_PLAYER,
			     sizeof(*item) - sizeof(struct bt_avrcp_item_hdr) + name_len);
	item = (const struct bt_avrcp_media_player_item *)&evt->data[off];
	zassert_equal(sys_be16_to_cpu(item->player_id), 1U, "Player %u",
		      sys_be16_to_cpu(item->player_id));
	zassert_equal(item->major_type, 1U, "Major type %u", item->major_type);
	zassert_equal(sys_be32_to_cpu(item->player_subtype), 0U, "Subtype %u",
		      sys_be32_to_cpu(item->player_subtype));
	zassert_equal(item->play_status, BT_AVRCP_PLAYBACK_STATUS_STOPPED, "Play status %u",
		      item->play_status);
	zassert_mem_equal(item->feature_bitmask, no_features, sizeof(no_features),
			  "Unexpected features");
	zassert_equal(sys_be16_to_cpu(item->charset_id), BT_AVRCP_CHARSET_UTF8, "Charset 0x%04x",
		      sys_be16_to_cpu(item->charset_id));
	zassert_equal(sys_be16_to_cpu(item->name_len), name_len, "Name length %u",
		      sys_be16_to_cpu(item->name_len));
	zassert_mem_equal(item->name, TEST_PLAYER_NAME, name_len, "Unexpected player name");
	zassert_equal(end, evt->len, "%zu bytes after the item", evt->len - end);
}

/* The folder and the media element item of the extra GetFolderItems response. */
static void check_folder_and_element(const struct test_event *evt, size_t off)
{
	const struct bt_avrcp_folder_item *folder;
	const struct bt_avrcp_media_element_item *element;
	size_t folder_name_len = strlen(TEST_FOLDER_NAME);
	size_t song_name_len = strlen(TEST_SONG_NAME);
	size_t attrs_len = sizeof(struct bt_avrcp_media_attr) + test_attr_item.len;
	size_t element_len;
	size_t end;
	size_t p;

	end = check_item_hdr(evt, off, BT_AVRCP_ITEM_TYPE_FOLDER,
			     sizeof(*folder) - sizeof(struct bt_avrcp_item_hdr) + folder_name_len);
	folder = (const struct bt_avrcp_folder_item *)&evt->data[off];
	zassert_equal(sys_get_be64(folder->uid), TEST_FOLDER_UID, "Folder UID");
	zassert_equal(folder->folder_type, BT_AVRCP_FOLDER_TYPE_TITLES, "Folder type %u",
		      folder->folder_type);
	zassert_equal(folder->playable, 1U, "Playable %u", folder->playable);
	zassert_equal(sys_be16_to_cpu(folder->charset_id), BT_AVRCP_CHARSET_UTF8,
		      "Folder charset 0x%04x", sys_be16_to_cpu(folder->charset_id));
	zassert_equal(sys_be16_to_cpu(folder->name_len), folder_name_len, "Folder name length %u",
		      sys_be16_to_cpu(folder->name_len));
	zassert_mem_equal(folder->name, TEST_FOLDER_NAME, folder_name_len, "Folder name");

	off = end;
	element_len = sizeof(*element) - sizeof(struct bt_avrcp_item_hdr) +
		      sizeof(struct media_element_item_name) + song_name_len + 1U + attrs_len;
	end = check_item_hdr(evt, off, BT_AVRCP_ITEM_TYPE_MEDIA_ELEMENT, element_len);
	element = (const struct bt_avrcp_media_element_item *)&evt->data[off];
	zassert_equal(sys_get_be64(element->uid), TEST_SONG_UID, "Element UID");
	zassert_equal(element->media_type, BT_AVRCP_MEDIA_TYPE_AUDIO, "Media type %u",
		      element->media_type);

	p = off + sizeof(*element);
	zassert_equal(sys_get_be16(&evt->data[p]), BT_AVRCP_CHARSET_UTF8, "Song charset 0x%04x",
		      sys_get_be16(&evt->data[p]));
	zassert_equal(sys_get_be16(&evt->data[p + 2U]), song_name_len, "Song name length %u",
		      sys_get_be16(&evt->data[p + 2U]));
	zassert_mem_equal(&evt->data[p + 4U], TEST_SONG_NAME, song_name_len, "Song name");
	p += sizeof(struct media_element_item_name) + song_name_len;
	zassert_equal(evt->data[p], 1U, "%u song attributes", evt->data[p]);
	test_check_media_attrs(&evt->data[p + 1U], attrs_len, &test_attr_item, 1U);

	zassert_equal(end, evt->len, "%zu bytes after the items", evt->len - end);
}

static void get_folder_items(void)
{
	static const uint32_t ids[] = {BT_AVRCP_MEDIA_ATTR_ID_TITLE};
	struct test_event evt;
	struct net_buf *buf;
	uint8_t tid;

	tid = folder_items_send(BT_AVRCP_SCOPE_VFS, TEST_FOLDER_ITEMS_END, NULL, 0U);
	test_ct_expect(EVT_CT_FOLDER_ITEMS, tid, &evt);
	check_media_player_item(&evt, check_folder_items_hdr(&evt, 1U));

	tid = folder_items_send(BT_AVRCP_SCOPE_VFS, TEST_FOLDER_ITEMS_END, NULL, 0U);
	test_ct_expect_browsing_status(EVT_CT_FOLDER_ITEMS, tid, BT_AVRCP_STATUS_INTERNAL_ERROR);

	/* Extra: a folder and a media element item with one attribute. */
	tid = folder_items_send(BT_AVRCP_SCOPE_NOW_PLAYING, 1U, ids, ARRAY_SIZE(ids));
	test_ct_expect(EVT_CT_FOLDER_ITEMS, tid, &evt);
	check_folder_and_element(&evt, check_folder_items_hdr(&evt, 2U));

	/* An empty command and no buffer are refused by the CT. */
	buf = test_pdu_buf();
	TEST_ERR(bt_avrcp_ct_get_folder_items(test_ct, test_next_tid(), buf), -EINVAL);
	net_buf_unref(buf);
	TEST_ERR(bt_avrcp_ct_get_folder_items(test_ct, test_next_tid(), NULL), -EINVAL);
}

/* b2b: avrcp_ct get_folder_items, avrcp_tg get_folder_items_rsp */
ZTEST(avrcp_ct_browsing, test_002_get_folder_items)
{
	test_ct_run_twice(get_folder_items, true);
}

static uint8_t change_path_send(uint8_t direction, uint64_t uid)
{
	struct bt_avrcp_change_path_cmd *cmd;
	struct net_buf *buf = test_pdu_buf();
	uint8_t tid = test_next_tid();

	cmd = net_buf_add(buf, sizeof(*cmd));
	cmd->uid_counter = sys_cpu_to_be16(TEST_UID_COUNTER);
	cmd->direction = direction;
	sys_put_be64(uid, cmd->folder_uid);

	TEST_SEND(bt_avrcp_ct_change_path(test_ct, tid, buf), buf);

	return tid;
}

static void change_path_cmd(uint8_t direction, uint64_t uid, uint8_t status, uint32_t num_items)
{
	struct test_event evt;
	uint8_t tid = change_path_send(direction, uid);

	test_ct_expect(EVT_CT_CHANGE_PATH, tid, &evt);
	zassert_equal(evt.status, status, "Status 0x%02x", evt.status);
	zassert_equal(evt.value, num_items, "%u items", evt.value);
}

static void change_path(void)
{
	change_path_cmd(BT_AVRCP_CHANGE_PATH_CHILD, TEST_UID_01, BT_AVRCP_STATUS_SUCCESS,
			TEST_CHANGE_PATH_ITEMS);
	change_path_cmd(BT_AVRCP_CHANGE_PATH_CHILD, TEST_UID_01, BT_AVRCP_STATUS_INVALID_PARAMETER,
			0U);
	/* Extra: back to the parent folder. */
	change_path_cmd(BT_AVRCP_CHANGE_PATH_PARENT, 0U, BT_AVRCP_STATUS_SUCCESS,
			TEST_CHANGE_PATH_PARENT_ITEMS);
}

/* b2b: avrcp_ct change_path, avrcp_tg change_path_rsp */
ZTEST(avrcp_ct_browsing, test_003_change_path)
{
	test_ct_run_twice(change_path, true);
}

static uint8_t item_attrs_send(uint8_t scope)
{
	struct bt_avrcp_get_item_attrs_cmd *cmd;
	struct net_buf *buf = test_pdu_buf();
	uint8_t tid = test_next_tid();

	cmd = net_buf_add(buf, sizeof(*cmd));
	cmd->scope = scope;
	sys_put_be64(TEST_UID_01, cmd->uid);
	cmd->uid_counter = sys_cpu_to_be16(TEST_UID_COUNTER);
	cmd->num_attrs = 0U;

	TEST_SEND(bt_avrcp_ct_get_item_attrs(test_ct, tid, buf), buf);

	return tid;
}

static void get_item_attrs(void)
{
	struct test_event evt;
	uint8_t tid;

	tid = item_attrs_send(BT_AVRCP_SCOPE_VFS);
	test_ct_expect(EVT_CT_ITEM_ATTRS, tid, &evt);
	zassert_true(evt.len >= 2U, "Length %u", evt.len);
	zassert_equal(evt.status, BT_AVRCP_STATUS_SUCCESS, "Status 0x%02x", evt.status);
	zassert_equal(evt.data[1], 1U, "%u attributes", evt.data[1]);
	test_check_media_attrs(&evt.data[2], evt.len - 2U, &test_attr_item, 1U);

	tid = item_attrs_send(BT_AVRCP_SCOPE_VFS);
	test_ct_expect_browsing_status(EVT_CT_ITEM_ATTRS, tid, BT_AVRCP_STATUS_INVALID_PARAMETER);

	/* The stack of the CT does not check the scope; the TG rejects it. */
	tid = item_attrs_send(TEST_SCOPE_INVALID);
	test_ct_expect_browsing_status(EVT_CT_ITEM_ATTRS, tid, BT_AVRCP_STATUS_INVALID_SCOPE);
}

/* b2b: avrcp_ct get_item_attrs, avrcp_tg get_item_attributes_rsp */
ZTEST(avrcp_ct_browsing, test_004_get_item_attrs)
{
	test_ct_run_twice(get_item_attrs, true);
}

static uint8_t total_items_send(uint8_t scope)
{
	uint8_t tid = test_next_tid();

	TEST_OK(bt_avrcp_ct_get_total_number_of_items(test_ct, tid, scope));

	return tid;
}

static void get_total_number_of_items(void)
{
	const struct bt_avrcp_get_total_number_of_items_rsp *rsp;
	struct test_event evt;

	for (uint8_t scope = BT_AVRCP_SCOPE_MEDIA_PLAYER_LIST; scope <= BT_AVRCP_SCOPE_NOW_PLAYING;
	     scope++) {
		test_ct_expect(EVT_CT_TOTAL_ITEMS, total_items_send(scope), &evt);
		zassert_equal(evt.len, sizeof(*rsp), "Length %u", evt.len);
		rsp = (const struct bt_avrcp_get_total_number_of_items_rsp *)evt.data;
		zassert_equal(rsp->status, BT_AVRCP_STATUS_SUCCESS, "Status 0x%02x", rsp->status);
		zassert_equal(sys_be16_to_cpu(rsp->uid_counter), TEST_UID_COUNTER, "UID counter %u",
			      sys_be16_to_cpu(rsp->uid_counter));
		zassert_equal(sys_be32_to_cpu(rsp->num_items), TEST_TOTAL_ITEMS, "%u items",
			      sys_be32_to_cpu(rsp->num_items));
	}

	test_ct_expect_browsing_status(EVT_CT_TOTAL_ITEMS, total_items_send(BT_AVRCP_SCOPE_VFS),
				       BT_AVRCP_STATUS_PARAMETER_CONTENT_ERROR);

	TEST_ERR(
		bt_avrcp_ct_get_total_number_of_items(test_ct, test_next_tid(), TEST_SCOPE_INVALID),
		-EINVAL);
	TEST_ERR(bt_avrcp_ct_get_total_number_of_items(test_ct, test_next_tid(), 0xffU), -EINVAL);
}

/* b2b: avrcp_ct get_total_number_of_items, avrcp_tg get_total_num_of_items_rsp */
ZTEST(avrcp_ct_browsing, test_005_get_total_number_of_items)
{
	test_ct_run_twice(get_total_number_of_items, true);
}

static uint8_t search_send(const char *str)
{
	struct net_buf *buf = test_pdu_buf();
	uint8_t tid = test_next_tid();

	net_buf_add_be16(buf, BT_AVRCP_CHARSET_UTF8);
	net_buf_add_be16(buf, strlen(str));
	net_buf_add_mem(buf, str, strlen(str));

	TEST_SEND(bt_avrcp_ct_search(test_ct, tid, buf), buf);

	return tid;
}

static void search(void)
{
	const struct bt_avrcp_search_rsp *rsp;
	struct test_event evt;

	test_ct_expect(EVT_CT_SEARCH, search_send(TEST_SEARCH_STR_1), &evt);
	zassert_equal(evt.len, sizeof(*rsp), "Length %u", evt.len);
	rsp = (const struct bt_avrcp_search_rsp *)evt.data;
	zassert_equal(rsp->status, BT_AVRCP_STATUS_SUCCESS, "Status 0x%02x", rsp->status);
	zassert_equal(sys_be16_to_cpu(rsp->uid_counter), TEST_UID_COUNTER, "UID counter %u",
		      sys_be16_to_cpu(rsp->uid_counter));
	zassert_equal(sys_be32_to_cpu(rsp->num_items), TEST_SEARCH_ITEMS, "%u items",
		      sys_be32_to_cpu(rsp->num_items));

	test_ct_expect_browsing_status(EVT_CT_SEARCH, search_send(TEST_SEARCH_STR_2),
				       BT_AVRCP_STATUS_PARAMETER_CONTENT_ERROR);
}

/* b2b: avrcp_ct search, avrcp_tg search_rsp */
ZTEST(avrcp_ct_browsing, test_006_search)
{
	test_ct_run_twice(search, true);
}

/* PlayItem and AddToNowPlaying have the same command format. */
static void item_cmds(int (*send)(struct bt_avrcp_ct *ct, uint8_t tid, struct net_buf *buf),
		      enum test_evt rsp, const struct test_item_case *cases, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		struct bt_avrcp_play_item_cmd *cmd;
		struct net_buf *buf = test_vendor_buf();
		uint8_t tid = test_next_tid();

		cmd = net_buf_add(buf, sizeof(*cmd));
		cmd->scope = cases[i].scope;
		sys_put_be64(cases[i].uid, cmd->uid);
		cmd->uid_counter = sys_cpu_to_be16(cases[i].uid_counter);

		TEST_SEND(send(test_ct, tid, buf), buf);
		test_ct_expect_status(rsp, tid, cases[i].status);
	}
}

static void play_item(void)
{
	static const struct test_item_case cases[] = {TEST_PLAY_ITEM_CASES};

	item_cmds(bt_avrcp_ct_play_item, EVT_CT_PLAY_ITEM, cases, ARRAY_SIZE(cases));
}

/* b2b: avrcp_ct play_item, avrcp_tg play_item_rsp */
ZTEST(avrcp_ct_browsing, test_007_play_item)
{
	test_ct_run_twice(play_item, false);
	test_ct_run_twice(play_item, true);
}

static void add_to_now_playing(void)
{
	static const struct test_item_case cases[] = {TEST_ADD_TO_NOW_PLAYING_CASES};
	struct net_buf *buf;

	item_cmds(bt_avrcp_ct_add_to_now_playing, EVT_CT_ADD_TO_NOW_PLAYING, cases,
		  ARRAY_SIZE(cases));

	/* No buffer and an empty command are refused by the CT. */
	TEST_ERR(bt_avrcp_ct_add_to_now_playing(test_ct, test_next_tid(), NULL), -EINVAL);
	buf = test_vendor_buf();
	TEST_ERR(bt_avrcp_ct_add_to_now_playing(test_ct, test_next_tid(), buf), -EINVAL);
	net_buf_unref(buf);
}

/* b2b: avrcp_ct add_to_now_playing, avrcp_tg add_to_now_playing_rsp */
ZTEST(avrcp_ct_browsing, test_008_add_to_now_playing)
{
	test_ct_run_twice(add_to_now_playing, false);
	test_ct_run_twice(add_to_now_playing, true);
}

static void expect_reject(uint8_t tid, uint8_t status)
{
	struct test_event evt;

	test_ct_expect(EVT_CT_GENERAL_REJECT, tid, &evt);
	zassert_equal(evt.status, status, "Reject status 0x%02x", evt.status);
}

static void general_reject(void)
{
	expect_reject(browsed_player_send(TEST_PLAYER_UTF8), BT_AVRCP_STATUS_INVALID_COMMAND);
	expect_reject(folder_items_send(BT_AVRCP_SCOPE_VFS, TEST_FOLDER_ITEMS_END, NULL, 0U),
		      BT_AVRCP_STATUS_INVALID_PARAMETER);
	expect_reject(change_path_send(BT_AVRCP_CHANGE_PATH_CHILD, TEST_UID_01),
		      BT_AVRCP_STATUS_INVALID_COMMAND);
	expect_reject(item_attrs_send(BT_AVRCP_SCOPE_VFS), BT_AVRCP_STATUS_INVALID_COMMAND);
	expect_reject(search_send(TEST_SEARCH_STR_1), BT_AVRCP_STATUS_INVALID_COMMAND);
	expect_reject(total_items_send(BT_AVRCP_SCOPE_VFS), BT_AVRCP_STATUS_INVALID_COMMAND);
}

/* b2b: avrcp_tg send_browsing_general_reject */
ZTEST(avrcp_ct_browsing, test_009_general_reject)
{
	test_ct_run_twice(general_reject, true);
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

ZTEST_SUITE(avrcp_ct_browsing, NULL, suite_setup, suite_before, NULL, suite_teardown);
