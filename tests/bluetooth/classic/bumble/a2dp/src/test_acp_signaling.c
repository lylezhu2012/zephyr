/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Signaling tests of the acceptor (device 1), counterpart of test_int_signaling.c. Commands to
 * an unknown endpoint (an abort is not answered) and configurations outside the capabilities are
 * refused by the stack and not reported to the application.
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/classic/a2dp.h>
#include <zephyr/bluetooth/classic/a2dp_codec_sbc.h>
#include <zephyr/bluetooth/classic/avdtp.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"

#define LOCAL_TSEP test_peer_tsep(TEST_INT_TSEP)

static void run_twice(void (*body)(void))
{
	test_br_sync();
	body();

	/* Waits for the release of the stream and drops the events of the first connection. */
	test_wait_disconnected();

	test_br_sync();
	body();

	test_br_sync();
}

/* The A2DP connection and the configuration of the board-to-board preparation. */
static void prepare(void)
{
	test_acp_wait_connected();

	test_expect_rsp(EVT_CONFIG_REQ, BT_AVDTP_SUCCESS);
	zassert_equal(test_req_cfg.codec_ie[2], TEST_MIN_BITPOOL, "Minimum bitpool %u",
		      test_req_cfg.codec_ie[2]);
	zassert_false(test_req_delay_report, "Delay reporting configured");
	zassert_equal_ptr(test_stream.local_ep, test_local_ep(LOCAL_TSEP, false),
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

static void set_configuration(void)
{
	prepare();

	/* The configuration with the invalid minimum bitpool is not reported. */
	test_evt_none(EVT_CONFIG_REQ, K_NO_WAIT);
}

ZTEST(a2dp_acp_signaling, test_001_set_configuration)
{
	run_twice(set_configuration);
}

static void establish_cmd(void)
{
	prepare();
	establish();
}

ZTEST(a2dp_acp_signaling, test_002_establish)
{
	run_twice(establish_cmd);
}

static void release_cmd(void)
{
	prepare();
	establish();

	test_expect(EVT_RELEASE_REQ);
	test_expect(EVT_RELEASED);
}

ZTEST(a2dp_acp_signaling, test_003_release)
{
	run_twice(release_cmd);
}

static void start_cmd(void)
{
	prepare();
	establish();
	start();
}

ZTEST(a2dp_acp_signaling, test_004_start)
{
	run_twice(start_cmd);
}

static void suspend_cmd(void)
{
	prepare();
	establish();
	start();
	suspend();
}

ZTEST(a2dp_acp_signaling, test_005_suspend)
{
	run_twice(suspend_cmd);
}

static void reconfigure_cmd(void)
{
	prepare();
	establish();
	start();
	suspend();

	/* Only the reconfiguration within the capabilities is reported. */
	test_expect(EVT_RECONFIG_REQ);
	zassert_equal(test_req_cfg.codec_ie[2], TEST_MIN_BITPOOL_RECONF, "Minimum bitpool %u",
		      test_req_cfg.codec_ie[2]);
	test_expect(EVT_CONFIGURED);
}

ZTEST(a2dp_acp_signaling, test_006_reconfigure)
{
	run_twice(reconfigure_cmd);
}

static void abort_cmd(void)
{
	prepare();
	establish();

	/* The abort of the unknown endpoint is not answered, and not reported. */
	test_expect(EVT_ABORT_REQ);
	test_expect(EVT_RELEASED);
}

ZTEST(a2dp_acp_signaling, test_007_abort)
{
	run_twice(abort_cmd);
}

static void get_configuration_cmd(void)
{
	prepare();

	test_expect(EVT_GET_CONFIG_REQ);
}

ZTEST(a2dp_acp_signaling, test_008_get_configuration)
{
	run_twice(get_configuration_cmd);
}

static void get_mtu(void)
{
	uint32_t mtu;

	prepare();
	establish();

	mtu = bt_a2dp_get_mtu(&test_stream);
	zassert_equal(mtu, BT_L2CAP_RX_MTU, "Media MTU %u, expected %u", mtu, BT_L2CAP_RX_MTU);
}

ZTEST(a2dp_acp_signaling, test_009_get_mtu)
{
	run_twice(get_mtu);
}

static void delay_report_cmd(void)
{
	struct test_event evt;

	prepare();

	if (LOCAL_TSEP == BT_AVDTP_SINK) {
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

	test_br_sync();
}

ZTEST(a2dp_acp_signaling, test_010_delay_report)
{
	run_twice(delay_report_cmd);
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

ZTEST_SUITE(a2dp_acp_signaling, NULL, setup, before, NULL, teardown);
