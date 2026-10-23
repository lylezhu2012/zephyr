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

	test_br_sync();
}

ZTEST(a2dp_acp_stream, test_005_media_sink_int)
{
	test_br_sync();

	configured(BT_AVDTP_SOURCE, false);
	establish();
	start();

	/*
	 * The stack reports started from the start indication, before the endpoint is streaming
	 * (bt_avdtp_send_media_data() refuses it until then). Sending from the test thread works
	 * as long as the Bluetooth RX thread, cooperative and of a higher priority, does not block
	 * while it handles the indication.
	 */
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
