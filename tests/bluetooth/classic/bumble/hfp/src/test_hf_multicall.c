/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/classic/hfp_hf.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"
#include "test_hf.h"

static void hf_call_op(int (*op)(struct bt_hfp_hf_call *call), struct bt_hfp_hf_call *call,
		       const char *name)
{
	int err = op(call);

	zassert_equal(err, 0, "Failed to %s call (err %d)", name, err);
}

static void hf_op(int (*op)(struct bt_hfp_hf *hf), const char *name)
{
	int err = op(hf);

	zassert_equal(err, 0, "Failed to %s (err %d)", name, err);
}

/* Counterpart of ag_active_call(). */
static struct bt_hfp_hf_call *hf_active_call(void)
{
	struct bt_hfp_hf_call *call = hf_wait_incoming(TEST_NUMBER_1);

	hf_call_op(bt_hfp_hf_accept, call, "accept");
	hf_expect_call(EVT_ACCEPT, call);

	return call;
}

/* Counterpart of ag_held_call(). */
static struct bt_hfp_hf_call *hf_held_call(void)
{
	struct bt_hfp_hf_call *call = hf_wait_ringing_call(TEST_NUMBER_1);

	hf_expect_call(EVT_ACCEPT, call);
	hf_expect_call(EVT_HELD, call);

	return call;
}

ZTEST(hfp_hf_multicall, test_001_cwn_001)
{
	struct test_event evt;
	int err;

	test_br_sync();
	hf_slc_connect();

	err = bt_hfp_hf_call_waiting_notify(hf, true);
	zassert_equal(err, 0, "Failed to enable call waiting notification (err %d)", err);
	k_sleep(TEST_AT_DELIVERY_TIME);

	test_br_sync();

	(void)hf_active_call();

	TEST_EXPECT(EVT_CALL_WAITING, TEST_HFP_CALL_TIMEOUT, &evt);
	zassert_str_equal(evt.str, TEST_NUMBER_WAITING, "Unexpected number %s", evt.str);

	test_br_sync();
}

ZTEST(hfp_hf_multicall, test_002_3wc_001)
{
	struct bt_hfp_hf_call *call1;
	struct bt_hfp_hf_call *call2;

	test_br_sync();
	hf_slc_connect();

	call1 = hf_active_call();
	call2 = hf_wait_call(EVT_INCOMING);

	hf_op(bt_hfp_hf_hold_active_accept_other, "hold active and accept other");
	hf_expect_call(EVT_HELD, call1);
	hf_expect_call(EVT_ACCEPT, call2);

	test_br_sync();
}

ZTEST(hfp_hf_multicall, test_003_3wc_002)
{
	struct bt_hfp_hf_call *call1;
	struct bt_hfp_hf_call *call2;

	test_br_sync();
	hf_slc_connect();

	call1 = hf_active_call();

	hf_op(bt_hfp_hf_hold_active_accept_other, "hold active call");
	hf_expect_call(EVT_HELD, call1);

	call2 = hf_wait_call(EVT_OUTGOING);
	hf_expect_call(EVT_REMOTE_RINGING, call2);
	hf_expect_call(EVT_ACCEPT, call2);

	test_br_sync();
}

ZTEST(hfp_hf_multicall, test_004_3wc_003)
{
	struct bt_hfp_hf_call *call1;
	struct bt_hfp_hf_call *call2;

	test_br_sync();
	hf_slc_connect();

	call1 = hf_active_call();
	call2 = hf_wait_call(EVT_INCOMING);

	hf_op(bt_hfp_hf_hold_active_accept_other, "hold active and accept other");
	hf_expect_call(EVT_HELD, call1);
	hf_expect_call(EVT_ACCEPT, call2);

	hf_op(bt_hfp_hf_join_conversation, "join the calls");
	hf_expect_call(EVT_RETRIEVE, call1);

	test_br_sync();

	hf_op(bt_hfp_hf_explicit_call_transfer, "transfer the calls");
	hf_wait_no_sco();

	test_br_sync();
}

ZTEST(hfp_hf_multicall, test_005_ecs_001_hf)
{
	struct bt_hfp_hf_call *call;
	struct test_event evt;
	uint32_t seen = 0;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_ringing_call(TEST_NUMBER_1);
	hf_expect_call(EVT_ACCEPT, call);
	(void)hf_wait_call(EVT_INCOMING);

	test_br_sync();

	/* Results of earlier queries made by the stack itself are not relevant. */
	while (test_evt_wait(EVT_QUERY_CALL, K_NO_WAIT, NULL) == 0) {
	}

	hf_op(bt_hfp_hf_query_list_of_current_calls, "query current calls");

	while (seen != (BIT(1) | BIT(2))) {
		TEST_EXPECT(EVT_QUERY_CALL, TEST_HFP_TIMEOUT, &evt);

		if (evt.value == 1) {
			zassert_equal(TEST_QUERY_CALL_STATUS(evt.value2),
				      BT_HFP_HF_CALL_STATUS_ACTIVE, "Unexpected status of call 1");
			zassert_str_equal(evt.str, TEST_NUMBER_1, "Unexpected number %s", evt.str);
		} else if (evt.value == 2) {
			zassert_equal(TEST_QUERY_CALL_DIR(evt.value2), BT_HFP_HF_CALL_DIR_INCOMING,
				      "Unexpected direction of call 2");
			zassert_equal(TEST_QUERY_CALL_STATUS(evt.value2),
				      BT_HFP_HF_CALL_STATUS_WAITING, "Unexpected status of call 2");
			zassert_str_equal(evt.str, TEST_NUMBER_2, "Unexpected number %s", evt.str);
		} else {
			zassert_unreachable("Unexpected call index %d", evt.value);
		}

		seen |= BIT(evt.value);
	}

	test_br_sync();
}

ZTEST(hfp_hf_multicall, test_006_ecs_002)
{
	struct bt_hfp_hf_call *call1;
	struct bt_hfp_hf_call *call2;

	test_br_sync();
	hf_slc_connect();

	call1 = hf_held_call();
	call2 = hf_wait_call(EVT_INCOMING);

	test_br_sync();

	hf_call_op(bt_hfp_hf_release_specified_call, call1, "release");
	hf_expect_call(EVT_TERMINATE, call1);

	hf_call_op(bt_hfp_hf_release_specified_call, call2, "release");
	hf_expect_call(EVT_REJECT, call2);

	test_br_sync();
}

ZTEST(hfp_hf_multicall, test_007_release_all_held)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_held_call();

	test_br_sync();

	hf_op(bt_hfp_hf_release_all_held, "release held calls");
	hf_expect_call(EVT_TERMINATE, call);

	test_br_sync();
}

ZTEST(hfp_hf_multicall, test_008_udub)
{
	struct bt_hfp_hf_call *call2;

	test_br_sync();
	hf_slc_connect();

	(void)hf_active_call();
	call2 = hf_wait_call(EVT_INCOMING);

	hf_op(bt_hfp_hf_set_udub, "set UDUB");
	hf_expect_call(EVT_REJECT, call2);

	test_br_sync();
}

ZTEST(hfp_hf_multicall, test_009_release_active_accept_other)
{
	struct bt_hfp_hf_call *call1;
	struct bt_hfp_hf_call *call2;

	test_br_sync();
	hf_slc_connect();

	call1 = hf_active_call();
	call2 = hf_wait_call(EVT_INCOMING);

	hf_op(bt_hfp_hf_release_active_accept_other, "release active and accept other");
	hf_expect_call(EVT_TERMINATE, call1);
	hf_expect_call(EVT_ACCEPT, call2);

	test_br_sync();
}

ZTEST(hfp_hf_multicall, test_010_ag_hold_retrieve)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_held_call();
	hf_expect_call(EVT_RETRIEVE, call);

	test_br_sync();
}

ZTEST(hfp_hf_multicall, test_011_ag_explicit_call_transfer)
{
	struct bt_hfp_hf_call *call1;
	struct bt_hfp_hf_call *call2;

	test_br_sync();
	hf_slc_connect();

	call1 = hf_held_call();
	call2 = hf_wait_call(EVT_INCOMING);
	hf_expect_call(EVT_ACCEPT, call2);
	hf_expect_call(EVT_RETRIEVE, call1);

	test_br_sync();

	/* The AG ends both calls after the transfer. */
	hf_expect_call(EVT_TERMINATE, call1);
	hf_expect_call(EVT_TERMINATE, call2);
	hf_wait_no_sco();

	test_br_sync();
}

ZTEST(hfp_hf_multicall, test_012_udub_with_held_call)
{
	struct bt_hfp_hf_call *call1;
	struct bt_hfp_hf_call *call2;

	test_br_sync();
	hf_slc_connect();

	call1 = hf_held_call();
	call2 = hf_wait_call(EVT_INCOMING);

	/* With a held and a waiting call, AT+CHLD=0 applies to the waiting call only. */
	hf_op(bt_hfp_hf_set_udub, "set UDUB");
	hf_expect_call(EVT_REJECT, call2);
	test_evt_none(EVT_TERMINATE, TEST_AT_DELIVERY_TIME);

	test_br_sync();

	/* The AG retrieves the held call. */
	hf_expect_call(EVT_RETRIEVE, call1);

	test_br_sync();
}

ZTEST(hfp_hf_multicall, test_013_udub_then_waiting)
{
	struct bt_hfp_hf_call *call2;
	struct test_event evt;

	test_br_sync();
	hf_slc_connect();

	(void)hf_active_call();
	call2 = hf_wait_call(EVT_INCOMING);
	TEST_EXPECT(EVT_CALL_WAITING, TEST_HFP_TIMEOUT, &evt);
	zassert_str_equal(evt.str, TEST_NUMBER_2, "Unexpected number %s", evt.str);

	hf_op(bt_hfp_hf_set_udub, "set UDUB");
	hf_expect_call(EVT_REJECT, call2);

	test_br_sync();

	/* The next waiting call is reported once the AG sets callsetup again. */
	(void)hf_wait_call(EVT_INCOMING);
	TEST_EXPECT(EVT_CALL_WAITING, TEST_HFP_TIMEOUT, &evt);
	zassert_str_equal(evt.str, TEST_NUMBER_WAITING, "Unexpected number %s", evt.str);

	test_br_sync();
}

ZTEST(hfp_hf_multicall, test_014_udub_then_waiting_at_once)
{
	struct bt_hfp_hf_call *call2;
	struct test_event evt;

	test_br_sync();
	hf_slc_connect();

	(void)hf_active_call();
	call2 = hf_wait_call(EVT_INCOMING);
	TEST_EXPECT(EVT_CALL_WAITING, TEST_HFP_TIMEOUT, &evt);
	zassert_str_equal(evt.str, TEST_NUMBER_2, "Unexpected number %s", evt.str);

	hf_op(bt_hfp_hf_set_udub, "set UDUB");
	hf_expect_call(EVT_REJECT, call2);

	/* The AG starts the next waiting call right after it has rejected the first one. */
	(void)hf_wait_call(EVT_INCOMING);
	TEST_EXPECT(EVT_CALL_WAITING, TEST_HFP_TIMEOUT, &evt);
	zassert_str_equal(evt.str, TEST_NUMBER_WAITING, "Unexpected number %s", evt.str);

	test_br_sync();
}

ZTEST(hfp_hf_multicall, test_015_release_waiting_by_index)
{
	struct bt_hfp_hf_call *call2;
	struct test_event evt;
	int err;

	test_br_sync();
	hf_slc_connect();

	(void)hf_active_call();
	call2 = hf_wait_call(EVT_INCOMING);
	TEST_EXPECT(EVT_CALL_WAITING, TEST_HFP_TIMEOUT, &evt);
	zassert_str_equal(evt.str, TEST_NUMBER_2, "Unexpected number %s", evt.str);

	/* The index of the waiting call is known once the current calls are listed. */
	hf_op(bt_hfp_hf_query_list_of_current_calls, "query current calls");
	err = test_evt_wait_value(EVT_QUERY_CALL, 2, TEST_HFP_TIMEOUT, NULL);
	zassert_equal(err, 0, "Waiting call not listed (err %d)", err);

	hf_call_op(bt_hfp_hf_release_specified_call, call2, "release");
	hf_expect_call(EVT_REJECT, call2);

	test_br_sync();

	/* The next waiting call is reported once the AG sets callsetup again. */
	(void)hf_wait_call(EVT_INCOMING);
	TEST_EXPECT(EVT_CALL_WAITING, TEST_HFP_TIMEOUT, &evt);
	zassert_str_equal(evt.str, TEST_NUMBER_WAITING, "Unexpected number %s", evt.str);

	test_br_sync();
}

ZTEST(hfp_hf_multicall, test_016_udub_then_query)
{
	struct bt_hfp_hf_call *call2;

	test_br_sync();
	hf_slc_connect();

	(void)hf_active_call();
	call2 = hf_wait_call(EVT_INCOMING);

	/* AT+CLCC is sent right after the OK of AT+CHLD=0, maybe before the AG ends the call. */
	hf_op(bt_hfp_hf_set_udub, "set UDUB");
	hf_op(bt_hfp_hf_query_list_of_current_calls, "query current calls");
	hf_expect_call(EVT_REJECT, call2);

	/* The rejected call is not reported again. */
	test_evt_none(EVT_INCOMING, TEST_AT_DELIVERY_TIME);
	test_evt_none(EVT_REJECT, K_NO_WAIT);

	test_br_sync();
}

ZTEST(hfp_hf_multicall, test_017_ag_reject_waiting)
{
	struct bt_hfp_hf_call *call2;
	struct test_event evt;

	test_br_sync();
	hf_slc_connect();

	(void)hf_active_call();
	call2 = hf_wait_call(EVT_INCOMING);
	TEST_EXPECT(EVT_CALL_WAITING, TEST_HFP_TIMEOUT, &evt);
	zassert_str_equal(evt.str, TEST_NUMBER_2, "Unexpected number %s", evt.str);

	test_br_sync();

	/* The AG rejects the waiting call, the active call is not affected. */
	hf_expect_call(EVT_REJECT, call2);
	test_evt_none(EVT_TERMINATE, K_NO_WAIT);

	test_br_sync();

	/* The next waiting call gets a call object. */
	(void)hf_wait_call(EVT_INCOMING);
	TEST_EXPECT(EVT_CALL_WAITING, TEST_HFP_TIMEOUT, &evt);
	zassert_str_equal(evt.str, TEST_NUMBER_WAITING, "Unexpected number %s", evt.str);

	test_br_sync();
}

static void *setup(void)
{
	hf_suite_setup();
	return NULL;
}

static void teardown(void *f)
{
	hf_suite_teardown();
}

static void before(void *f)
{
	hf_test_before();
}

static void after(void *f)
{
	hf_test_after();
}

ZTEST_SUITE(hfp_hf_multicall, NULL, setup, before, after, teardown);
