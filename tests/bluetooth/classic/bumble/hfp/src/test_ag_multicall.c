/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/classic/hfp_ag.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"
#include "test_ag.h"

static void ag_call_op(int (*op)(struct bt_hfp_ag_call *call), struct bt_hfp_ag_call *call,
		       const char *name)
{
	int err = op(call);

	zassert_equal(err, 0, "Failed to %s call (err %d)", name, err);
}

/* First call, accepted by the HF. */
static struct bt_hfp_ag_call *ag_active_call(void)
{
	struct bt_hfp_ag_call *call = ag_incoming(TEST_NUMBER_1);

	ag_expect_call(EVT_ACCEPT, call);

	return call;
}

/* First call, accepted and held by the AG. */
static struct bt_hfp_ag_call *ag_held_call(void)
{
	struct bt_hfp_ag_call *call = ag_incoming(TEST_NUMBER_1);

	ag_expect_call(EVT_RINGING, call);

	/* The HF has seen the call ringing. */
	test_br_sync();

	ag_call_op(bt_hfp_ag_accept, call, "accept");
	ag_expect_call(EVT_ACCEPT, call);

	ag_call_op(bt_hfp_ag_hold, call, "hold");
	ag_expect_call(EVT_HELD, call);

	return call;
}

/* b2b: CWN_001 - call waiting notification enabled by the HF. */
ZTEST(hfp_ag_multicall, test_001_cwn_001)
{
	test_br_sync();
	ag_slc_wait();

	/* The HF has enabled the call waiting notification. */
	test_br_sync();

	(void)ag_active_call();
	(void)ag_incoming(TEST_NUMBER_WAITING);

	test_br_sync();
}

/* b2b: 3WC_001 - active call held and waiting call accepted by the HF (AT+CHLD=2). */
ZTEST(hfp_ag_multicall, test_002_3wc_001)
{
	struct bt_hfp_ag_call *call1;
	struct bt_hfp_ag_call *call2;

	test_br_sync();
	ag_slc_wait();

	call1 = ag_active_call();
	call2 = ag_incoming(TEST_NUMBER_WAITING);

	ag_expect_call(EVT_HELD, call1);
	ag_expect_call(EVT_ACCEPT, call2);

	test_br_sync();
}

/* b2b: 3WC_002 - call held by the HF, then a second outgoing call of the AG. */
ZTEST(hfp_ag_multicall, test_003_3wc_002)
{
	struct bt_hfp_ag_call *call1;
	struct bt_hfp_ag_call *call2;
	int err;

	test_br_sync();
	ag_slc_wait();

	call1 = ag_active_call();
	ag_expect_call(EVT_HELD, call1);

	err = bt_hfp_ag_outgoing(ag, TEST_NUMBER_WAITING);
	zassert_equal(err, 0, "Failed to dial (err %d)", err);
	call2 = ag_wait_call(EVT_OUTGOING);

	ag_call_op(bt_hfp_ag_remote_ringing, call2, "ring");
	ag_expect_call(EVT_RINGING, call2);
	ag_call_op(bt_hfp_ag_remote_accept, call2, "accept");
	ag_expect_call(EVT_ACCEPT, call2);

	test_br_sync();
}

/* b2b: 3WC_003 - calls joined and transferred by the HF (AT+CHLD=3, AT+CHLD=4). */
ZTEST(hfp_ag_multicall, test_004_3wc_003)
{
	struct bt_hfp_ag_call *call1;
	struct bt_hfp_ag_call *call2;

	test_br_sync();
	ag_slc_wait();

	call1 = ag_active_call();
	call2 = ag_incoming(TEST_NUMBER_WAITING);

	ag_expect_call(EVT_HELD, call1);
	ag_expect_call(EVT_ACCEPT, call2);

	/* The HF joins the calls into a conversation. */
	test_br_sync();

	TEST_EXPECT(EVT_EXPLICIT_CALL_TRANSFER, TEST_HFP_TIMEOUT, NULL);
	ag_wait_no_sco();

	test_br_sync();
}

/* b2b: ECS_001 (HF) - list of current calls queried by the HF. */
ZTEST(hfp_ag_multicall, test_005_ecs_001_hf)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_incoming(TEST_NUMBER_1);
	ag_expect_call(EVT_RINGING, call);
	test_br_sync();

	ag_call_op(bt_hfp_ag_accept, call, "accept");
	ag_expect_call(EVT_ACCEPT, call);

	(void)ag_incoming(TEST_NUMBER_2);

	/* The HF queries the list of current calls. */
	test_br_sync();
	test_br_sync();
}

/* b2b: ECS_002 - held and waiting calls released by index (AT+CHLD=1<idx>). */
ZTEST(hfp_ag_multicall, test_006_ecs_002)
{
	struct bt_hfp_ag_call *call1;
	struct bt_hfp_ag_call *call2;

	test_br_sync();
	ag_slc_wait();

	call1 = ag_held_call();
	call2 = ag_incoming(TEST_NUMBER_2);

	/* The HF has seen the waiting call. */
	test_br_sync();

	ag_expect_call(EVT_TERMINATE, call1);
	ag_expect_call(EVT_REJECT, call2);

	test_br_sync();
}

/* Held call released by the HF (AT+CHLD=0). */
ZTEST(hfp_ag_multicall, test_007_release_all_held)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_held_call();

	/* The HF has seen the held call. */
	test_br_sync();

	ag_expect_call(EVT_TERMINATE, call);

	test_br_sync();
}

/* Waiting call rejected by the HF with User Determined User Busy (AT+CHLD=0). */
ZTEST(hfp_ag_multicall, test_008_udub)
{
	struct bt_hfp_ag_call *call2;

	test_br_sync();
	ag_slc_wait();

	(void)ag_active_call();
	call2 = ag_incoming(TEST_NUMBER_2);

	ag_expect_call(EVT_REJECT, call2);
	test_evt_none(EVT_TERMINATE, TEST_AT_DELIVERY_TIME);

	test_br_sync();
}

/* Active call released and waiting call accepted by the HF (AT+CHLD=1). */
ZTEST(hfp_ag_multicall, test_009_release_active_accept_other)
{
	struct bt_hfp_ag_call *call1;
	struct bt_hfp_ag_call *call2;

	test_br_sync();
	ag_slc_wait();

	call1 = ag_active_call();
	call2 = ag_incoming(TEST_NUMBER_2);

	ag_expect_call(EVT_TERMINATE, call1);
	ag_expect_call(EVT_ACCEPT, call2);

	test_br_sync();
}

/* Call held and retrieved by the AG. */
ZTEST(hfp_ag_multicall, test_010_ag_hold_retrieve)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_held_call();

	ag_call_op(bt_hfp_ag_retrieve, call, "retrieve");
	ag_expect_call(EVT_RETRIEVE, call);

	test_br_sync();
}

/* Explicit call transfer by the AG, after the held call is retrieved into the conversation. */
ZTEST(hfp_ag_multicall, test_011_ag_explicit_call_transfer)
{
	struct bt_hfp_ag_call *call1;
	struct bt_hfp_ag_call *call2;
	int err;

	test_br_sync();
	ag_slc_wait();

	call1 = ag_held_call();

	/* A second incoming call is accepted while the first one is held. */
	call2 = ag_incoming(TEST_NUMBER_2);
	ag_call_op(bt_hfp_ag_accept, call2, "accept");
	ag_expect_call(EVT_ACCEPT, call2);

	/* The AG transfers the calls only when all of them are active. */
	ag_call_op(bt_hfp_ag_retrieve, call1, "retrieve");
	ag_expect_call(EVT_RETRIEVE, call1);

	/* The HF has seen both calls active. */
	test_br_sync();

	err = bt_hfp_ag_explicit_call_transfer(ag);
	zassert_equal(err, 0, "Failed to transfer calls (err %d)", err);
	TEST_EXPECT(EVT_EXPLICIT_CALL_TRANSFER, TEST_HFP_TIMEOUT, NULL);
	ag_wait_no_sco();

	test_br_sync();
}

/* UDUB (AT+CHLD=0) with a held and a waiting call: only the waiting call is released. */
ZTEST(hfp_ag_multicall, test_012_udub_with_held_call)
{
	struct bt_hfp_ag_call *call1;
	struct bt_hfp_ag_call *call2;

	test_br_sync();
	ag_slc_wait();

	call1 = ag_held_call();
	call2 = ag_incoming(TEST_NUMBER_2);

	ag_expect_call(EVT_REJECT, call2);
	test_evt_none(EVT_TERMINATE, TEST_AT_DELIVERY_TIME);

	/* The HF has seen the waiting call rejected. */
	test_br_sync();

	/* The held call still exists. */
	ag_call_op(bt_hfp_ag_retrieve, call1, "retrieve");
	ag_expect_call(EVT_RETRIEVE, call1);

	test_br_sync();
}

/* A waiting call after UDUB of a previous waiting call, with an active call. */
ZTEST(hfp_ag_multicall, test_013_udub_then_waiting)
{
	struct bt_hfp_ag_call *call2;

	test_br_sync();
	ag_slc_wait();

	(void)ag_active_call();
	call2 = ag_incoming(TEST_NUMBER_2);
	ag_expect_call(EVT_REJECT, call2);

	/* The HF has seen the waiting call rejected. */
	test_br_sync();

	(void)ag_incoming(TEST_NUMBER_WAITING);

	/* The HF has seen the second waiting call. */
	test_br_sync();
}

/*
 * UDUB of a waiting call, with an active call, and a second waiting call started as soon as the
 * AG reports the first one rejected.
 */
ZTEST(hfp_ag_multicall, test_014_udub_then_waiting_at_once)
{
	struct bt_hfp_ag_call *call2;

	test_br_sync();
	ag_slc_wait();

	(void)ag_active_call();
	call2 = ag_incoming(TEST_NUMBER_2);
	ag_expect_call(EVT_REJECT, call2);

	/* No sync with the HF in between. */
	(void)ag_incoming(TEST_NUMBER_WAITING);

	/* The HF has seen the second waiting call. */
	test_br_sync();
}

/* A waiting call released by index (AT+CHLD=1<idx>) with an active call, then a second one. */
ZTEST(hfp_ag_multicall, test_015_release_waiting_by_index)
{
	struct bt_hfp_ag_call *call2;

	test_br_sync();
	ag_slc_wait();

	(void)ag_active_call();
	call2 = ag_incoming(TEST_NUMBER_2);
	ag_expect_call(EVT_REJECT, call2);

	/* The HF has seen the waiting call released. */
	test_br_sync();

	(void)ag_incoming(TEST_NUMBER_WAITING);

	/* The HF has seen the second waiting call. */
	test_br_sync();
}

/* UDUB of a waiting call, with an active call, and the current calls queried right after it. */
ZTEST(hfp_ag_multicall, test_016_udub_then_query)
{
	struct bt_hfp_ag_call *call2;

	test_br_sync();
	ag_slc_wait();

	(void)ag_active_call();
	call2 = ag_incoming(TEST_NUMBER_2);
	ag_expect_call(EVT_REJECT, call2);

	/* The HF has seen the waiting call rejected once. */
	test_br_sync();
}

/*
 * Waiting call rejected by the AG, with an active call, then a second waiting call. The HF has
 * no AT command pending: it learns the end of the waiting call from +CIEV (callsetup 0) and its
 * own AT+CLCC.
 */
ZTEST(hfp_ag_multicall, test_017_ag_reject_waiting)
{
	struct bt_hfp_ag_call *call2;

	test_br_sync();
	ag_slc_wait();

	(void)ag_active_call();
	call2 = ag_incoming(TEST_NUMBER_2);

	/* The HF has seen the waiting call. */
	test_br_sync();

	ag_call_op(bt_hfp_ag_reject, call2, "reject");
	ag_expect_call(EVT_REJECT, call2);

	/* The HF has seen the waiting call rejected. */
	test_br_sync();

	(void)ag_incoming(TEST_NUMBER_WAITING);

	/* The HF has seen the second waiting call. */
	test_br_sync();
}

static void *setup(void)
{
	ag_suite_setup();
	return NULL;
}

static void teardown(void *f)
{
	ag_suite_teardown();
}

static void before(void *f)
{
	ag_test_before();
}

static void after(void *f)
{
	ag_test_after();
}

ZTEST_SUITE(hfp_ag_multicall, NULL, setup, before, after, teardown);
