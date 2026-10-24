/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Stream tests of the acceptor (device 1), counterpart of test_int_stream.c. */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/classic/a2dp.h>
#include <zephyr/bluetooth/classic/a2dp_codec_sbc.h>
#include <zephyr/bluetooth/classic/avdtp.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"

/* The A2DP connection and an accepted configuration of the endpoint of type @p tsep. */
static void configured(enum bt_avdtp_sep_type tsep, bool delay_report)
{
	test_acp_wait_connected();

	test_expect_rsp(EVT_CONFIG_REQ, BT_AVDTP_SUCCESS);
	zassert_equal(test_req_delay_report, delay_report, "Delay reporting %u",
		      test_req_delay_report);
	zassert_equal_ptr(test_stream.local_ep, test_local_ep(tsep, delay_report),
			  "Unexpected local endpoint");
	test_expect(EVT_CONFIGURED);
}

static void establish(void)
{
	test_expect(EVT_ESTABLISH_REQ);
	test_expect(EVT_ESTABLISHED);
}

static void start(void)
{
	test_expect(EVT_START_REQ);
	test_expect(EVT_STARTED);
}

static void suspend(void)
{
	test_expect(EVT_SUSPEND_REQ);
	test_expect(EVT_SUSPENDED);
}

static void release(void)
{
	test_expect(EVT_RELEASE_REQ);
	test_expect(EVT_RELEASED);
}

ZTEST(a2dp_acp_stream, test_001_media_source_int)
{
	test_br_sync();

	configured(BT_AVDTP_SINK, false);
	establish();
	start();
	test_media_expect(TEST_MEDIA_PKTS);

	test_br_sync();

	suspend();
	start();
	test_media_expect(TEST_MEDIA_PKTS);

	test_br_sync();

	release();

	test_br_sync();
}

ZTEST(a2dp_acp_stream, test_002_delay_report_configured)
{
	struct test_event evt;

	test_br_sync();

	configured(BT_AVDTP_SOURCE, true);
	zassert_true(test_stream.delay_report, "Delay reporting not configured");

	TEST_EXPECT(EVT_DELAY_REPORT_REQ, TEST_A2DP_TIMEOUT, &evt);
	zassert_equal(evt.value, TEST_DELAY_REPORT_STREAM, "Delay report %d", evt.value);
	TEST_EXPECT(EVT_DELAY_REPORT, TEST_A2DP_TIMEOUT, &evt);
	zassert_equal(evt.value, TEST_DELAY_REPORT_STREAM, "Delay report %d", evt.value);

	establish();

	test_br_sync();
}

ZTEST(a2dp_acp_stream, test_003_reconfigure_48k)
{
	struct bt_a2dp_codec_sbc_params *sbc;

	test_br_sync();

	configured(BT_AVDTP_SINK, false);
	establish();

	test_expect(EVT_RECONFIG_REQ);
	sbc = (struct bt_a2dp_codec_sbc_params *)&test_req_cfg.codec_ie[0];
	zassert_equal(bt_a2dp_sbc_get_sampling_frequency(sbc), 48000U, "Sampling frequency %u",
		      bt_a2dp_sbc_get_sampling_frequency(sbc));
	test_expect(EVT_CONFIGURED);
	test_expect(EVT_GET_CONFIG_REQ);

	test_br_sync();
}

ZTEST(a2dp_acp_stream, test_004_config_rejected)
{
	/* The application rejects the next configuration request. */
	test_config_req_err = BT_AVDTP_UNSUPPORTED_CONFIGURATION;

	test_br_sync();

	test_acp_wait_connected();

	/* The configuration outside the capabilities is rejected by the stack and not reported. */
	test_expect_rsp(EVT_CONFIG_REQ, BT_AVDTP_UNSUPPORTED_CONFIGURATION);
	test_expect_rsp(EVT_CONFIG_REQ, BT_AVDTP_SUCCESS);
	test_expect(EVT_CONFIGURED);
	test_evt_none(EVT_CONFIGURED, K_NO_WAIT);

	test_br_sync();
}

ZTEST(a2dp_acp_stream, test_005_media_sink_int)
{
	test_br_sync();

	configured(BT_AVDTP_SOURCE, false);
	establish();
	start();
	test_media_send(TEST_MEDIA_PKTS);
	suspend();
	release();

	test_br_sync();
}

ZTEST(a2dp_acp_stream, test_006_abort_configured)
{
	test_br_sync();

	configured(BT_AVDTP_SOURCE, false);
	test_expect(EVT_ABORT_REQ);
	test_expect(EVT_RELEASED);

	test_br_sync();
}

ZTEST(a2dp_acp_stream, test_007_avdtp_1_2_peer)
{
	test_br_sync();

	test_acp_wait_connected();

	/* The configuration with delay reporting is refused by the initiator stack, not sent. */
	test_expect_rsp(EVT_CONFIG_REQ, BT_AVDTP_SUCCESS);
	zassert_false(test_req_delay_report, "Delay reporting configured");
	zassert_equal_ptr(test_stream.local_ep, test_local_ep(BT_AVDTP_SOURCE, true),
			  "Unexpected local endpoint");
	test_expect(EVT_CONFIGURED);
	test_evt_none(EVT_CONFIG_REQ, K_NO_WAIT);

	test_br_sync();
}

/* The initiator only checks local API calls. */
ZTEST(a2dp_acp_stream, test_008_unconfigured_stream)
{
	test_br_sync();
	test_br_sync();
}

ZTEST(a2dp_acp_stream, test_009_null_stream)
{
	test_br_sync();
	test_br_sync();
}

/* The stream released by the initiator is refused on this side too. */
ZTEST(a2dp_acp_stream, test_010_released_stream)
{
	test_br_sync();

	configured(BT_AVDTP_SOURCE, false);
	establish();
	release();

	test_stream_cmds_refused();

	test_br_sync();
}

/* The value of the next event of @p type of the test stream: the result of the callback action. */
static int32_t expect_value(enum test_evt type)
{
	struct test_event evt;

	TEST_EXPECT(type, TEST_A2DP_TIMEOUT, &evt);
	zassert_equal_ptr(evt.obj, &test_stream, "%s of an unexpected stream", test_evt_str(type));

	return evt.value;
}

/*
 * An application sends media once the stream is started, possibly from the started callback, and
 * no more once it is suspended.
 */
ZTEST(a2dp_acp_stream, test_011_media_from_started_cb)
{
	int32_t err;

	test_act_in_cb = true;

	test_br_sync();

	configured(BT_AVDTP_SOURCE, false);
	establish();

	test_expect(EVT_START_REQ);
	err = expect_value(EVT_STARTED);
	zassert_equal(err, 0, "Media packet not sent from the started callback (err %d)", err);

	test_expect(EVT_SUSPEND_REQ);
	err = expect_value(EVT_SUSPENDED);
	zassert_equal(err, -EIO, "Media packet sent from the suspended callback (err %d)", err);

	release();

	test_br_sync();
}

/* A sink sends the first delay report from the configured callback (AVDTP v1.3, 8.19.1). */
ZTEST(a2dp_acp_stream, test_012_delay_report_from_configured_cb)
{
	int32_t err;

	test_act_in_cb = true;

	test_br_sync();

	test_acp_wait_connected();
	test_expect_rsp(EVT_CONFIG_REQ, BT_AVDTP_SUCCESS);
	zassert_true(test_req_delay_report, "Delay reporting not configured");
	zassert_equal_ptr(test_stream.local_ep, test_local_ep(BT_AVDTP_SINK, true),
			  "Unexpected local endpoint");

	err = expect_value(EVT_CONFIGURED);
	zassert_equal(err, 0, "Delay report not sent from the configured callback (err %d)", err);
	test_expect_rsp(EVT_DELAY_REPORT_RSP, BT_AVDTP_SUCCESS);

	test_br_sync();
}

ZTEST(a2dp_acp_stream, test_013_config_endpoint_in_use)
{
	test_br_sync();

	configured(BT_AVDTP_SOURCE, false);
	test_expect(EVT_ABORT_REQ);
	test_expect(EVT_RELEASED);

	test_br_sync();
}

/* The configuration outside the capabilities is rejected by the stack and not reported. */
ZTEST(a2dp_acp_stream, test_014_rejected_config_stream)
{
	test_br_sync();

	test_acp_wait_connected();
	test_expect_rsp(EVT_CONFIG_REQ, BT_AVDTP_SUCCESS);
	test_expect(EVT_CONFIGURED);
	test_evt_none(EVT_CONFIGURED, K_NO_WAIT);

	test_br_sync();
}

static struct bt_a2dp_stream collision_stream;
static int collision_err;

/* From the configuration request of the initiator: configure the endpoint of the initiator. */
static void collision_config(void)
{
	collision_err =
		test_stream_config_send(&collision_stream, test_local_ep(BT_AVDTP_SOURCE, true),
					A2DP_SBC_SAMP_FREQ_44100, TEST_MIN_BITPOOL, false);
}

ZTEST(a2dp_acp_stream, test_015_config_collision)
{
	struct test_event evt;

	memset(&collision_stream, 0, sizeof(collision_stream));
	collision_err = -EAGAIN;

	test_br_sync();

	/* The sink endpoint of the initiator. */
	test_acp_wait_connected();
	test_int_discover(BT_AVDTP_SOURCE, true, 0U);

	test_config_req_err = BT_AVDTP_SEP_IN_USE;
	test_config_req_hook = collision_config;

	test_br_sync();

	test_expect_rsp(EVT_CONFIG_REQ, BT_AVDTP_SEP_IN_USE);
	zassert_equal(collision_err, 0, "Failed to send the configuration (err %d)", collision_err);

	TEST_EXPECT(EVT_CONFIG_RSP, TEST_A2DP_TIMEOUT, &evt);
	zassert_equal_ptr(evt.obj, &collision_stream, "config_rsp of an unexpected stream");
	zassert_equal(evt.value, BT_AVDTP_SEP_IN_USE, "Configuration error 0x%02x", evt.value);

	/* The initiator configures its endpoint again. */
	test_expect_rsp(EVT_CONFIG_REQ, BT_AVDTP_SUCCESS);
	test_expect(EVT_CONFIGURED);

	test_br_sync();
}

static void *setup(void)
{
	test_a2dp_suite_setup(TEST_BR_ACCEPTOR);
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

ZTEST_SUITE(a2dp_acp_stream, NULL, setup, before, NULL, teardown);
