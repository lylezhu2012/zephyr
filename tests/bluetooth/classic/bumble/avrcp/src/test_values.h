/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_AVRCP_VALUES_H_
#define TEST_AVRCP_VALUES_H_

#include <stdint.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/net_buf.h>
#include <zephyr/bluetooth/classic/avrcp.h>

/*
 * Commands of the CT and responses of the TG, shared by the suites of both roles. The values come
 * from the board-to-board avrcp_ct and avrcp_tg tests; the cases marked "extra" have no
 * board-to-board counterpart. Each suite instantiates the tables it needs from these macros.
 */

/* GetCapabilities (EventsSupported) response, as the board-to-board TG. */
#define TEST_EVENTS_SUPPORTED                                                                      \
	BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED, BT_AVRCP_EVT_TRACK_CHANGED,                          \
		BT_AVRCP_EVT_TRACK_REACHED_END, BT_AVRCP_EVT_TRACK_REACHED_START,                  \
		BT_AVRCP_EVT_VOLUME_CHANGED

/* Capability ID that is neither CompanyID nor EventsSupported (extra). */
#define TEST_CAP_ID_INVALID 0x05U

/* Passthrough vendor unique operation (extra). */
#define TEST_OPID_VU 0x0010U

/* Notification values of the register_notification test. */
#define TEST_VOLUME_INTERIM 0x10U
#define TEST_VOLUME_CHANGED 0x20U
#define TEST_TRACK_INTERIM  0x111111ULL
#define TEST_TRACK_CHANGED  0x1ULL

/* Event IDs refused by the CT: one above the last event, and 0x21 as the board-to-board test. */
#define TEST_EVENT_ID_INVALID_1 (BT_AVRCP_EVT_VOLUME_CHANGED + 1U)
#define TEST_EVENT_ID_INVALID_2 0x21U
/* Event ID of the corrupted notification of the board-to-board TG. */
#define TEST_EVENT_ID_CORRUPTED 0xffU

/* Playback interval of the PLAYBACK_POS_CHANGED registration, in seconds (extra). */
#define TEST_PLAYBACK_INTERVAL 1U

/* Events of the notification_events test (extra), registered one after the other. */
#define TEST_NOTIFICATION_EVENTS                                                                   \
	BT_AVRCP_EVT_TRACK_REACHED_END, BT_AVRCP_EVT_TRACK_REACHED_START,                          \
		BT_AVRCP_EVT_PLAYBACK_POS_CHANGED, BT_AVRCP_EVT_BATT_STATUS_CHANGED,               \
		BT_AVRCP_EVT_SYSTEM_STATUS_CHANGED, BT_AVRCP_EVT_PLAYER_APP_SETTING_CHANGED,       \
		BT_AVRCP_EVT_NOW_PLAYING_CONTENT_CHANGED, BT_AVRCP_EVT_AVAILABLE_PLAYERS_CHANGED,  \
		BT_AVRCP_EVT_ADDRESSED_PLAYER_CHANGED, BT_AVRCP_EVT_UIDS_CHANGED

static inline uint32_t test_event_interval(uint8_t event_id)
{
	return (event_id == BT_AVRCP_EVT_PLAYBACK_POS_CHANGED) ? TEST_PLAYBACK_INTERVAL : 0U;
}

/* SetAbsoluteVolume. */
#define TEST_VOLUME_HALF 0x40U
#define TEST_VOLUME_MAX  BT_AVRCP_MAX_ABSOLUTE_VOLUME

/* GetPlayStatus response. */
#define TEST_SONG_LENGTH   180000U
#define TEST_SONG_POSITION 30000U

/* GetElementAttributes identifiers. */
#define TEST_ELEMENT_ID_LARGE 1U
#define TEST_ELEMENT_ID_STD   0U
#define TEST_ELEMENT_ID_LIST  2U

/* ListPlayerApplicationSettingAttributes responses, one per command: count, IDs. */
#define TEST_LIST_ATTRS_CASES                                                                      \
	{2, {1, 2}}, {3, {1, 2, 3}}, {1, {3}},                                                     \
	{                                                                                          \
		4, {1, 2, 3, 4}                                                                    \
	}

struct test_list_case {
	uint8_t num;
	uint8_t ids[4];
};

/* ListPlayerApplicationSettingValues: attribute of the command, values of the response. */
#define TEST_LIST_VALS_CASES                                                                       \
	{1, 2, {1, 2}}, {2, 3, {1, 2, 3}}, {3, 2, {1, 2}},                                         \
	{                                                                                          \
		4, 1, {1}                                                                          \
	}

struct test_list_vals_case {
	uint8_t attr_id;
	uint8_t num;
	uint8_t vals[4];
};

/* GetCurrentPlayerApplicationSettingValue: attributes of the command, pairs of the response. */
#define TEST_GET_CURR_CASES                                                                        \
	{1, {1}, 2, {{1, 1}, {2, 2}}}, {2, {1, 2}, 2, {{1, 2}, {2, 3}}},                           \
		{3, {1, 2, 3}, 3, {{1, 2}, {2, 2}, {3, 3}}},                                       \
	{                                                                                          \
		0, {0}, 0,                                                                         \
		{{0}}                                                                              \
	}

struct test_get_curr_case {
	uint8_t num_attrs;
	uint8_t attr_ids[4];
	uint8_t num_pairs;
	uint8_t pairs[4][2];
};

/* SetPlayerApplicationSettingValue: pairs of the command, status of the response. */
#define TEST_SET_VAL_CASES                                                                         \
	{1, {{1, 1}}, BT_AVRCP_STATUS_SUCCESS}, {2, {{1, 2}, {2, 2}}, BT_AVRCP_STATUS_SUCCESS},    \
		{3, {{1, 1}, {2, 3}, {3, 2}}, BT_AVRCP_STATUS_INVALID_PARAMETER},                  \
		{3, {{1, 1}, {2, 3}, {3, 2}}, BT_AVRCP_STATUS_NOT_A_DIRECTORY},                    \
	{                                                                                          \
		1, {{1, 1}}, BT_AVRCP_STATUS_NOT_IMPLEMENTED                                       \
	}

struct test_set_val_case {
	uint8_t num;
	uint8_t pairs[3][2];
	uint8_t status;
};

/* GetPlayerApplicationSettingAttributeText: attributes of the command, response status. */
#define TEST_ATTR_TEXT_CASES                                                                       \
	{1, {1}, BT_AVRCP_STATUS_SUCCESS}, {2, {1, 2}, BT_AVRCP_STATUS_SUCCESS},                   \
		{3, {1, 2, 3}, BT_AVRCP_STATUS_SUCCESS},                                           \
	{                                                                                          \
		1, {4}, BT_AVRCP_STATUS_INVALID_PARAMETER                                          \
	}

struct test_attr_text_case {
	uint8_t num;
	uint8_t attr_ids[4];
	uint8_t status;
};

/* GetPlayerApplicationSettingValueText: attribute and values of the command, response status. */
#define TEST_VAL_TEXT_CASES                                                                        \
	{1, 2, {1, 2}, BT_AVRCP_STATUS_SUCCESS}, {2, 3, {1, 2, 3}, BT_AVRCP_STATUS_SUCCESS},       \
		{3, 2, {1, 2}, BT_AVRCP_STATUS_SUCCESS},                                           \
		{4, 3, {1, 2, 3}, BT_AVRCP_STATUS_SUCCESS},                                        \
	{                                                                                          \
		1, 1, {1}, BT_AVRCP_STATUS_PARAMETER_CONTENT_ERROR                                 \
	}

struct test_val_text_case {
	uint8_t attr_id;
	uint8_t num;
	uint8_t val_ids[4];
	uint8_t status;
};

/* InformDisplayableCharacterSet: character sets of the command, response status. */
#define TEST_CHARSET_CASES                                                                         \
	{1, {0x006a}, BT_AVRCP_STATUS_SUCCESS}, {2, {0x006a, 0x0003}, BT_AVRCP_STATUS_SUCCESS},    \
		{3, {0x006a, 0x0003, 0x0004}, BT_AVRCP_STATUS_NOT_A_DIRECTORY},                    \
		{5, {0x006a, 0x0006, 0x000f, 0x006f, 0x0070}, BT_AVRCP_STATUS_INVALID_PARAMETER},  \
	{                                                                                          \
		7, {0x006a, 0x0003, 0x0004, 0x0005, 0x0006, 0x0008, 0x000f},                       \
			BT_AVRCP_STATUS_SUCCESS                                                    \
	}

struct test_charset_case {
	uint8_t num;
	uint16_t charsets[7];
	uint8_t status;
};

/* InformBatteryStatusOfCT: battery status of the command, response status. */
#define TEST_BATT_CASES                                                                            \
	{BT_AVRCP_BATTERY_STATUS_NORMAL, BT_AVRCP_STATUS_SUCCESS},                                 \
		{BT_AVRCP_BATTERY_STATUS_WARNING, BT_AVRCP_STATUS_INVALID_PARAMETER},              \
		{BT_AVRCP_BATTERY_STATUS_CRITICAL, BT_AVRCP_STATUS_PARAMETER_CONTENT_ERROR},       \
		{BT_AVRCP_BATTERY_STATUS_EXTERNAL, BT_AVRCP_STATUS_SUCCESS},                       \
	{                                                                                          \
		BT_AVRCP_BATTERY_STATUS_FULL, BT_AVRCP_STATUS_SUCCESS                              \
	}

/* Battery status refused by the CT. */
#define TEST_BATT_INVALID 0x05U

/* SetAddressedPlayer: player ID of the command, response status. */
#define TEST_ADDRESSED_CASES                                                                       \
	{1, BT_AVRCP_STATUS_SUCCESS}, {2, BT_AVRCP_STATUS_INVALID_PARAMETER},                      \
	{                                                                                          \
		999, BT_AVRCP_STATUS_INVALID_PLAYER_ID                                             \
	}

struct test_status_case {
	uint16_t param;
	uint8_t status;
};

/* SetBrowsedPlayer: player ID of the command. */
#define TEST_PLAYER_UTF8    1U
#define TEST_PLAYER_CHARSET 2U
#define TEST_PLAYER_REJECT  3U

/* SetBrowsedPlayer response values. */
#define TEST_BROWSED_UID_COUNTER 1U
#define TEST_BROWSED_FOLDER      "Music"
/* Folder name with another character set (bytes as the board-to-board test, charset 17). */
#define TEST_BROWSED_CHARSET     0x0011U
#define TEST_BROWSED_FOLDER_RAW  {0xe9, 0x89, 0xb1}

/* GetFolderItems commands. */
#define TEST_FOLDER_ITEMS_END 10U
#define TEST_PLAYER_NAME      "Demo"
#define TEST_FOLDER_NAME      "Folder"
#define TEST_SONG_NAME        "Song"
#define TEST_FOLDER_UID       0x10ULL
#define TEST_SONG_UID         0x11ULL

/* Item attribute of GetItemAttributes and of the media element item. */
#define TEST_ITEM_TITLE "Test Song"

/* ChangePath. */
#define TEST_CHANGE_PATH_ITEMS        10U
#define TEST_CHANGE_PATH_PARENT_ITEMS 5U
/* Folder UID of ChangePath and item UID of GetItemAttributes (all bytes 0x01, as the b2b CT). */
#define TEST_UID_01                   0x0101010101010101ULL
#define TEST_UID_COUNTER              1U

/* GetTotalNumberOfItems and Search responses. */
#define TEST_TOTAL_ITEMS  100U
#define TEST_SEARCH_ITEMS 3U

/* Scope outside the range of bt_avrcp_scope_t. */
#define TEST_SCOPE_INVALID 0x04U

/* Search. */
#define TEST_SEARCH_STR_1 "rock"
#define TEST_SEARCH_STR_2 "searchTestString"

/* PlayItem and AddToNowPlaying: scope, UID and UID counter of the command, response status. */
#define TEST_PLAY_ITEM_CASES                                                                       \
	{BT_AVRCP_SCOPE_VFS, 0x1, 1, BT_AVRCP_STATUS_SUCCESS},                                     \
		{BT_AVRCP_SCOPE_SEARCH, 0xff, 2, BT_AVRCP_STATUS_SUCCESS},                         \
		{BT_AVRCP_SCOPE_NOW_PLAYING, 0x10, 3, BT_AVRCP_STATUS_SUCCESS},                    \
		{BT_AVRCP_SCOPE_VFS, 0x5, 0, BT_AVRCP_STATUS_INVALID_PARAMETER},                   \
		{BT_AVRCP_SCOPE_SEARCH, 0x999, 0, BT_AVRCP_STATUS_DOES_NOT_EXIST},                 \
		{BT_AVRCP_SCOPE_MEDIA_PLAYER_LIST, 0x10, 0, BT_AVRCP_STATUS_MEDIA_IN_USE},         \
	{                                                                                          \
		TEST_SCOPE_INVALID, 0x1, 1, BT_AVRCP_STATUS_INVALID_SCOPE                          \
	}

#define TEST_ADD_TO_NOW_PLAYING_CASES                                                              \
	{BT_AVRCP_SCOPE_VFS, 0x1, 1, BT_AVRCP_STATUS_SUCCESS},                                     \
		{BT_AVRCP_SCOPE_NOW_PLAYING, 0xff, 2, BT_AVRCP_STATUS_SUCCESS},                    \
		{BT_AVRCP_SCOPE_SEARCH, 0x10, 0, BT_AVRCP_STATUS_INVALID_PARAMETER},               \
		{BT_AVRCP_SCOPE_VFS, 0x1, 0, BT_AVRCP_STATUS_SUCCESS},                             \
		{BT_AVRCP_SCOPE_VFS, 0x1, 0, BT_AVRCP_STATUS_NOT_IMPLEMENTED},                     \
	{                                                                                          \
		TEST_SCOPE_INVALID, 0x1, 1, BT_AVRCP_STATUS_INVALID_SCOPE                          \
	}

struct test_item_case {
	uint8_t scope;
	uint64_t uid;
	uint16_t uid_counter;
	uint8_t status;
};

#endif /* TEST_AVRCP_VALUES_H_ */
