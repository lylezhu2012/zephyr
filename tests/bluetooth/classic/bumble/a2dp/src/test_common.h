/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_A2DP_COMMON_H_
#define TEST_A2DP_COMMON_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/classic/a2dp.h>
#include <zephyr/bluetooth/classic/a2dp_codec_sbc.h>
#include <zephyr/bluetooth/classic/avdtp.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#include "test_br.h"

/* Events reported by the A2DP and stream callbacks. */
enum test_evt {
	EVT_CONNECTED,
	EVT_DISCONNECTED,
	EVT_CONFIG_REQ,
	EVT_RECONFIG_REQ,
	EVT_CONFIG_RSP,
	EVT_ESTABLISH_REQ,
	EVT_ESTABLISH_RSP,
	EVT_RELEASE_REQ,
	EVT_RELEASE_RSP,
	EVT_START_REQ,
	EVT_START_RSP,
	EVT_SUSPEND_REQ,
	EVT_SUSPEND_RSP,
	EVT_ABORT_REQ,
	EVT_ABORT_RSP,
	EVT_GET_CONFIG_REQ,
	EVT_GET_CONFIG_RSP,
	EVT_DELAY_REPORT_REQ,
	EVT_DELAY_REPORT_RSP,
	EVT_CONFIGURED,
	EVT_ESTABLISHED,
	EVT_RELEASED,
	EVT_STARTED,
	EVT_SUSPENDED,
	EVT_RECV,
	EVT_DELAY_REPORT,

	EVT_COUNT,
};

struct test_event {
	enum test_evt type;
	/* A2DP connection or stream the event refers to. */
	void *obj;
	/* Error code of a request or response, or the value of the event. */
	int32_t value;
	int32_t value2;
};

/* Timeout of one A2DP procedure of the peer; the AVDTP response timeout of the stack is 6 s. */
#define TEST_A2DP_TIMEOUT K_SECONDS(10)

/* Values of the board-to-board tests. */
#define TEST_BAD_SEID              5U
#define TEST_MIN_BITPOOL_INVALID   1U
#define TEST_MIN_BITPOOL           20U
#define TEST_MIN_BITPOOL_RECONF_NS 13U
#define TEST_MIN_BITPOOL_RECONF    22U
#define TEST_DELAY_REPORT          1U
#define TEST_FRAG_MTU              4U
#define TEST_RANDOM_STEPS          50U

/* Delay report value of the stream tests, in 1/10 ms. */
#define TEST_DELAY_REPORT_STREAM 1500U

/* Media packets sent by a source in the stream tests. */
#define TEST_MEDIA_PKTS        10U
#define TEST_MEDIA_PAYLOAD_LEN 64U
#define TEST_MEDIA_TS_STEP     128U

/* Local endpoint type of the initiator in the signaling tests. */
#define TEST_INT_TSEP (IS_ENABLED(CONFIG_TEST_A2DP_INT_SINK) ? BT_AVDTP_SINK : BT_AVDTP_SOURCE)

static inline enum bt_avdtp_sep_type test_peer_tsep(enum bt_avdtp_sep_type tsep)
{
	return (tsep == BT_AVDTP_SINK) ? BT_AVDTP_SOURCE : BT_AVDTP_SINK;
}

/* A2DP connection of the current test. */
extern struct bt_a2dp *test_a2dp;

/* Stream of the current test, on both devices. */
extern struct bt_a2dp_stream test_stream;

/* Remote endpoint found by test_int_discover(). */
extern struct bt_a2dp_ep test_remote_ep;

/* Response error code of the acceptor to the next configuration request, 0 accepts it. */
extern uint8_t test_config_req_err;

/* Called from the next configuration request, before it is answered. */
extern void (*test_config_req_hook)(void);

/* Configuration of the last (re)configuration request seen by the acceptor. */
extern struct bt_a2dp_codec_ie test_req_cfg;
extern bool test_req_delay_report;

/* Configuration of the last get configuration response. */
extern struct bt_a2dp_codec_ie test_rsp_cfg;

/* Media packets received with an unexpected payload. */
extern atomic_t test_media_bad;

/*
 * The stream callbacks act as an application does (set by a test, cleared before each test):
 * configured sends a delay report on a sink with delay reporting, started and suspended send a
 * media packet on a source. The value of their event is the result of the call, 0 otherwise.
 */
extern bool test_act_in_cb;

void test_evt_reset(void);
void test_evt_post(enum test_evt type, void *obj, int32_t value, int32_t value2);

/*
 * Wait for the oldest unconsumed event of @p type and consume it. Events of other types stay
 * queued. Returns 0, -EAGAIN on timeout or -ENOTCONN if the ACL link is lost.
 */
int test_evt_wait(enum test_evt type, k_timeout_t timeout, struct test_event *evt);

/* Check that no event of @p type arrives within @p timeout. */
void test_evt_none(enum test_evt type, k_timeout_t timeout);

const char *test_evt_str(enum test_evt type);

#define TEST_EXPECT(type, timeout, evt)                                                            \
	do {                                                                                       \
		int _err = test_evt_wait((type), (timeout), (evt));                                \
                                                                                                   \
		zassert_equal(_err, 0, "No %s event (err %d)", test_evt_str(type), _err);          \
	} while (false)

/* Suite and test fixtures of both roles. */
void test_a2dp_suite_setup(enum test_br_role role);
void test_a2dp_suite_teardown(void);
void test_a2dp_before(void);

/*
 * Endpoints registered on both devices: an SBC sink with delay reporting, an SBC source without
 * and an SBC source with delay reporting.
 */
struct bt_a2dp_ep *test_local_ep(enum bt_avdtp_sep_type tsep, bool delay_report);

/* Wait for an event of the test stream; for responses, check the error code. */
void test_expect(enum test_evt type);
void test_expect_rsp(enum test_evt type, uint8_t err);

/* Initiator: A2DP connection, discovery and configuration. */
void test_int_connect(void);
void test_int_disconnect(void);

/*
 * Discover the endpoints of the peer and select the SBC endpoint of the other type than
 * @p local_tsep, with or without delay reporting. The AVDTP version given to the stack is read
 * from the A2DP SDP record of the peer if @p avdtp_version is 0.
 */
void test_int_discover(enum bt_avdtp_sep_type local_tsep, bool delay_report,
		       uint16_t avdtp_version);

/* As test_int_discover(), selecting the endpoint with the identifier @p seid. */
void test_int_discover_seid(enum bt_avdtp_sep_type local_tsep, uint8_t seid,
			    uint16_t avdtp_version);

/*
 * Configure the stream with SBC @p freq and @p min_bitpool, from the local endpoint of type
 * @p local_tsep to the discovered endpoint; @p err is the expected response error code.
 * test_int_config_send() only sends the request and returns the result of the API call;
 * test_stream_config_send() does so for @p stream from @p local_ep.
 */
int test_stream_config_send(struct bt_a2dp_stream *stream, struct bt_a2dp_ep *local_ep,
			    uint8_t freq, uint8_t min_bitpool, bool delay_report);
int test_int_config_send(enum bt_avdtp_sep_type local_tsep, uint8_t freq, uint8_t min_bitpool,
			 bool delay_report);
void test_int_config(enum bt_avdtp_sep_type local_tsep, uint8_t freq, uint8_t min_bitpool,
		     bool delay_report, uint8_t err);

/* Reconfigure the stream with SBC @p freq and @p min_bitpool; _send() only sends the request. */
int test_int_reconfig_send(uint8_t freq, uint8_t min_bitpool);
void test_int_reconfig(uint8_t freq, uint8_t min_bitpool, uint8_t err);

/* Board-to-board preparation: connect, discover and configure with min bitpool 20. */
void test_int_prepare(enum bt_avdtp_sep_type local_tsep);

/* Acceptor: wait for the A2DP connection. */
void test_acp_wait_connected(void);

/*
 * Wait for the release of the A2DP connection, and of the test stream if it is configured, then
 * drop the remaining events of the connection.
 */
void test_wait_disconnected(void);

/*
 * Replace the stream end point identifier of the peer used in the next commands by @p seid, or
 * restore it, as the modify_acp_seid command of the board-to-board tests.
 */
void test_set_acp_seid(uint8_t seid);
void test_restore_acp_seid(void);

/* Send a stream command and wait for its response with error code @p err. */
void test_stream_cmd(int (*cmd)(struct bt_a2dp_stream *stream), enum test_evt rsp, uint8_t err);
void test_delay_report_cmd(uint16_t value, uint8_t err);

/* Source: send @p count media packets; sink: receive them. */
void test_media_send(uint16_t count);
void test_media_expect(uint16_t count);

/* Send one media packet on @p stream and return the result of bt_a2dp_stream_send(). */
int test_media_send_on(struct bt_a2dp_stream *stream);

#define CHECK_EINVAL(call)                                                                         \
	do {                                                                                       \
		int _err = (call);                                                                 \
                                                                                                   \
		zassert_equal(_err, -EINVAL, "%s returned %d", #call, _err);                       \
	} while (false)

/* Check that every stream command on the test stream is refused, and that it has no MTU. */
void test_stream_cmds_refused(void);

/*
 * Set the transmit MTU of the AVDTP L2CAP channels of the ACL link to @p mtu, for the rest of the
 * link, as the modify_mtu command of the board-to-board tests (white-box, test_mtu.c).
 */
void test_avdtp_set_tx_mtu(uint16_t mtu);

#endif /* TEST_A2DP_COMMON_H_ */
