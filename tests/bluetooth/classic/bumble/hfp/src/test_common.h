/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_HFP_COMMON_H_
#define TEST_HFP_COMMON_H_

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/ztest.h>

#include "test_br.h"

/* Events reported by the AG and HF callbacks. */
enum test_evt {
	EVT_CONNECTED,
	EVT_DISCONNECTED,
	EVT_SCO_CONNECTED,
	EVT_SCO_DISCONNECTED,
	EVT_SERVICE,
	EVT_SIGNAL,
	EVT_ROAM,
	EVT_BATTERY,
	EVT_VGM,
	EVT_VGS,
	EVT_OPERATOR,
	EVT_OUTGOING,
	EVT_REMOTE_RINGING,
	EVT_INCOMING,
	EVT_INCOMING_HELD,
	EVT_RINGING,
	EVT_ACCEPT,
	EVT_REJECT,
	EVT_TERMINATE,
	EVT_HELD,
	EVT_RETRIEVE,
	EVT_RING_INDICATION,
	EVT_DIALING,
	EVT_CLIP,
	EVT_INBAND_RING,
	EVT_CODEC_IDS,
	EVT_CODEC_NEGOTIATE,
	EVT_AUDIO_CONNECT_REQ,
	EVT_ECNR_TURN_OFF,
	EVT_CALL_WAITING,
	EVT_EXPLICIT_CALL_TRANSFER,
	EVT_VOICE_RECOGNITION,
	EVT_VRE_STATE,
	EVT_TEXTUAL_REPRESENTATION,
	EVT_READY_TO_ACCEPT_AUDIO,
	EVT_REQUEST_PHONE_NUMBER,
	EVT_DTMF_CODE,
	EVT_SUBSCRIBER_NUMBER,
	EVT_HF_INDICATOR_VALUE,
	EVT_QUERY_CALL,
	EVT_NUMBER_CALL,
	EVT_MEMORY_DIAL,
	EVT_REDIAL,
	EVT_GET_ONGOING_CALL,

	EVT_COUNT,
};

#define TEST_EVT_STR_LEN 40

struct test_event {
	enum test_evt type;
	/* Call or SCO connection object the event refers to, if any. */
	void *obj;
	int32_t value;
	int32_t value2;
	char str[TEST_EVT_STR_LEN];
};

/* Timeout of one profile level step of the peer. */
#define TEST_HFP_TIMEOUT K_SECONDS(10)

/* Timeout for steps that include the AG call setup timers. */
#define TEST_HFP_CALL_TIMEOUT K_SECONDS(20)

/*
 * Time for the AT commands the HF sends right after the SLC is established (gains, CMEE, COPS,
 * CLIP, ...). An application command sent meanwhile is interleaved with them.
 */
#define TEST_SLC_SETTLE_TIME K_SECONDS(2)

/*
 * AT commands and result codes are sent from a work queue, so the L2CAP ECHO of test_br_sync()
 * can reach the peer first. A side that sends something the peer cannot observe through a
 * callback waits this long before test_br_sync().
 */
#define TEST_AT_DELIVERY_TIME K_MSEC(500)

/* Air_Mode values reported for SCO connections, see bt_conn_sco_info. */
#define TEST_AIR_MODE_CVSD        0x02
#define TEST_AIR_MODE_TRANSPARENT 0x03
#define TEST_AIR_MODE_ANY         0xFF

/* Values shared by both roles. */
#define TEST_AG_IND_SERVICE 1
#define TEST_AG_IND_SIGNAL  4
#define TEST_AG_IND_ROAM    0
#define TEST_AG_IND_BATTERY 3

#define TEST_OPERATOR_NAME "b2b_hfp_hf"

/* Phone numbers of the calls, as used by the board-to-board tests. */
#define TEST_NUMBER_1       "1234567890"
#define TEST_NUMBER_2       "1234567891"
#define TEST_NUMBER_PSI     "19283746"
#define TEST_NUMBER_WAITING "1234"
#define TEST_NUMBER_ONGOING "9876789"
#define TEST_NUMBER_LAST    "123456"

void test_evt_reset(void);
void test_evt_post(enum test_evt type, void *obj, int32_t value, int32_t value2, const char *str);

/*
 * Wait for the oldest unconsumed event of @p type and consume it. Events of other types stay
 * queued. Returns 0, -EAGAIN on timeout or -ENOTCONN if the ACL link is lost.
 */
int test_evt_wait(enum test_evt type, k_timeout_t timeout, struct test_event *evt);

/* Wait for an event of @p type with @p value; older events of that type are consumed. */
int test_evt_wait_value(enum test_evt type, int32_t value, k_timeout_t timeout,
			struct test_event *evt);

/* Wait for an event of @p type for @p obj; older events of that type are consumed. */
int test_evt_wait_obj(enum test_evt type, const void *obj, k_timeout_t timeout,
		      struct test_event *evt);

/* Return true if an unconsumed event of @p type is queued, without consuming it. */
bool test_evt_pending(enum test_evt type);

/* Check that no event of @p type arrives within @p timeout. */
void test_evt_none(enum test_evt type, k_timeout_t timeout);

const char *test_evt_str(enum test_evt type);

#define TEST_EXPECT(type, timeout, evt)                                                            \
	do {                                                                                       \
		int _err = test_evt_wait((type), (timeout), (evt));                                \
                                                                                                   \
		zassert_equal(_err, 0, "No %s event (err %d)", test_evt_str(type), _err);          \
	} while (false)

/* Discover the peer SDP record of service class @p svclass and return its RFCOMM channel. */
uint8_t test_sdp_get_rfcomm_channel(struct bt_conn *conn, uint16_t svclass);

/* Air mode of the SCO connection @p sco_conn. */
uint8_t test_sco_air_mode(struct bt_conn *sco_conn);

#endif /* TEST_HFP_COMMON_H_ */
