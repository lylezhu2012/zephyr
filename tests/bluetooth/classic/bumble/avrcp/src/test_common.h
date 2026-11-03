/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_AVRCP_COMMON_H_
#define TEST_AVRCP_COMMON_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/net_buf.h>
#include <zephyr/bluetooth/classic/avrcp.h>
#include <zephyr/ztest.h>

#include "test_br.h"

/*
 * Events reported by the AVRCP callbacks. Both devices register the CT and the TG callbacks, so
 * a connection or a disconnection is reported twice on each device (EVT_CT_* and EVT_TG_*).
 */
enum test_evt {
	EVT_CT_CONNECTED,
	EVT_CT_DISCONNECTED,
	EVT_CT_BROWSING_CONNECTED,
	EVT_CT_BROWSING_DISCONNECTED,
	EVT_TG_CONNECTED,
	EVT_TG_DISCONNECTED,
	EVT_TG_BROWSING_CONNECTED,
	EVT_TG_BROWSING_DISCONNECTED,

	/* Responses received by the CT. */
	EVT_CT_UNIT_INFO,
	EVT_CT_SUBUNIT_INFO,
	EVT_CT_PASSTHROUGH,
	EVT_CT_GET_CAPS,
	EVT_CT_NOTIFICATION,
	EVT_CT_NOTIFY_CHANGED,
	EVT_CT_LIST_ATTRS,
	EVT_CT_LIST_VALS,
	EVT_CT_GET_CURR_VAL,
	EVT_CT_SET_VAL,
	EVT_CT_ATTR_TEXT,
	EVT_CT_VAL_TEXT,
	EVT_CT_DISPLAYABLE_CHAR_SET,
	EVT_CT_BATT_STATUS,
	EVT_CT_ABSOLUTE_VOLUME,
	EVT_CT_ELEMENT_ATTRS,
	EVT_CT_PLAY_STATUS,
	EVT_CT_ADDRESSED_PLAYER,
	EVT_CT_PLAY_ITEM,
	EVT_CT_ADD_TO_NOW_PLAYING,
	EVT_CT_BROWSED_PLAYER,
	EVT_CT_FOLDER_ITEMS,
	EVT_CT_CHANGE_PATH,
	EVT_CT_ITEM_ATTRS,
	EVT_CT_TOTAL_ITEMS,
	EVT_CT_SEARCH,
	EVT_CT_GENERAL_REJECT,

	/* Commands received by the TG. */
	EVT_TG_UNIT_INFO,
	EVT_TG_SUBUNIT_INFO,
	EVT_TG_PASSTHROUGH,
	EVT_TG_GET_CAPS,
	EVT_TG_REGISTER_NOTIFICATION,
	EVT_TG_LIST_ATTRS,
	EVT_TG_LIST_VALS,
	EVT_TG_GET_CURR_VAL,
	EVT_TG_SET_VAL,
	EVT_TG_ATTR_TEXT,
	EVT_TG_VAL_TEXT,
	EVT_TG_DISPLAYABLE_CHAR_SET,
	EVT_TG_BATT_STATUS,
	EVT_TG_ABSOLUTE_VOLUME,
	EVT_TG_ELEMENT_ATTRS,
	EVT_TG_PLAY_STATUS,
	EVT_TG_ADDRESSED_PLAYER,
	EVT_TG_PLAY_ITEM,
	EVT_TG_ADD_TO_NOW_PLAYING,
	EVT_TG_BROWSED_PLAYER,
	EVT_TG_FOLDER_ITEMS,
	EVT_TG_CHANGE_PATH,
	EVT_TG_ITEM_ATTRS,
	EVT_TG_TOTAL_ITEMS,
	EVT_TG_SEARCH,

	EVT_COUNT,
};

/* Largest payload kept by an event: the fragmented GetElementAttributes response (575 bytes). */
#define TEST_EVT_DATA_LEN 640

struct test_event {
	enum test_evt type;
	uint8_t tid;
	/*
	 * Status of a CT response. For the browsing responses that only have a buffer, the first
	 * byte of the buffer.
	 */
	uint8_t status;
	/* Event ID of a notification or of a registration. */
	uint8_t event_id;
	/*
	 * Scalar parameters: unit/subunit type, passthrough result, capability ID, volume, battery
	 * status, scope, player ID, number of items, notification interval or event values.
	 */
	uint32_t value;
	uint32_t value2;
	/* The callback had a buffer or event data (false for a rejected CT response). */
	bool has_data;
	uint16_t len;
	/* Copy of the buffer of the callback, or of the variable event data. */
	uint8_t data[TEST_EVT_DATA_LEN];
};

/* Timeout of one AVRCP step of the peer; the CT of the stack has no response timer. */
#define TEST_AVRCP_TIMEOUT K_SECONDS(10)

/* AVRCP CT and TG objects of the current connection, on both devices. */
extern struct bt_avrcp_ct *test_ct;
extern struct bt_avrcp_tg *test_tg;

/*
 * Callbacks registered by both devices. The stack keeps the pointers to these structures, so a
 * test can clear an optional callback; test_avrcp_before() restores them.
 */
extern struct bt_avrcp_ct_cb test_ct_cb;
extern struct bt_avrcp_tg_cb test_tg_cb;

void test_evt_reset(void);

/*
 * Wait for the oldest unconsumed event of @p type and consume it. Events of other types stay
 * queued. Returns 0, -EAGAIN on timeout or -ENOTCONN if the ACL link is lost.
 */
int test_evt_wait(enum test_evt type, k_timeout_t timeout, struct test_event *evt);

/* Check that no event of @p type arrives within @p timeout. */
void test_evt_none(enum test_evt type, k_timeout_t timeout);

const char *test_evt_str(enum test_evt type);

#define TEST_EXPECT(type, evt)                                                                     \
	do {                                                                                       \
		int _err = test_evt_wait((type), TEST_AVRCP_TIMEOUT, (evt));                       \
                                                                                                   \
		zassert_equal(_err, 0, "No %s event (err %d)", test_evt_str(type), _err);          \
	} while (false)

#define TEST_OK(call)                                                                              \
	do {                                                                                       \
		int _err = (call);                                                                 \
                                                                                                   \
		zassert_equal(_err, 0, "%s returned %d", #call, _err);                             \
	} while (false)

#define TEST_ERR(call, expected)                                                                   \
	do {                                                                                       \
		int _err = (call);                                                                 \
                                                                                                   \
		zassert_equal(_err, (expected), "%s returned %d", #call, _err);                    \
	} while (false)

/* Suite and test fixtures of both roles. */
void test_avrcp_suite_setup(enum test_br_role role);
void test_avrcp_suite_teardown(void);
void test_avrcp_before(void);

/* Restore the callbacks of test_ct_cb and test_tg_cb cleared by a test. */
void test_avrcp_restore_cb(void);

/* Transaction label of the next CT command (0 to 15). */
uint8_t test_next_tid(void);

/*
 * Control channel: connect from this device or wait for the connection, both reported by the CT
 * and the TG callbacks of this device.
 */
void test_avrcp_connect(void);
void test_avrcp_wait_connected(void);

/* Disconnect the control channel (and the browsing channel first, if it is connected). */
void test_avrcp_disconnect(void);
void test_avrcp_wait_disconnected(void);

/* Browsing channel. */
void test_browsing_connect(void);
void test_browsing_wait_connected(void);
void test_browsing_disconnect(void);
void test_browsing_wait_disconnected(void);

/*
 * Run @p body twice on a new AVRCP connection of the CT (device 0): the CT releases the first
 * connection and the TG the second one, as the board-to-board avrcp_ct and avrcp_tg tests do.
 * With @p browsing the browsing channel is connected after the control channel. The CT
 * disconnects it before the control channel; the TG only disconnects the control channel and the
 * stack closes the browsing channel first.
 */
void test_ct_run_twice(void (*body)(void), bool browsing);
void test_tg_run_twice(void (*body)(void), bool browsing);

/*
 * Buffers for commands and responses: vendor dependent PDUs, and the other PDUs (passthrough
 * responses and browsing PDUs).
 */
struct net_buf *test_vendor_buf(void);
struct net_buf *test_pdu_buf(void);

/* Send @p buf with @p call; the caller still owns the buffer if the call fails. */
#define TEST_SEND(call, buf)                                                                       \
	do {                                                                                       \
		int _err = (call);                                                                 \
                                                                                                   \
		if (_err != 0) {                                                                   \
			net_buf_unref(buf);                                                        \
		}                                                                                  \
		zassert_equal(_err, 0, "%s returned %d", #call, _err);                             \
	} while (false)

/* Changed notification callback given to bt_avrcp_ct_register_notification(). */
void test_ct_notify_changed(struct bt_avrcp_ct *ct, uint8_t event_id,
			    struct bt_avrcp_event_data *data);

/*
 * Event data of the notification tests: the TG sends test_event_data() (interim or changed), the
 * CT checks the event with test_check_event_data().
 */
void test_event_data(uint8_t event_id, bool changed, struct bt_avrcp_event_data *data);
void test_check_event_data(const struct test_event *evt, bool changed);

/* Media attribute; the value is padded with zeros to @p len bytes if @p len is larger. */
struct test_media_attr {
	uint32_t id;
	const char *val;
	uint16_t len;
};

/* GetElementAttributes responses: the 7 attributes and the 3 large ones (575 bytes). */
extern const struct test_media_attr test_attrs_std[7];
extern const struct test_media_attr test_attrs_large[3];
/* Attribute of the GetItemAttributes response and of the media element item. */
extern const struct test_media_attr test_attr_item;

/* Add the attribute tuples (ID, character set, length, value) of @p attrs to @p buf. */
void test_add_media_attrs(struct net_buf *buf, const struct test_media_attr *attrs, size_t count);

/* Check @p count attribute tuples at @p data, which must take exactly @p len bytes. */
void test_check_media_attrs(const uint8_t *data, size_t len, const struct test_media_attr *attrs,
			    size_t count);

/* Text of a player application setting attribute and of a value. */
const char *test_attr_text(uint8_t attr_id);
const char *test_val_text(uint8_t val_id);

/* CT: wait for the response @p type to the command with @p tid. */
void test_ct_expect(enum test_evt type, uint8_t tid, struct test_event *evt);

/* CT: wait for a rejected response (no data) with @p status. */
void test_ct_expect_status(enum test_evt type, uint8_t tid, uint8_t status);

/* CT: wait for a browsing response that only contains @p status. */
void test_ct_expect_browsing_status(enum test_evt type, uint8_t tid, uint8_t status);

#endif /* TEST_AVRCP_COMMON_H_ */
