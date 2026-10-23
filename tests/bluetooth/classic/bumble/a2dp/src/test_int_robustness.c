/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Robustness tests of the initiator (device 0): signaling with fragmented AVDTP messages in both
 * directions (board-to-board fragmentation tests of a2dp_sink and a2dp_source) and a random
 * sequence of stream commands (board-to-board a2dp_sink random_procedure).
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/classic/a2dp.h>
#include <zephyr/bluetooth/classic/a2dp_codec_sbc.h>
#include <zephyr/bluetooth/classic/avdtp.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"

LOG_MODULE_REGISTER(test_a2dp_int_robustness, CONFIG_TEST_CLASSIC_A2DP_SIM_LOG_LEVEL);

static void fragmentation(enum bt_avdtp_sep_type tsep)
{
	test_br_sync();

	test_int_connect();
	test_avdtp_set_tx_mtu(TEST_FRAG_MTU);

	/* The acceptor has set its transmit MTU too. */
	test_br_sync();

	test_int_discover(tsep, tsep == BT_AVDTP_SOURCE, 0U);
	test_int_config(tsep, A2DP_SBC_SAMP_FREQ_44100, TEST_MIN_BITPOOL, false, BT_AVDTP_SUCCESS);

	test_br_sync();
}

/* b2b: a2dp_sink fragmentation - AVDTP transmit MTU of 4 on both sides. */
ZTEST(a2dp_int_robustness, test_001_fragmentation_sink_int)
{
	fragmentation(BT_AVDTP_SINK);
}

/* b2b: a2dp_source fragmentation */
ZTEST(a2dp_int_robustness, test_002_fragmentation_source_int)
{
	fragmentation(BT_AVDTP_SOURCE);
}

enum random_cmd {
	CMD_CONFIGURE,
	CMD_ESTABLISH,
	CMD_RECONFIGURE,
	CMD_RELEASE,
	CMD_START,
	CMD_SUSPEND,
	CMD_ABORT,
	CMD_GET_CONFIG,
	CMD_GET_MTU,
	CMD_DELAY_REPORT,

	CMD_COUNT,
};

static uint32_t rand_state;

/* Fixed sequence, so that a failure can be reproduced. */
static uint32_t rand_next(void)
{
	rand_state = (rand_state * 1103515245U) + 12345U;

	return rand_state >> 16;
}

static void random_cmd(enum random_cmd cmd)
{
	struct test_event evt;
	enum test_evt rsp;
	int err;

	switch (cmd) {
	case CMD_CONFIGURE:
		err = test_int_config_send(BT_AVDTP_SINK, A2DP_SBC_SAMP_FREQ_44100,
					   TEST_MIN_BITPOOL, false);
		rsp = EVT_CONFIG_RSP;
		break;
	case CMD_ESTABLISH:
		err = bt_a2dp_stream_establish(&test_stream);
		rsp = EVT_ESTABLISH_RSP;
		break;
	case CMD_RECONFIGURE:
		err = test_int_reconfig_send(A2DP_SBC_SAMP_FREQ_44100, TEST_MIN_BITPOOL_RECONF);
		rsp = EVT_CONFIG_RSP;
		break;
	case CMD_RELEASE:
		err = bt_a2dp_stream_release(&test_stream);
		rsp = EVT_RELEASE_RSP;
		break;
	case CMD_START:
		err = bt_a2dp_stream_start(&test_stream);
		rsp = EVT_START_RSP;
		break;
	case CMD_SUSPEND:
		err = bt_a2dp_stream_suspend(&test_stream);
		rsp = EVT_SUSPEND_RSP;
		break;
	case CMD_ABORT:
		err = bt_a2dp_stream_abort(&test_stream);
		rsp = EVT_ABORT_RSP;
		break;
	case CMD_GET_CONFIG:
		err = bt_a2dp_stream_get_config(&test_stream);
		rsp = EVT_GET_CONFIG_RSP;
		break;
	case CMD_GET_MTU:
		LOG_DBG("Media MTU %u", bt_a2dp_get_mtu(&test_stream));
		return;
	case CMD_DELAY_REPORT:
	default:
		err = bt_a2dp_stream_delay_report(&test_stream, TEST_DELAY_REPORT);
		rsp = EVT_DELAY_REPORT_RSP;
		break;
	}

	if (err != 0) {
		LOG_DBG("Command %u not sent (err %d)", cmd, err);
		return;
	}

	/* Responses of the commands are not checked, only that the procedure goes on. */
	err = test_evt_wait(rsp, TEST_A2DP_TIMEOUT, &evt);
	zassert_not_equal(err, -ENOTCONN, "ACL link lost");
	if (err != 0) {
		LOG_WRN("No %s for command %u", test_evt_str(rsp), cmd);
	} else {
		LOG_DBG("Command %u: %s 0x%02x", cmd, test_evt_str(rsp), evt.value);
	}
}

/*
 * b2b: a2dp_sink random_procedure - 50 random stream commands; then a complete stream procedure
 * on a new A2DP connection still works.
 */
ZTEST(a2dp_int_robustness, test_003_random_procedure)
{
	test_br_sync();

	test_int_connect();
	test_int_discover(BT_AVDTP_SINK, false, 0U);

	rand_state = 0x0a2d9U;
	for (uint32_t i = 0; i < TEST_RANDOM_STEPS; i++) {
		/* Only the events of the current command are waited for. */
		test_evt_reset();
		random_cmd((enum random_cmd)(rand_next() % CMD_COUNT));
	}

	/* The acceptor has seen all commands. */
	test_br_sync();

	test_int_disconnect();

	/* The acceptor has seen the A2DP connection released. */
	test_br_sync();

	test_int_prepare(BT_AVDTP_SINK);
	test_stream_cmd(bt_a2dp_stream_establish, EVT_ESTABLISH_RSP, BT_AVDTP_SUCCESS);
	test_expect(EVT_ESTABLISHED);
	test_stream_cmd(bt_a2dp_stream_start, EVT_START_RSP, BT_AVDTP_SUCCESS);
	test_expect(EVT_STARTED);
	test_stream_cmd(bt_a2dp_stream_suspend, EVT_SUSPEND_RSP, BT_AVDTP_SUCCESS);
	test_expect(EVT_SUSPENDED);
	test_stream_cmd(bt_a2dp_stream_release, EVT_RELEASE_RSP, BT_AVDTP_SUCCESS);
	test_expect(EVT_RELEASED);

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

ZTEST_SUITE(a2dp_int_robustness, NULL, setup, before, NULL, teardown);
