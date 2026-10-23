/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Signaling tests of the initiator (device 0): the board-to-board a2dp_sink tests. As there, each
 * test body runs twice, with the A2DP connection released in between. With
 * CONFIG_TEST_A2DP_INT_SINK=n the same tests run with the initiator on the source side; the
 * board-to-board a2dp_source counterparts are disabled there.
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/classic/a2dp.h>
#include <zephyr/bluetooth/classic/a2dp_codec_sbc.h>
#include <zephyr/bluetooth/classic/avdtp.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"

static void run_twice(void (*body)(void))
{
	test_br_sync();
	body();

	/* Waits for the release of the stream and drops the events of the first connection. */
	test_int_disconnect();

	/* The acceptor has seen the A2DP connection released. */
	test_br_sync();
	body();

	test_br_sync();
}

/* The command to an unknown endpoint is rejected, then the command to the peer is accepted. */
static void cmd_bad_seid_then_good(int (*cmd)(struct bt_a2dp_stream *stream), enum test_evt rsp,
				   uint8_t bad_seid_err)
{
	test_set_acp_seid(TEST_BAD_SEID);
	test_stream_cmd(cmd, rsp, bad_seid_err);
	test_restore_acp_seid();

	test_stream_cmd(cmd, rsp, BT_AVDTP_SUCCESS);
}

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

static void set_configuration(void)
{
	enum bt_avdtp_sep_type tsep = TEST_INT_TSEP;

	test_int_connect();
	test_int_discover(tsep, tsep == BT_AVDTP_SOURCE, 0U);

	/* Rejected by the stack of the peer (SBC capability check). */
	test_int_config(tsep, A2DP_SBC_SAMP_FREQ_44100, TEST_MIN_BITPOOL_INVALID, false,
			BT_A2DP_INVALID_MINIMUM_BITPOOL_VALUE);

	test_int_config(tsep, A2DP_SBC_SAMP_FREQ_44100, TEST_MIN_BITPOOL, false, BT_AVDTP_SUCCESS);
}

/* b2b: set_configuration_cmd - invalid minimum bitpool rejected, then a valid configuration. */
ZTEST(a2dp_int_signaling, test_001_set_configuration)
{
	run_twice(set_configuration);
}

static void establish_cmd(void)
{
	test_int_prepare(TEST_INT_TSEP);

	cmd_bad_seid_then_good(bt_a2dp_stream_establish, EVT_ESTABLISH_RSP, BT_AVDTP_BAD_ACP_SEID);
	test_expect(EVT_ESTABLISHED);
}

/* b2b: establish_cmd */
ZTEST(a2dp_int_signaling, test_002_establish)
{
	run_twice(establish_cmd);
}

static void release_cmd(void)
{
	test_int_prepare(TEST_INT_TSEP);
	establish();

	cmd_bad_seid_then_good(bt_a2dp_stream_release, EVT_RELEASE_RSP, BT_AVDTP_BAD_ACP_SEID);
	test_expect(EVT_RELEASED);
}

/* b2b: release_cmd */
ZTEST(a2dp_int_signaling, test_003_release)
{
	run_twice(release_cmd);
}

static void start_cmd(void)
{
	test_int_prepare(TEST_INT_TSEP);
	establish();

	cmd_bad_seid_then_good(bt_a2dp_stream_start, EVT_START_RSP, BT_AVDTP_BAD_ACP_SEID);
	test_expect(EVT_STARTED);
}

/* b2b: start_cmd */
ZTEST(a2dp_int_signaling, test_004_start)
{
	run_twice(start_cmd);
}

static void suspend_cmd(void)
{
	test_int_prepare(TEST_INT_TSEP);
	establish();
	start();

	cmd_bad_seid_then_good(bt_a2dp_stream_suspend, EVT_SUSPEND_RSP, BT_AVDTP_BAD_ACP_SEID);
	test_expect(EVT_SUSPENDED);
}

/* b2b: suspend_cmd */
ZTEST(a2dp_int_signaling, test_005_suspend)
{
	run_twice(suspend_cmd);
}

static void reconfigure_cmd(void)
{
	test_int_prepare(TEST_INT_TSEP);
	establish();
	start();
	suspend();

	/* Below the minimum bitpool of the capabilities of the peer endpoint. */
	test_int_reconfig(A2DP_SBC_SAMP_FREQ_44100, TEST_MIN_BITPOOL_RECONF_NS,
			  BT_A2DP_NOT_SUPPORTED_MINIMUM_BITPOOL_VALUE);

	test_int_reconfig(A2DP_SBC_SAMP_FREQ_44100, TEST_MIN_BITPOOL_RECONF, BT_AVDTP_SUCCESS);
}

/* b2b: reconfigure_cmd - unsupported minimum bitpool rejected, then a valid one. */
ZTEST(a2dp_int_signaling, test_006_reconfigure)
{
	run_twice(reconfigure_cmd);
}

static void abort_cmd(void)
{
	test_int_prepare(TEST_INT_TSEP);
	establish();

	/*
	 * AVDTP v1.3, 8.16.2: "If an AVDTP_ABORT_CMD contains an invalid SEID, no response shall
	 * be sent." The stack reports the abort with BT_AVDTP_TIME_OUT after its 6 s timeout.
	 */
	cmd_bad_seid_then_good(bt_a2dp_stream_abort, EVT_ABORT_RSP, BT_AVDTP_TIME_OUT);
	test_expect(EVT_RELEASED);
}

/* b2b: abort_cmd - in the OPEN state. */
ZTEST(a2dp_int_signaling, test_007_abort)
{
	run_twice(abort_cmd);
}

static void get_configuration_cmd(void)
{
	test_int_prepare(TEST_INT_TSEP);

	cmd_bad_seid_then_good(bt_a2dp_stream_get_config, EVT_GET_CONFIG_RSP,
			       BT_AVDTP_BAD_ACP_SEID);
	zassert_equal(test_rsp_cfg.len, BT_A2DP_SBC_IE_LENGTH, "Configuration length %u",
		      test_rsp_cfg.len);
	zassert_equal(test_rsp_cfg.codec_ie[2], TEST_MIN_BITPOOL, "Minimum bitpool %u",
		      test_rsp_cfg.codec_ie[2]);
}

/* b2b: get_configuration_cmd - in the CONFIGURED state. */
ZTEST(a2dp_int_signaling, test_008_get_configuration)
{
	run_twice(get_configuration_cmd);
}

static void get_mtu(void)
{
	uint32_t mtu;

	test_int_prepare(TEST_INT_TSEP);
	establish();

	/* Receive MTU of the media channel of the peer, the same build. */
	mtu = bt_a2dp_get_mtu(&test_stream);
	zassert_equal(mtu, BT_L2CAP_RX_MTU, "Media MTU %u, expected %u", mtu, BT_L2CAP_RX_MTU);
}

/* b2b: get_mtu */
ZTEST(a2dp_int_signaling, test_009_get_mtu)
{
	run_twice(get_mtu);
}

static void delay_report_cmd(void)
{
	struct test_event evt;

	test_int_prepare(TEST_INT_TSEP);

	/*
	 * Delay reporting is not configured. AVDTP v1.3, 8.19.1: "Delay reports are sent from SNK
	 * to SRC." With CONFIG_TEST_A2DP_INT_SINK=n the acceptor is the sink and sends the reports.
	 */
	if (TEST_INT_TSEP == BT_AVDTP_SINK) {
		test_set_acp_seid(TEST_BAD_SEID);
		test_delay_report_cmd(TEST_DELAY_REPORT, BT_AVDTP_BAD_ACP_SEID);
		test_restore_acp_seid();

		test_delay_report_cmd(TEST_DELAY_REPORT, BT_AVDTP_SUCCESS);
	} else {
		TEST_EXPECT(EVT_DELAY_REPORT_REQ, TEST_A2DP_TIMEOUT, &evt);
		zassert_equal(evt.value, TEST_DELAY_REPORT, "Delay report %d", evt.value);
		TEST_EXPECT(EVT_DELAY_REPORT, TEST_A2DP_TIMEOUT, &evt);
		zassert_equal(evt.value, TEST_DELAY_REPORT, "Delay report %d", evt.value);
	}

	/* The sink has the response of its last delay report before the release. */
	test_br_sync();
}

/* b2b: delay_report_cmd - in the CONFIGURED state, without delay reporting configured. */
ZTEST(a2dp_int_signaling, test_010_delay_report)
{
	run_twice(delay_report_cmd);
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

ZTEST_SUITE(a2dp_int_signaling, NULL, setup, before, NULL, teardown);
