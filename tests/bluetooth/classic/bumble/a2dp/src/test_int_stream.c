/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Stream tests of the initiator (device 0), beyond the board-to-board tests: media transfer in
 * both directions, delay reporting configured, reconfiguration of the sampling frequency,
 * configurations rejected by the stack and by the application, abort without media channel, the
 * discovery with AVDTP version 1.2, stream commands without a configured stream, media and the
 * first delay report sent by the acceptor from its stream callbacks, the configuration of an
 * endpoint in use, a stream after a rejected configuration and a configuration collision.
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/sbc.h>
#include <zephyr/bluetooth/classic/a2dp.h>
#include <zephyr/bluetooth/classic/a2dp_codec_sbc.h>
#include <zephyr/bluetooth/classic/avdtp.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"

static void establish(void)
{
	test_stream_cmd(bt_a2dp_stream_establish, EVT_ESTABLISH_RSP, BT_AVDTP_SUCCESS);
	test_expect(EVT_ESTABLISHED);
}

static void start(void)
{
	test_stream_cmd(bt_a2dp_stream_start, EVT_START_RSP, BT_AVDTP_SUCCESS);
	test_expect(EVT_STARTED);
}

static void suspend(void)
{
	test_stream_cmd(bt_a2dp_stream_suspend, EVT_SUSPEND_RSP, BT_AVDTP_SUCCESS);
	test_expect(EVT_SUSPENDED);
}

static void release(void)
{
	test_stream_cmd(bt_a2dp_stream_release, EVT_RELEASE_RSP, BT_AVDTP_SUCCESS);
	test_expect(EVT_RELEASED);
}

/* Media sent by the initiator (source), with a suspend and a restart in between. */
ZTEST(a2dp_int_stream, test_001_media_source_int)
{
	test_br_sync();

	test_int_prepare(BT_AVDTP_SOURCE);
	establish();
	start();
	test_media_send(TEST_MEDIA_PKTS);

	/* The sink has received the media packets. */
	test_br_sync();

	suspend();
	start();
	test_media_send(TEST_MEDIA_PKTS);

	test_br_sync();

	release();

	test_br_sync();
}

/*
 * Delay reporting configured between the sink and a source endpoint with the capability. AVDTP
 * v1.3, 8.19.1: "A first Delay Report shall be sent immediately after the SEP has been
 * configured."
 */
ZTEST(a2dp_int_stream, test_002_delay_report_configured)
{
	test_br_sync();

	test_int_connect();
	test_int_discover(BT_AVDTP_SINK, true, 0U);
	test_int_config(BT_AVDTP_SINK, A2DP_SBC_SAMP_FREQ_44100, TEST_MIN_BITPOOL, true,
			BT_AVDTP_SUCCESS);
	zassert_true(test_stream.delay_report, "Delay reporting not configured");

	test_delay_report_cmd(TEST_DELAY_REPORT_STREAM, BT_AVDTP_SUCCESS);
	establish();

	test_br_sync();
}

/* Reconfiguration from 44.1 kHz to 48 kHz in the OPEN state, read back with Get Configuration. */
ZTEST(a2dp_int_stream, test_003_reconfigure_48k)
{
	struct bt_a2dp_codec_sbc_params *sbc;

	test_br_sync();

	test_int_prepare(BT_AVDTP_SOURCE);
	establish();

	test_int_reconfig(A2DP_SBC_SAMP_FREQ_48000, TEST_MIN_BITPOOL, BT_AVDTP_SUCCESS);
	test_stream_cmd(bt_a2dp_stream_get_config, EVT_GET_CONFIG_RSP, BT_AVDTP_SUCCESS);

	sbc = (struct bt_a2dp_codec_sbc_params *)&test_rsp_cfg.codec_ie[0];
	zassert_equal(bt_a2dp_sbc_get_sampling_frequency(sbc), 48000U, "Sampling frequency %u",
		      bt_a2dp_sbc_get_sampling_frequency(sbc));
	zassert_equal(bt_a2dp_sbc_get_channel_mode(sbc), SBC_CH_MODE_JOINT_STEREO,
		      "Channel mode %d", bt_a2dp_sbc_get_channel_mode(sbc));
	zassert_equal(bt_a2dp_sbc_get_channel_num(sbc), 2U, "Channels %u",
		      bt_a2dp_sbc_get_channel_num(sbc));
	zassert_equal(bt_a2dp_sbc_get_subband_num(sbc), 8U, "Subbands %u",
		      bt_a2dp_sbc_get_subband_num(sbc));
	zassert_equal(bt_a2dp_sbc_get_block_length(sbc), 16U, "Block length %u",
		      bt_a2dp_sbc_get_block_length(sbc));
	zassert_equal(bt_a2dp_sbc_get_allocation_method(sbc), SBC_ALLOC_MTHD_LOUDNESS,
		      "Allocation method %d", bt_a2dp_sbc_get_allocation_method(sbc));

	test_br_sync();
}

/* A configuration rejected by the stack of the peer, then by its application, then accepted. */
ZTEST(a2dp_int_stream, test_004_config_rejected)
{
	test_br_sync();

	test_int_connect();
	test_int_discover(BT_AVDTP_SINK, false, 0U);

	/* 16 kHz is outside the capabilities of the peer endpoint. */
	test_int_config(BT_AVDTP_SINK, A2DP_SBC_SAMP_FREQ_16000, TEST_MIN_BITPOOL, false,
			BT_A2DP_NOT_SUPPORTED_SAMPLING_FREQUENCY);
	test_int_config(BT_AVDTP_SINK, A2DP_SBC_SAMP_FREQ_44100, TEST_MIN_BITPOOL, false,
			BT_AVDTP_UNSUPPORTED_CONFIGURATION);
	test_int_config(BT_AVDTP_SINK, A2DP_SBC_SAMP_FREQ_44100, TEST_MIN_BITPOOL, false,
			BT_AVDTP_SUCCESS);

	test_br_sync();
}

/* Media sent by the acceptor (source) on a stream configured and started by the sink. */
ZTEST(a2dp_int_stream, test_005_media_sink_int)
{
	test_br_sync();

	test_int_prepare(BT_AVDTP_SINK);
	establish();
	start();
	test_media_expect(TEST_MEDIA_PKTS);
	suspend();
	release();

	test_br_sync();
}

/* Abort in the CONFIGURED state, before the media channel exists. */
ZTEST(a2dp_int_stream, test_006_abort_configured)
{
	test_br_sync();

	test_int_prepare(BT_AVDTP_SINK);
	test_stream_cmd(bt_a2dp_stream_abort, EVT_ABORT_RSP, BT_AVDTP_SUCCESS);
	test_expect(EVT_RELEASED);

	test_br_sync();
}

/*
 * Discovery with AVDTP version 1.2 given to the stack for the peer: the stack uses Get
 * Capabilities, and the acceptor reports the delay reporting capability only for Get All
 * Capabilities (Zephyr implementation). The source endpoint with delay reporting, configured with
 * it in test_002, is then found without the capability, and delay reporting cannot be configured.
 */
ZTEST(a2dp_int_stream, test_007_avdtp_1_2_peer)
{
	/* Both devices register the same endpoints in the same order: same SEIDs. */
	uint8_t seid = test_local_ep(BT_AVDTP_SOURCE, true)->sep.sep_info.id;
	int err;

	test_br_sync();

	test_int_connect();
	test_int_discover_seid(BT_AVDTP_SINK, seid, 0x0102U);
	zassert_false(test_remote_ep.delay_report, "Delay reporting capability reported");

	err = test_int_config_send(BT_AVDTP_SINK, A2DP_SBC_SAMP_FREQ_44100, TEST_MIN_BITPOOL, true);
	zassert_equal(err, -EINVAL, "Delay reporting configuration not refused (err %d)", err);

	test_int_config(BT_AVDTP_SINK, A2DP_SBC_SAMP_FREQ_44100, TEST_MIN_BITPOOL, false,
			BT_AVDTP_SUCCESS);

	test_br_sync();
}

/* Stream commands on a stream that has not been configured are refused. */
ZTEST(a2dp_int_stream, test_008_unconfigured_stream)
{
	test_br_sync();

	/* The test stream is cleared before each test. */
	test_stream_cmds_refused();

	test_br_sync();
}

/* Stream commands without a stream are refused. */
ZTEST(a2dp_int_stream, test_009_null_stream)
{
	BT_A2DP_SBC_EP_CFG_DEFAULT(cfg_44100, A2DP_SBC_SAMP_FREQ_44100);

	test_br_sync();

	CHECK_EINVAL(bt_a2dp_stream_establish(NULL));
	CHECK_EINVAL(bt_a2dp_stream_release(NULL));
	CHECK_EINVAL(bt_a2dp_stream_start(NULL));
	CHECK_EINVAL(bt_a2dp_stream_suspend(NULL));
	CHECK_EINVAL(bt_a2dp_stream_abort(NULL));
	CHECK_EINVAL(bt_a2dp_stream_get_config(NULL));
	CHECK_EINVAL(bt_a2dp_stream_reconfig(NULL, &cfg_44100));
	CHECK_EINVAL(bt_a2dp_stream_delay_report(NULL, TEST_DELAY_REPORT));
	CHECK_EINVAL(test_media_send_on(NULL));
	zassert_equal(bt_a2dp_get_mtu(NULL), 0U, "MTU without a stream");

	test_br_sync();
}

/*
 * Stream commands on a released stream are refused: the stream keeps its connection and endpoint,
 * which may be configured again for another stream.
 */
ZTEST(a2dp_int_stream, test_010_released_stream)
{
	test_br_sync();

	test_int_prepare(BT_AVDTP_SINK);
	establish();
	release();

	test_stream_cmds_refused();

	test_br_sync();
}

/* The acceptor (source) sends media from its started callback, and none from suspended. */
ZTEST(a2dp_int_stream, test_011_media_from_started_cb)
{
	test_br_sync();

	test_int_prepare(BT_AVDTP_SINK);
	establish();
	start();
	test_media_expect(1);
	suspend();
	release();

	test_br_sync();
}

/* The acceptor (sink) sends the first delay report from its configured callback. */
ZTEST(a2dp_int_stream, test_012_delay_report_from_configured_cb)
{
	struct test_event evt;

	test_br_sync();

	test_int_connect();
	test_int_discover(BT_AVDTP_SOURCE, true, 0U);
	test_int_config(BT_AVDTP_SOURCE, A2DP_SBC_SAMP_FREQ_44100, TEST_MIN_BITPOOL, true,
			BT_AVDTP_SUCCESS);

	TEST_EXPECT(EVT_DELAY_REPORT_REQ, TEST_A2DP_TIMEOUT, &evt);
	zassert_equal_ptr(evt.obj, &test_stream, "Delay report of an unexpected stream");
	zassert_equal(evt.value, TEST_DELAY_REPORT_STREAM, "Delay report %d", evt.value);
	TEST_EXPECT(EVT_DELAY_REPORT, TEST_A2DP_TIMEOUT, &evt);
	zassert_equal_ptr(evt.obj, &test_stream, "Delay report of an unexpected stream");
	zassert_equal(evt.value, TEST_DELAY_REPORT_STREAM, "Delay report %d", evt.value);

	test_br_sync();
}

/*
 * The configuration of another stream on the endpoint of the test stream is refused, and the
 * test stream keeps its endpoint.
 */
ZTEST(a2dp_int_stream, test_013_config_endpoint_in_use)
{
	BT_A2DP_SBC_EP_CFG_DEFAULT(cfg_44100, A2DP_SBC_SAMP_FREQ_44100);
	static struct bt_a2dp_stream other;
	int err;

	memset(&other, 0, sizeof(other));

	test_br_sync();

	test_int_prepare(BT_AVDTP_SINK);

	err = bt_a2dp_stream_config(test_a2dp, &other, test_local_ep(BT_AVDTP_SINK, false),
				    &test_remote_ep, &cfg_44100);
	zassert_equal(err, -EINVAL, "Configuration on an endpoint in use (err %d)", err);
	CHECK_EINVAL(bt_a2dp_stream_abort(&other));

	test_stream_cmd(bt_a2dp_stream_abort, EVT_ABORT_RSP, BT_AVDTP_SUCCESS);
	test_expect(EVT_RELEASED);

	test_br_sync();
}

/* After a configuration rejected by the peer, the stream is not configured. */
ZTEST(a2dp_int_stream, test_014_rejected_config_stream)
{
	test_br_sync();

	test_int_connect();
	test_int_discover(BT_AVDTP_SINK, false, 0U);

	/* 16 kHz is outside the capabilities of the peer endpoint. */
	test_int_config(BT_AVDTP_SINK, A2DP_SBC_SAMP_FREQ_16000, TEST_MIN_BITPOOL, false,
			BT_A2DP_NOT_SUPPORTED_SAMPLING_FREQUENCY);
	test_stream_cmds_refused();

	test_int_config(BT_AVDTP_SINK, A2DP_SBC_SAMP_FREQ_44100, TEST_MIN_BITPOOL, false,
			BT_AVDTP_SUCCESS);

	test_br_sync();
}

/*
 * Configuration collision: while the configuration of the initiator is pending, the acceptor
 * configures the same endpoint of the initiator, then rejects the configuration of the
 * initiator. The stack of the initiator refuses the configuration of its endpoint in use, and the
 * endpoint can be configured again.
 */
ZTEST(a2dp_int_stream, test_015_config_collision)
{
	test_br_sync();

	test_int_connect();

	/* The acceptor has discovered the endpoints of the initiator. */
	test_br_sync();

	test_int_discover(BT_AVDTP_SINK, false, 0U);
	test_int_config(BT_AVDTP_SINK, A2DP_SBC_SAMP_FREQ_44100, TEST_MIN_BITPOOL, false,
			BT_AVDTP_SEP_IN_USE);

	/*
	 * The configuration of the acceptor is sent before its response on the same channel, so
	 * it has been handled already: refused by the stack, not reported.
	 */
	test_evt_none(EVT_CONFIG_REQ, K_NO_WAIT);
	test_evt_none(EVT_CONFIGURED, K_NO_WAIT);

	test_int_config(BT_AVDTP_SINK, A2DP_SBC_SAMP_FREQ_44100, TEST_MIN_BITPOOL, false,
			BT_AVDTP_SUCCESS);

	test_br_sync();
}

static void *setup(void)
{
	test_a2dp_suite_setup(TEST_BR_INITIATOR);
	return NULL;
}

static void teardown(void *f)
{
	test_a2dp_suite_teardown();
}

static void before(void *f)
{
	test_a2dp_before();
}

ZTEST_SUITE(a2dp_int_stream, NULL, setup, before, NULL, teardown);
