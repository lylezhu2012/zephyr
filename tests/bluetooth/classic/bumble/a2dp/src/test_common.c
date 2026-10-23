/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/classic/a2dp.h>
#include <zephyr/bluetooth/classic/a2dp_codec_sbc.h>
#include <zephyr/bluetooth/classic/avdtp.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"

LOG_MODULE_REGISTER(test_a2dp_common, CONFIG_TEST_CLASSIC_A2DP_SIM_LOG_LEVEL);

#define EVT_SLOTS 128

static struct test_event evt_slots[EVT_SLOTS];
static bool evt_used[EVT_SLOTS];
static uint32_t evt_seq[EVT_SLOTS];
static uint32_t evt_next_seq;

static K_MUTEX_DEFINE(evt_lock);
static K_CONDVAR_DEFINE(evt_cond);

static const char *const evt_names[] = {
	[EVT_CONNECTED] = "connected",
	[EVT_DISCONNECTED] = "disconnected",
	[EVT_CONFIG_REQ] = "config_req",
	[EVT_RECONFIG_REQ] = "reconfig_req",
	[EVT_CONFIG_RSP] = "config_rsp",
	[EVT_ESTABLISH_REQ] = "establish_req",
	[EVT_ESTABLISH_RSP] = "establish_rsp",
	[EVT_RELEASE_REQ] = "release_req",
	[EVT_RELEASE_RSP] = "release_rsp",
	[EVT_START_REQ] = "start_req",
	[EVT_START_RSP] = "start_rsp",
	[EVT_SUSPEND_REQ] = "suspend_req",
	[EVT_SUSPEND_RSP] = "suspend_rsp",
	[EVT_ABORT_REQ] = "abort_req",
	[EVT_ABORT_RSP] = "abort_rsp",
	[EVT_GET_CONFIG_REQ] = "get_config_req",
	[EVT_GET_CONFIG_RSP] = "get_config_rsp",
	[EVT_DELAY_REPORT_REQ] = "delay_report_req",
	[EVT_DELAY_REPORT_RSP] = "delay_report_rsp",
	[EVT_CONFIGURED] = "configured",
	[EVT_ESTABLISHED] = "established",
	[EVT_RELEASED] = "released",
	[EVT_STARTED] = "started",
	[EVT_SUSPENDED] = "suspended",
	[EVT_RECV] = "recv",
	[EVT_DELAY_REPORT] = "delay_report",
};

BUILD_ASSERT(ARRAY_SIZE(evt_names) == EVT_COUNT);
BUILD_ASSERT(EVT_COUNT <= 32, "Event types do not fit in a 32-bit mask");

const char *test_evt_str(enum test_evt type)
{
	return ((type < EVT_COUNT) && (evt_names[type] != NULL)) ? evt_names[type] : "?";
}

void test_evt_reset(void)
{
	k_mutex_lock(&evt_lock, K_FOREVER);
	memset(evt_used, 0, sizeof(evt_used));
	k_mutex_unlock(&evt_lock);
}

void test_evt_post(enum test_evt type, void *obj, int32_t value, int32_t value2)
{
	size_t slot = EVT_SLOTS;

	LOG_DBG("event %s obj %p value %d %d", test_evt_str(type), obj, value, value2);

	k_mutex_lock(&evt_lock, K_FOREVER);

	for (size_t i = 0; i < EVT_SLOTS; i++) {
		if (!evt_used[i]) {
			slot = i;
			break;
		}
	}

	if (slot == EVT_SLOTS) {
		k_mutex_unlock(&evt_lock);
		LOG_ERR("Event store full, %s dropped", test_evt_str(type));
		return;
	}

	evt_slots[slot].type = type;
	evt_slots[slot].obj = obj;
	evt_slots[slot].value = value;
	evt_slots[slot].value2 = value2;
	evt_seq[slot] = evt_next_seq++;
	evt_used[slot] = true;

	k_condvar_broadcast(&evt_cond);
	k_mutex_unlock(&evt_lock);
}

/*
 * Index of the oldest unconsumed event of a type in the mask @p types, or -1. Called with
 * evt_lock held.
 */
static int evt_find(uint32_t types)
{
	int found = -1;

	for (int i = 0; i < EVT_SLOTS; i++) {
		if (evt_used[i] && ((BIT(evt_slots[i].type) & types) != 0U) &&
		    ((found < 0) || ((int32_t)(evt_seq[i] - evt_seq[found]) < 0))) {
			found = i;
		}
	}

	return found;
}

/* test_evt_wait() for the oldest event of any type in the mask @p types. */
static int evt_wait(uint32_t types, k_timeout_t timeout, struct test_event *evt)
{
	k_timepoint_t end = sys_timepoint_calc(timeout);
	int idx;

	k_mutex_lock(&evt_lock, K_FOREVER);

	while ((idx = evt_find(types)) < 0) {
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

int test_evt_wait(enum test_evt type, k_timeout_t timeout, struct test_event *evt)
{
	return evt_wait(BIT(type), timeout, evt);
}

void test_evt_none(enum test_evt type, k_timeout_t timeout)
{
	struct test_event evt;
	int err;

	err = test_evt_wait(type, timeout, &evt);
	zassert_not_equal(err, 0, "Unexpected %s event (value %d)", test_evt_str(type), evt.value);
}

/* Endpoints, registered on both devices in this order (SEID 1, 2 and 3). */
BT_A2DP_SBC_SINK_EP_DEFAULT(sink_ep);
BT_A2DP_SBC_SOURCE_EP_DEFAULT(source_ep);
BT_A2DP_SBC_SOURCE_EP(source_dr_ep, A2DP_SBC_SAMP_FREQ_44100 | A2DP_SBC_SAMP_FREQ_48000,
		      A2DP_SBC_CH_MODE_MONO | A2DP_SBC_CH_MODE_STEREO | A2DP_SBC_CH_MODE_JOINT,
		      A2DP_SBC_BLK_LEN_16, A2DP_SBC_SUBBAND_8, A2DP_SBC_ALLOC_MTHD_LOUDNESS, 18U,
		      35U, true);

/* Maximum bitpool of the configurations, the maximum of the endpoint capabilities. */
#define TEST_MAX_BITPOOL 35U

/* A2DP SDP records of the board-to-board tests, AVDTP and A2DP version 1.3. */
static struct bt_sdp_attribute a2dp_sink_attrs[] = {
	BT_SDP_NEW_SERVICE,
	BT_SDP_LIST(
		BT_SDP_ATTR_SVCLASS_ID_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
			BT_SDP_ARRAY_16(BT_SDP_AUDIO_SINK_SVCLASS)
		},
		)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PROTO_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 16),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_PROTO_L2CAP)
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				BT_SDP_ARRAY_16(BT_UUID_AVDTP_VAL)
			},
			)
		},
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_UUID_AVDTP_VAL)
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				BT_SDP_ARRAY_16(AVDTP_VERSION_1_3)
			},
			)
		},
		)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PROFILE_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 8),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_ADVANCED_AUDIO_SVCLASS)
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				BT_SDP_ARRAY_16(0x0103U)
			},
			)
		},
		)
	),
	BT_SDP_SERVICE_NAME("A2DPSink"),
	BT_SDP_SUPPORTED_FEATURES(0x0001U),
};

static struct bt_sdp_record a2dp_sink_rec = BT_SDP_RECORD(a2dp_sink_attrs);

static struct bt_sdp_attribute a2dp_source_attrs[] = {
	BT_SDP_NEW_SERVICE,
	BT_SDP_LIST(
		BT_SDP_ATTR_SVCLASS_ID_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
			BT_SDP_ARRAY_16(BT_SDP_AUDIO_SOURCE_SVCLASS)
		},
		)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PROTO_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 16),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_PROTO_L2CAP)
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				BT_SDP_ARRAY_16(BT_UUID_AVDTP_VAL)
			},
			)
		},
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_UUID_AVDTP_VAL)
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				BT_SDP_ARRAY_16(AVDTP_VERSION_1_3)
			},
			)
		},
		)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PROFILE_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 8),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_ADVANCED_AUDIO_SVCLASS)
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				BT_SDP_ARRAY_16(0x0103U)
			},
			)
		},
		)
	),
	BT_SDP_SERVICE_NAME("A2DPSource"),
	BT_SDP_SUPPORTED_FEATURES(0x0001U),
};

static struct bt_sdp_record a2dp_source_rec = BT_SDP_RECORD(a2dp_source_attrs);

struct bt_a2dp *test_a2dp;
struct bt_a2dp_stream test_stream;

static struct bt_a2dp_codec_ie remote_ep_cap;
struct bt_a2dp_ep test_remote_ep = {
	.codec_cap = &remote_ep_cap,
};

uint8_t test_config_req_err;
struct bt_a2dp_codec_ie test_req_cfg;
bool test_req_delay_report;
struct bt_a2dp_codec_ie test_rsp_cfg;
atomic_t test_media_bad;

/* Sequence number of the next media packet sent, and expected, in the current test. */
static uint16_t media_tx_seq;
static uint16_t media_rx_seq;

/*
 * The test stream is configured: from its configured callback to its released callback. Cleared
 * after the released event is posted, so a reader that sees it cleared finds the event queued.
 */
static atomic_t stream_in_use;

static struct bt_a2dp_stream_ops stream_ops;

struct bt_a2dp_ep *test_local_ep(enum bt_avdtp_sep_type tsep, bool delay_report)
{
	if (tsep == BT_AVDTP_SINK) {
		return &sink_ep;
	}

	return delay_report ? &source_dr_ep : &source_ep;
}

static void a2dp_connected(struct bt_a2dp *a2dp, int err)
{
	if (err == 0) {
		test_a2dp = a2dp;
	}

	test_evt_post(EVT_CONNECTED, a2dp, err, 0);
}

static void a2dp_disconnected(struct bt_a2dp *a2dp)
{
	test_evt_post(EVT_DISCONNECTED, a2dp, 0, 0);
}

static int a2dp_config_req(struct bt_a2dp *a2dp, struct bt_a2dp_ep *ep,
			   struct bt_a2dp_codec_cfg *codec_cfg, struct bt_a2dp_stream **stream,
			   uint8_t *rsp_err_code)
{
	uint8_t err = test_config_req_err;

	test_req_cfg = *codec_cfg->codec_config;
	test_req_delay_report = codec_cfg->delay_report;

	/* The error code applies to one request only. */
	test_config_req_err = 0;

	*rsp_err_code = err;
	if (err == 0) {
		bt_a2dp_stream_cb_register(&test_stream, &stream_ops);
		*stream = &test_stream;
	}

	test_evt_post(EVT_CONFIG_REQ, &test_stream, err, 0);

	return (err == 0) ? 0 : -EINVAL;
}

static int a2dp_reconfig_req(struct bt_a2dp_stream *stream, struct bt_a2dp_codec_cfg *codec_cfg,
			     uint8_t *rsp_err_code)
{
	test_req_cfg = *codec_cfg->codec_config;
	test_req_delay_report = codec_cfg->delay_report;
	*rsp_err_code = 0;
	test_evt_post(EVT_RECONFIG_REQ, stream, 0, 0);

	return 0;
}

static void a2dp_config_rsp(struct bt_a2dp_stream *stream, uint8_t rsp_err_code)
{
	test_evt_post(EVT_CONFIG_RSP, stream, rsp_err_code, 0);
}

/* The acceptor accepts every stream control request. */
static int accept_req(enum test_evt type, struct bt_a2dp_stream *stream, uint8_t *rsp_err_code)
{
	*rsp_err_code = 0;
	test_evt_post(type, stream, 0, 0);

	return 0;
}

static int a2dp_establish_req(struct bt_a2dp_stream *stream, uint8_t *rsp_err_code)
{
	return accept_req(EVT_ESTABLISH_REQ, stream, rsp_err_code);
}

static void a2dp_establish_rsp(struct bt_a2dp_stream *stream, uint8_t rsp_err_code)
{
	test_evt_post(EVT_ESTABLISH_RSP, stream, rsp_err_code, 0);
}

static int a2dp_release_req(struct bt_a2dp_stream *stream, uint8_t *rsp_err_code)
{
	return accept_req(EVT_RELEASE_REQ, stream, rsp_err_code);
}

static void a2dp_release_rsp(struct bt_a2dp_stream *stream, uint8_t rsp_err_code)
{
	test_evt_post(EVT_RELEASE_RSP, stream, rsp_err_code, 0);
}

static int a2dp_start_req(struct bt_a2dp_stream *stream, uint8_t *rsp_err_code)
{
	return accept_req(EVT_START_REQ, stream, rsp_err_code);
}

static void a2dp_start_rsp(struct bt_a2dp_stream *stream, uint8_t rsp_err_code)
{
	test_evt_post(EVT_START_RSP, stream, rsp_err_code, 0);
}

static int a2dp_suspend_req(struct bt_a2dp_stream *stream, uint8_t *rsp_err_code)
{
	return accept_req(EVT_SUSPEND_REQ, stream, rsp_err_code);
}

static void a2dp_suspend_rsp(struct bt_a2dp_stream *stream, uint8_t rsp_err_code)
{
	test_evt_post(EVT_SUSPEND_RSP, stream, rsp_err_code, 0);
}

static int a2dp_abort_req(struct bt_a2dp_stream *stream, uint8_t *rsp_err_code)
{
	return accept_req(EVT_ABORT_REQ, stream, rsp_err_code);
}

static void a2dp_abort_rsp(struct bt_a2dp_stream *stream, uint8_t rsp_err_code)
{
	test_evt_post(EVT_ABORT_RSP, stream, rsp_err_code, 0);
}

static int a2dp_get_config_req(struct bt_a2dp_stream *stream, uint8_t *rsp_err_code)
{
	return accept_req(EVT_GET_CONFIG_REQ, stream, rsp_err_code);
}

static void a2dp_get_config_rsp(struct bt_a2dp_stream *stream, struct bt_a2dp_codec_cfg *codec_cfg,
				uint8_t rsp_err_code)
{
	if ((codec_cfg != NULL) && (codec_cfg->codec_config != NULL)) {
		test_rsp_cfg = *codec_cfg->codec_config;
	}

	test_evt_post(EVT_GET_CONFIG_RSP, stream, rsp_err_code, 0);
}

static int a2dp_delay_report_req(struct bt_a2dp_stream *stream, uint16_t value,
				 uint8_t *rsp_err_code)
{
	*rsp_err_code = 0;
	test_evt_post(EVT_DELAY_REPORT_REQ, stream, value, 0);

	return 0;
}

static void a2dp_delay_report_rsp(struct bt_a2dp_stream *stream, uint8_t rsp_err_code)
{
	test_evt_post(EVT_DELAY_REPORT_RSP, stream, rsp_err_code, 0);
}

static struct bt_a2dp_cb a2dp_cb = {
	.connected = a2dp_connected,
	.disconnected = a2dp_disconnected,
	.config_req = a2dp_config_req,
	.reconfig_req = a2dp_reconfig_req,
	.config_rsp = a2dp_config_rsp,
	.establish_req = a2dp_establish_req,
	.establish_rsp = a2dp_establish_rsp,
	.release_req = a2dp_release_req,
	.release_rsp = a2dp_release_rsp,
	.start_req = a2dp_start_req,
	.start_rsp = a2dp_start_rsp,
	.suspend_req = a2dp_suspend_req,
	.suspend_rsp = a2dp_suspend_rsp,
	.abort_req = a2dp_abort_req,
	.abort_rsp = a2dp_abort_rsp,
	.get_config_req = a2dp_get_config_req,
	.get_config_rsp = a2dp_get_config_rsp,
	.delay_report_req = a2dp_delay_report_req,
	.delay_report_rsp = a2dp_delay_report_rsp,
};

static void stream_configured(struct bt_a2dp_stream *stream)
{
	atomic_set(&stream_in_use, 1);
	test_evt_post(EVT_CONFIGURED, stream, 0, 0);
}

static void stream_established(struct bt_a2dp_stream *stream)
{
	test_evt_post(EVT_ESTABLISHED, stream, 0, 0);
}

static void stream_released(struct bt_a2dp_stream *stream)
{
	test_evt_post(EVT_RELEASED, stream, 0, 0);
	atomic_set(&stream_in_use, 0);
}

static void stream_started(struct bt_a2dp_stream *stream)
{
	test_evt_post(EVT_STARTED, stream, 0, 0);
}

static void stream_suspended(struct bt_a2dp_stream *stream)
{
	test_evt_post(EVT_SUSPENDED, stream, 0, 0);
}

/* SBC media payload header (one frame) followed by a pattern of the sequence number. */
#define MEDIA_SBC_HDR ((uint8_t)BT_A2DP_SBC_MEDIA_HDR_ENCODE(1, 0, 0, 0))

static void media_fill(uint8_t *data, size_t len, uint16_t seq_num)
{
	for (size_t i = 0; i < len; i++) {
		data[i] = (uint8_t)((seq_num * 31U) + (i * 7U) + 1U);
	}
}

static void stream_recv(struct bt_a2dp_stream *stream, struct net_buf *buf, uint16_t seq_num,
			uint32_t ts)
{
	uint8_t expected[TEST_MEDIA_PAYLOAD_LEN];
	bool ok = (buf->len == (1U + sizeof(expected))) && (buf->data[0] == MEDIA_SBC_HDR);

	if (ok) {
		media_fill(expected, sizeof(expected), seq_num);
		ok = memcmp(&buf->data[1], expected, sizeof(expected)) == 0;
	}

	if (!ok) {
		LOG_ERR("Unexpected media packet %u (len %u)", seq_num, buf->len);
		atomic_inc(&test_media_bad);
	}

	test_evt_post(EVT_RECV, stream, seq_num, (int32_t)ts);
}

static void stream_delay_report(struct bt_a2dp_stream *stream, uint16_t value)
{
	test_evt_post(EVT_DELAY_REPORT, stream, value, 0);
}

static struct bt_a2dp_stream_ops stream_ops = {
	.configured = stream_configured,
	.established = stream_established,
	.released = stream_released,
	.started = stream_started,
	.suspended = stream_suspended,
	.recv = stream_recv,
	.delay_report = stream_delay_report,
};

void test_a2dp_suite_setup(enum test_br_role role)
{
	int err;

	/* The AVDTP L2CAP channels require security level 2. */
	test_br_setup(role, BT_SECURITY_L2);

	err = bt_a2dp_register_cb(&a2dp_cb);
	zassert_equal(err, 0, "Failed to register A2DP callbacks (err %d)", err);

	err = bt_a2dp_register_ep(&sink_ep, BT_AVDTP_AUDIO, BT_AVDTP_SINK);
	zassert_equal(err, 0, "Failed to register the sink endpoint (err %d)", err);

	err = bt_a2dp_register_ep(&source_ep, BT_AVDTP_AUDIO, BT_AVDTP_SOURCE);
	zassert_equal(err, 0, "Failed to register the source endpoint (err %d)", err);

	err = bt_a2dp_register_ep(&source_dr_ep, BT_AVDTP_AUDIO, BT_AVDTP_SOURCE);
	zassert_equal(err, 0, "Failed to register the delay reporting source endpoint (err %d)",
		      err);

	err = bt_sdp_register_service(&a2dp_sink_rec);
	zassert_equal(err, 0, "Failed to register the A2DP sink SDP record (err %d)", err);

	err = bt_sdp_register_service(&a2dp_source_rec);
	zassert_equal(err, 0, "Failed to register the A2DP source SDP record (err %d)", err);
}

void test_a2dp_suite_teardown(void)
{
	test_br_teardown();
}

void test_a2dp_before(void)
{
	/*
	 * Clear the stream first: a late release of the stream of the previous test then finds no
	 * stream callbacks, or posts its event before the reset.
	 */
	test_a2dp = NULL;
	memset(&test_stream, 0, sizeof(test_stream));
	atomic_set(&stream_in_use, 0);
	test_evt_reset();

	test_config_req_err = 0;
	atomic_set(&test_media_bad, 0);
	media_tx_seq = 0;
	media_rx_seq = 0;
}

void test_expect(enum test_evt type)
{
	struct test_event evt;

	TEST_EXPECT(type, TEST_A2DP_TIMEOUT, &evt);
	zassert_equal_ptr(evt.obj, &test_stream, "%s of an unexpected stream", test_evt_str(type));
}

void test_expect_rsp(enum test_evt type, uint8_t err)
{
	struct test_event evt;

	TEST_EXPECT(type, TEST_A2DP_TIMEOUT, &evt);
	zassert_equal_ptr(evt.obj, &test_stream, "%s of an unexpected stream", test_evt_str(type));
	zassert_equal(evt.value, err, "%s error 0x%02x, expected 0x%02x", test_evt_str(type),
		      evt.value, err);
}

void test_int_connect(void)
{
	k_timepoint_t end = sys_timepoint_calc(TEST_A2DP_TIMEOUT);
	struct test_event evt;
	struct bt_a2dp *a2dp;
	int err;

	/*
	 * The stack keeps the A2DP and AVDTP objects of a released connection until its release
	 * work has run. Until then a new connection is refused, by the local stack (no object) or
	 * by the stack of the peer (reported as a disconnection): retry.
	 */
	while (true) {
		zassert_false(sys_timepoint_expired(end), "Failed to connect A2DP");

		a2dp = bt_a2dp_connect(test_br_conn());
		if (a2dp == NULL) {
			k_sleep(K_MSEC(100));
			continue;
		}

		err = evt_wait(BIT(EVT_CONNECTED) | BIT(EVT_DISCONNECTED), TEST_A2DP_TIMEOUT, &evt);
		zassert_equal(err, 0, "No A2DP connection result (err %d)", err);
		zassert_equal_ptr(evt.obj, a2dp, "Unexpected A2DP object");

		if (evt.type == EVT_CONNECTED) {
			return;
		}

		LOG_WRN("A2DP connection refused, retry");
		k_sleep(K_MSEC(100));
	}
}

void test_int_disconnect(void)
{
	int err;

	err = bt_a2dp_disconnect(test_a2dp);
	zassert_equal(err, 0, "Failed to disconnect A2DP (err %d)", err);

	test_wait_disconnected();
}

void test_acp_wait_connected(void)
{
	/* The stack reports a connection only once it is established (err 0). */
	TEST_EXPECT(EVT_CONNECTED, TEST_A2DP_TIMEOUT, NULL);
}

void test_wait_disconnected(void)
{
	/*
	 * The stack releases a configured stream with the connection: before the disconnection
	 * with a media channel, after it (release work) without. If the flag is clear, the
	 * released event is queued already or the stream was not configured, see stream_in_use.
	 */
	bool in_use = atomic_get(&stream_in_use) != 0;

	TEST_EXPECT(EVT_DISCONNECTED, TEST_A2DP_TIMEOUT, NULL);

	if (in_use) {
		test_expect(EVT_RELEASED);
	}

	/* Nothing more is reported for this connection. */
	test_evt_reset();
}

#define SDP_CLIENT_BUF_LEN 512
NET_BUF_POOL_FIXED_DEFINE(sdp_client_pool, 1, SDP_CLIENT_BUF_LEN, CONFIG_BT_CONN_TX_USER_DATA_SIZE,
			  NULL);

static struct bt_uuid_16 sdp_uuid;
static struct bt_sdp_discover_params sdp_params;
static K_SEM_DEFINE(sdp_sem, 0, 1);
static int sdp_err;
static uint16_t sdp_avdtp_version;

static uint8_t sdp_discover_cb(struct bt_conn *conn, struct bt_sdp_client_result *result,
			       const struct bt_sdp_discover_params *params)
{
	if ((result == NULL) || (result->resp_buf == NULL) || (result->resp_buf->len == 0)) {
		LOG_DBG("No (more) SDP record found");
		if (sdp_avdtp_version == 0U) {
			sdp_err = -ENOENT;
		}
		k_sem_give(&sdp_sem);
		return BT_SDP_DISCOVER_UUID_STOP;
	}

	sdp_err = bt_sdp_get_proto_param(result->resp_buf, BT_SDP_PROTO_AVDTP, &sdp_avdtp_version);
	LOG_DBG("AVDTP version 0x%04x (err %d)", sdp_avdtp_version, sdp_err);

	k_sem_give(&sdp_sem);
	return BT_SDP_DISCOVER_UUID_STOP;
}

/* AVDTP version of the A2DP SDP record of the peer for its endpoint type @p remote_tsep. */
static uint16_t sdp_get_avdtp_version(enum bt_avdtp_sep_type remote_tsep)
{
	int err;

	sdp_uuid.uuid.type = BT_UUID_TYPE_16;
	sdp_uuid.val = (remote_tsep == BT_AVDTP_SINK) ? BT_SDP_AUDIO_SINK_SVCLASS
						      : BT_SDP_AUDIO_SOURCE_SVCLASS;

	sdp_avdtp_version = 0U;
	sdp_err = -EIO;

	sdp_params.uuid = &sdp_uuid.uuid;
	sdp_params.func = sdp_discover_cb;
	sdp_params.pool = &sdp_client_pool;
	sdp_params.type = BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR;
	sdp_params.ids = NULL;

	k_sem_reset(&sdp_sem);
	err = bt_sdp_discover(test_br_conn(), &sdp_params);
	zassert_equal(err, 0, "Failed to start SDP discovery (err %d)", err);

	err = test_br_wait(&sdp_sem, TEST_A2DP_TIMEOUT);
	zassert_equal(err, 0, "SDP discovery timeout (err %d)", err);
	zassert_equal(sdp_err, 0, "No AVDTP version in the SDP record (err %d)", sdp_err);

	return sdp_avdtp_version;
}

static struct bt_avdtp_sep_info found_seps[4];
static struct bt_a2dp_discover_param discover_param;
static K_SEM_DEFINE(discover_sem, 0, 1);
static enum bt_avdtp_sep_type discover_tsep;
static bool discover_delay_report;
static uint8_t discover_seid;
static bool discover_found;

static uint8_t discover_cb(struct bt_a2dp *a2dp, struct bt_a2dp_ep_info *info,
			   struct bt_a2dp_ep **ep)
{
	bool match;

	if (info == NULL) {
		k_sem_give(&discover_sem);
		return BT_A2DP_DISCOVER_EP_STOP;
	}

	LOG_DBG("Endpoint %u type %u codec %u delay reporting %u", info->sep_info->id,
		info->sep_info->tsep, info->codec_type, info->delay_report);

	if (discover_seid != 0U) {
		match = info->sep_info->id == discover_seid;
	} else {
		match = info->delay_report == discover_delay_report;
	}

	if (!discover_found && match && (info->codec_type == BT_A2DP_SBC) &&
	    (info->sep_info->tsep == discover_tsep)) {
		discover_found = true;
		*ep = &test_remote_ep;
	}

	return BT_A2DP_DISCOVER_EP_CONTINUE;
}

static void discover_peer(enum bt_avdtp_sep_type local_tsep, bool delay_report, uint8_t seid,
			  uint16_t avdtp_version)
{
	int err;

	if (avdtp_version == 0U) {
		avdtp_version = sdp_get_avdtp_version(test_peer_tsep(local_tsep));
	}

	discover_tsep = test_peer_tsep(local_tsep);
	discover_delay_report = delay_report;
	discover_seid = seid;
	discover_found = false;

	memset(&discover_param, 0, sizeof(discover_param));
	discover_param.cb = discover_cb;
	discover_param.seps_info = found_seps;
	discover_param.sep_count = ARRAY_SIZE(found_seps);
	discover_param.avdtp_version = avdtp_version;

	k_sem_reset(&discover_sem);
	err = bt_a2dp_discover(test_a2dp, &discover_param);
	zassert_equal(err, 0, "Failed to discover the endpoints (err %d)", err);

	err = test_br_wait(&discover_sem, TEST_A2DP_TIMEOUT);
	zassert_equal(err, 0, "Endpoint discovery timeout (err %d)", err);
}

void test_int_discover(enum bt_avdtp_sep_type local_tsep, bool delay_report, uint16_t avdtp_version)
{
	discover_peer(local_tsep, delay_report, 0U, avdtp_version);
	zassert_true(discover_found, "No SBC %s endpoint with delay reporting %u found",
		     (discover_tsep == BT_AVDTP_SINK) ? "sink" : "source", delay_report);
}

void test_int_discover_seid(enum bt_avdtp_sep_type local_tsep, uint8_t seid, uint16_t avdtp_version)
{
	discover_peer(local_tsep, false, seid, avdtp_version);
	zassert_true(discover_found, "No SBC %s endpoint %u found",
		     (discover_tsep == BT_AVDTP_SINK) ? "sink" : "source", seid);
}

static struct bt_a2dp_codec_ie cfg_ie;
static struct bt_a2dp_codec_cfg cfg = {
	.codec_config = &cfg_ie,
};

static void sbc_cfg_set(uint8_t freq, uint8_t min_bitpool, bool delay_report)
{
	cfg_ie.len = BT_A2DP_SBC_IE_LENGTH;
	cfg_ie.codec_ie[0] = freq | A2DP_SBC_CH_MODE_JOINT;
	cfg_ie.codec_ie[1] =
		A2DP_SBC_BLK_LEN_16 | A2DP_SBC_SUBBAND_8 | A2DP_SBC_ALLOC_MTHD_LOUDNESS;
	cfg_ie.codec_ie[2] = min_bitpool;
	cfg_ie.codec_ie[3] = TEST_MAX_BITPOOL;
	cfg.delay_report = delay_report;
}

int test_int_config_send(enum bt_avdtp_sep_type local_tsep, uint8_t freq, uint8_t min_bitpool,
			 bool delay_report)
{
	sbc_cfg_set(freq, min_bitpool, delay_report);
	bt_a2dp_stream_cb_register(&test_stream, &stream_ops);

	return bt_a2dp_stream_config(test_a2dp, &test_stream,
				     test_local_ep(local_tsep, delay_report), &test_remote_ep,
				     &cfg);
}

void test_int_config(enum bt_avdtp_sep_type local_tsep, uint8_t freq, uint8_t min_bitpool,
		     bool delay_report, uint8_t err)
{
	int ret;

	ret = test_int_config_send(local_tsep, freq, min_bitpool, delay_report);
	zassert_equal(ret, 0, "Failed to configure the stream (err %d)", ret);

	test_expect_rsp(EVT_CONFIG_RSP, err);
	if (err == 0U) {
		test_expect(EVT_CONFIGURED);
	}
}

int test_int_reconfig_send(uint8_t freq, uint8_t min_bitpool)
{
	sbc_cfg_set(freq, min_bitpool, test_stream.delay_report);

	return bt_a2dp_stream_reconfig(&test_stream, &cfg);
}

void test_int_reconfig(uint8_t freq, uint8_t min_bitpool, uint8_t err)
{
	int ret;

	ret = test_int_reconfig_send(freq, min_bitpool);
	zassert_equal(ret, 0, "Failed to reconfigure the stream (err %d)", ret);

	test_expect_rsp(EVT_CONFIG_RSP, err);

	/* The stack reports an accepted reconfiguration with the configured callback. */
	if (err == 0U) {
		test_expect(EVT_CONFIGURED);
	}
}

void test_int_prepare(enum bt_avdtp_sep_type local_tsep)
{
	test_int_connect();
	/* A sink endpoint has delay reporting, a source endpoint not. */
	test_int_discover(local_tsep, local_tsep == BT_AVDTP_SOURCE, 0U);
	test_int_config(local_tsep, A2DP_SBC_SAMP_FREQ_44100, TEST_MIN_BITPOOL, false, 0U);
}

static uint8_t saved_acp_seid;

void test_set_acp_seid(uint8_t seid)
{
	/* The stack uses the identifier of the remote endpoint if there is one. */
	if (test_stream.remote_ep != NULL) {
		saved_acp_seid = test_stream.remote_ep->sep.sep_info.id;
		test_stream.remote_ep->sep.sep_info.id = seid;
	} else {
		saved_acp_seid = test_stream.remote_ep_id;
		test_stream.remote_ep_id = seid;
	}
}

void test_restore_acp_seid(void)
{
	if (test_stream.remote_ep != NULL) {
		test_stream.remote_ep->sep.sep_info.id = saved_acp_seid;
	} else {
		test_stream.remote_ep_id = saved_acp_seid;
	}
}

void test_stream_cmd(int (*cmd)(struct bt_a2dp_stream *stream), enum test_evt rsp, uint8_t err)
{
	int ret;

	ret = cmd(&test_stream);
	zassert_equal(ret, 0, "Failed to send the command of %s (err %d)", test_evt_str(rsp), ret);

	test_expect_rsp(rsp, err);
}

void test_delay_report_cmd(uint16_t value, uint8_t err)
{
	int ret;

	ret = bt_a2dp_stream_delay_report(&test_stream, value);
	zassert_equal(ret, 0, "Failed to send the delay report (err %d)", ret);

	test_expect_rsp(EVT_DELAY_REPORT_RSP, err);
}

NET_BUF_POOL_DEFINE(media_tx_pool, 4, BT_L2CAP_BUF_SIZE(CONFIG_BT_L2CAP_TX_MTU),
		    CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

void test_media_send(uint16_t count)
{
	uint8_t payload[TEST_MEDIA_PAYLOAD_LEN];

	for (uint16_t i = 0; i < count; i++) {
		uint16_t seq_num = media_tx_seq++;
		struct net_buf *buf;
		int err;

		buf = bt_a2dp_stream_create_pdu(&media_tx_pool, TEST_A2DP_TIMEOUT);
		zassert_not_null(buf, "No media buffer");

		net_buf_add_u8(buf, MEDIA_SBC_HDR);
		media_fill(payload, sizeof(payload), seq_num);
		net_buf_add_mem(buf, payload, sizeof(payload));

		err = bt_a2dp_stream_send(&test_stream, buf, seq_num,
					  (uint32_t)seq_num * TEST_MEDIA_TS_STEP);
		if (err != 0) {
			net_buf_unref(buf);
		}
		zassert_equal(err, 0, "Failed to send media packet %u (err %d)", seq_num, err);
	}
}

int test_media_send_on(struct bt_a2dp_stream *stream)
{
	struct net_buf *buf;
	int err;

	buf = bt_a2dp_stream_create_pdu(&media_tx_pool, TEST_A2DP_TIMEOUT);
	zassert_not_null(buf, "No media buffer");

	net_buf_add_u8(buf, MEDIA_SBC_HDR);
	net_buf_add(buf, TEST_MEDIA_PAYLOAD_LEN);

	err = bt_a2dp_stream_send(stream, buf, 0U, 0U);
	if (err != 0) {
		net_buf_unref(buf);
	}

	return err;
}

void test_stream_cmds_refused(void)
{
	CHECK_EINVAL(bt_a2dp_stream_establish(&test_stream));
	CHECK_EINVAL(bt_a2dp_stream_release(&test_stream));
	CHECK_EINVAL(bt_a2dp_stream_start(&test_stream));
	CHECK_EINVAL(bt_a2dp_stream_suspend(&test_stream));
	CHECK_EINVAL(bt_a2dp_stream_abort(&test_stream));
	CHECK_EINVAL(bt_a2dp_stream_get_config(&test_stream));
	CHECK_EINVAL(test_int_reconfig_send(A2DP_SBC_SAMP_FREQ_44100, TEST_MIN_BITPOOL));
	CHECK_EINVAL(bt_a2dp_stream_delay_report(&test_stream, TEST_DELAY_REPORT));
	CHECK_EINVAL(test_media_send_on(&test_stream));
	zassert_equal(bt_a2dp_get_mtu(&test_stream), 0U, "MTU of the stream");
}

void test_media_expect(uint16_t count)
{
	struct test_event evt;

	for (uint16_t i = 0; i < count; i++) {
		uint16_t seq_num = media_rx_seq++;

		TEST_EXPECT(EVT_RECV, TEST_A2DP_TIMEOUT, &evt);
		zassert_equal(evt.value, seq_num, "Media packet %d, expected %u", evt.value,
			      seq_num);
		zassert_equal(evt.value2, (int32_t)((uint32_t)seq_num * TEST_MEDIA_TS_STEP),
			      "Time stamp %d of media packet %u", evt.value2, seq_num);
	}

	zassert_equal(atomic_get(&test_media_bad), 0, "%d media packets with a bad payload",
		      (int)atomic_get(&test_media_bad));
}
