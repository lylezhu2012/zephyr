/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Robustness tests of the acceptor (device 1), counterpart of test_int_robustness.c. */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/classic/a2dp.h>
#include <zephyr/bluetooth/classic/avdtp.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"

static void fragmentation(void)
{
	test_br_sync();

	test_acp_wait_connected();
	test_avdtp_set_tx_mtu(TEST_FRAG_MTU);

	test_br_sync();

	test_expect_rsp(EVT_CONFIG_REQ, BT_AVDTP_SUCCESS);
	test_expect(EVT_CONFIGURED);

	test_br_sync();
}

ZTEST(a2dp_acp_robustness, test_001_fragmentation_sink_int)
{
	fragmentation();
}

ZTEST(a2dp_acp_robustness, test_002_fragmentation_source_int)
{
	fragmentation();
}

ZTEST(a2dp_acp_robustness, test_003_random_procedure)
{
	test_br_sync();

	/* Every request of the random commands is accepted. */
	test_acp_wait_connected();

	test_br_sync();

	/* Drops the events of the random commands. */
	test_wait_disconnected();
	test_br_sync();

	test_acp_wait_connected();
	test_expect_rsp(EVT_CONFIG_REQ, BT_AVDTP_SUCCESS);
	test_expect(EVT_CONFIGURED);
	test_expect(EVT_ESTABLISH_REQ);
	test_expect(EVT_ESTABLISHED);
	test_expect(EVT_START_REQ);
	test_expect(EVT_STARTED);
	test_expect(EVT_SUSPEND_REQ);
	test_expect(EVT_SUSPENDED);
	test_expect(EVT_RELEASE_REQ);
	test_expect(EVT_RELEASED);

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

ZTEST_SUITE(a2dp_acp_robustness, NULL, setup, before, NULL, teardown);
