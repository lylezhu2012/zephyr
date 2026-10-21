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
#include "test_ag.h"
#include "test_hf.h"

static void hf_call_op(int (*op)(struct bt_hfp_hf_call *call), struct bt_hfp_hf_call *call,
		       const char *name)
{
	int err = op(call);

	zassert_equal(err, 0, "Failed to %s call (err %d)", name, err);
}

static void hf_expect_dialing(bool success)
{
	struct test_event evt;

	TEST_EXPECT(EVT_DIALING, TEST_HFP_TIMEOUT, &evt);
	if (success) {
		zassert_equal(evt.value, 0, "Dialing failed (err %d)", evt.value);
	} else {
		zassert_not_equal(evt.value, 0, "Dialing not refused");
	}
}

ZTEST(hfp_hf_call, test_001_psi_007_ag)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_wait();

	call = hf_wait_ringing_call(TEST_NUMBER_1);
	hf_expect_call(EVT_REJECT, call);

	call = hf_wait_ringing_call(TEST_NUMBER_1);
	hf_expect_call(EVT_ACCEPT, call);
	hf_expect_call(EVT_TERMINATE, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_002_psi_007_hf)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_incoming(TEST_NUMBER_PSI);
	hf_call_op(bt_hfp_hf_reject, call, "reject");
	hf_expect_call(EVT_REJECT, call);

	call = hf_wait_incoming(TEST_NUMBER_PSI);
	hf_call_op(bt_hfp_hf_accept, call, "accept");
	hf_expect_call(EVT_ACCEPT, call);

	hf_call_op(bt_hfp_hf_terminate, call, "terminate");
	hf_expect_call(EVT_TERMINATE, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_003_aic_001)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_incoming(TEST_NUMBER_1);
	hf_call_op(bt_hfp_hf_accept, call, "accept");
	hf_expect_call(EVT_ACCEPT, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_004_aic_002)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_ringing_call(TEST_NUMBER_1);
	hf_expect_call(EVT_ACCEPT, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_005_ric_001)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_incoming(TEST_NUMBER_1);
	hf_call_op(bt_hfp_hf_reject, call, "reject");
	hf_expect_call(EVT_REJECT, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_006_ric_002)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_ringing_call(TEST_NUMBER_1);
	hf_expect_call(EVT_REJECT, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_007_tc_001)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_ringing_call(TEST_NUMBER_1);
	hf_expect_call(EVT_ACCEPT, call);

	hf_call_op(bt_hfp_hf_terminate, call, "terminate");
	hf_expect_call(EVT_TERMINATE, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_008_tc_002)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_ringing_call(TEST_NUMBER_1);
	hf_expect_call(EVT_ACCEPT, call);
	hf_expect_call(EVT_TERMINATE, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_009_act_001)
{
	test_br_sync();
	hf_slc_connect();

	hf_wait_sco_connected(TEST_AIR_MODE_ANY);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_010_act_002_ag)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_incoming(NULL);
	hf_wait_sco_connected(TEST_AIR_MODE_ANY);
	test_br_sync();
	hf_expect_call(EVT_ACCEPT, call);
	hf_wait_sco_disconnected();
	hf_expect_call(EVT_TERMINATE, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_011_act_002_hf)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_incoming(NULL);
	hf_wait_sco_connected(TEST_AIR_MODE_ANY);

	hf_call_op(bt_hfp_hf_accept, call, "accept");
	hf_expect_call(EVT_ACCEPT, call);

	hf_sco_disconnect();

	hf_call_op(bt_hfp_hf_terminate, call, "terminate");
	hf_expect_call(EVT_TERMINATE, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_012_pcn_001)
{
	int err;

	test_br_sync();
	hf_slc_connect();

	err = bt_hfp_hf_number_call(hf, TEST_NUMBER_1);
	zassert_equal(err, 0, "Failed to dial (err %d)", err);
	hf_expect_dialing(false);

	err = bt_hfp_hf_number_call(hf, TEST_AG_DIAL_NUMBER);
	zassert_equal(err, 0, "Failed to dial (err %d)", err);
	hf_expect_dialing(true);
	(void)hf_wait_call(EVT_OUTGOING);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_013_pmd_001)
{
	int err;

	test_br_sync();
	hf_slc_connect();

	err = bt_hfp_hf_memory_dial(hf, "5");
	zassert_equal(err, 0, "Failed to dial (err %d)", err);
	hf_expect_dialing(false);

	err = bt_hfp_hf_memory_dial(hf, TEST_AG_MEMORY_LOCATION);
	zassert_equal(err, 0, "Failed to dial (err %d)", err);
	hf_expect_dialing(true);
	(void)hf_wait_call(EVT_OUTGOING);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_014_pln_001)
{
	int err;

	test_br_sync();
	hf_slc_connect();

	err = bt_hfp_hf_redial(hf);
	zassert_equal(err, 0, "Failed to redial (err %d)", err);
	hf_expect_dialing(true);
	(void)hf_wait_call(EVT_OUTGOING);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_015_cli_001)
{
	struct bt_hfp_hf_call *call;
	int err;

	test_br_sync();
	hf_slc_connect();

	err = bt_hfp_hf_cli(hf, false);
	zassert_equal(err, 0, "Failed to disable CLI (err %d)", err);
	k_sleep(TEST_AT_DELIVERY_TIME);

	test_br_sync();

	/* RING is sent together with +CLIP when CLI is enabled. */
	call = hf_wait_call(EVT_INCOMING);
	hf_expect_call(EVT_RING_INDICATION, call);
	zassert_false(test_evt_pending(EVT_CLIP), "CLIP while CLI is disabled");

	hf_call_op(bt_hfp_hf_reject, call, "reject");
	hf_expect_call(EVT_REJECT, call);

	err = bt_hfp_hf_cli(hf, true);
	zassert_equal(err, 0, "Failed to enable CLI (err %d)", err);
	k_sleep(TEST_AT_DELIVERY_TIME);

	test_br_sync();

	(void)hf_wait_ringing_call(TEST_NUMBER_1);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_016_dtmf_001_ag)
{
	struct bt_hfp_hf_call *call;
	int err;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_ringing_call(TEST_NUMBER_1);
	hf_expect_call(EVT_ACCEPT, call);

	err = bt_hfp_hf_transmit_dtmf_code(call, '3');
	zassert_equal(err, 0, "Failed to send DTMF code (err %d)", err);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_017_dtmf_001_hf)
{
	struct bt_hfp_hf_call *call;
	int err;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_incoming(TEST_NUMBER_1);
	hf_call_op(bt_hfp_hf_accept, call, "accept");
	hf_expect_call(EVT_ACCEPT, call);

	err = bt_hfp_hf_transmit_dtmf_code(call, '3');
	zassert_equal(err, 0, "Failed to send DTMF code (err %d)", err);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_018_rah_001)
{
	struct bt_hfp_hf_call *call;
	int err;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_ringing_call(TEST_NUMBER_1);
	hf_expect_call(EVT_INCOMING_HELD, call);

	/*
	 * Queried while the AG still sends the indicators that follow +BTRH. An indicator received
	 * before the response must not release the SLC.
	 */
	err = bt_hfp_hf_query_respond_hold_status(hf);
	zassert_equal(err, 0, "Failed to query response and hold status (err %d)", err);
	test_evt_none(EVT_DISCONNECTED, TEST_AT_DELIVERY_TIME);

	/* The response reports the held call only if it is not preceded by an indicator. */
	(void)test_evt_wait_obj(EVT_INCOMING_HELD, call, K_NO_WAIT, NULL);

	/* Queried again once the indicators are received: the response reports the held call. */
	err = bt_hfp_hf_query_respond_hold_status(hf);
	zassert_equal(err, 0, "Failed to query response and hold status (err %d)", err);
	hf_expect_call(EVT_INCOMING_HELD, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_019_rah_002)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_incoming(TEST_NUMBER_1);
	hf_call_op(bt_hfp_hf_hold_incoming, call, "hold");
	hf_expect_call(EVT_INCOMING_HELD, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_020_rah_003)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_ringing_call(TEST_NUMBER_1);
	hf_expect_call(EVT_INCOMING_HELD, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_021_rah_004)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_incoming(TEST_NUMBER_1);
	hf_call_op(bt_hfp_hf_hold_incoming, call, "hold");
	hf_expect_call(EVT_INCOMING_HELD, call);

	hf_call_op(bt_hfp_hf_accept, call, "accept");
	hf_expect_call(EVT_ACCEPT, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_022_rah_005)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_ringing_call(TEST_NUMBER_1);
	hf_expect_call(EVT_INCOMING_HELD, call);
	hf_expect_call(EVT_ACCEPT, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_023_rah_006)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_incoming(TEST_NUMBER_1);
	hf_call_op(bt_hfp_hf_hold_incoming, call, "hold");
	hf_expect_call(EVT_INCOMING_HELD, call);

	hf_call_op(bt_hfp_hf_reject, call, "reject");
	hf_expect_call(EVT_REJECT, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_024_rah_007)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_ringing_call(TEST_NUMBER_1);
	hf_expect_call(EVT_INCOMING_HELD, call);
	hf_expect_call(EVT_REJECT, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_025_rah_008)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_ringing_call(TEST_NUMBER_1);
	hf_expect_call(EVT_INCOMING_HELD, call);
	hf_expect_call(EVT_REJECT, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_026_ecs_001_ag)
{
	test_br_sync();
	hf_slc_wait();

	(void)hf_wait_call(EVT_ACCEPT);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_027_inband_ringtone)
{
	struct bt_hfp_hf_call *call;
	int err;

	test_br_sync();
	hf_slc_connect();

	/* The SLC setup reports the in-band ring tone first. */
	err = test_evt_wait_value(EVT_INBAND_RING, 0, TEST_HFP_TIMEOUT, NULL);
	zassert_equal(err, 0, "In-band ring tone not disabled (err %d)", err);

	call = hf_wait_call(EVT_INCOMING);
	hf_expect_call(EVT_RING_INDICATION, call);
	zassert_is_null(hf_sco, "SCO connection while ringing");

	hf_call_op(bt_hfp_hf_accept, call, "accept");
	hf_expect_call(EVT_ACCEPT, call);
	hf_wait_sco_connected(TEST_AIR_MODE_ANY);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_028_ag_outgoing)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_call(EVT_OUTGOING);
	hf_expect_call(EVT_REMOTE_RINGING, call);
	hf_expect_call(EVT_ACCEPT, call);
	hf_expect_call(EVT_TERMINATE, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_029_ag_outgoing_rejected)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_call(EVT_OUTGOING);
	hf_expect_call(EVT_REMOTE_RINGING, call);
	hf_expect_call(EVT_REJECT, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_030_rah_query_no_held_call)
{
	int err;

	test_br_sync();
	hf_slc_connect();

	/* Without a held call, the AG answers AT+BTRH? with OK only. */
	err = bt_hfp_hf_query_respond_hold_status(hf);
	zassert_equal(err, 0, "Failed to query response and hold status (err %d)", err);
	test_evt_none(EVT_INCOMING_HELD, TEST_AT_DELIVERY_TIME);

	test_br_sync();

	/* An indicator received after the query must not release the SLC. */
	hf_expect_value(EVT_BATTERY, 1);
	test_evt_none(EVT_DISCONNECTED, TEST_AT_DELIVERY_TIME);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_031_rah_query_then_hold)
{
	struct bt_hfp_hf_call *call;
	int err;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_ringing_call(TEST_NUMBER_1);

	/*
	 * Without a held call, the AG answers AT+BTRH? with OK only. RING and +CLIP are repeated
	 * every CONFIG_BT_HFP_AG_RING_NOTIFY_INTERVAL seconds, so the +BTRH sent when the AG holds
	 * the call is the next result code.
	 */
	err = bt_hfp_hf_query_respond_hold_status(hf);
	zassert_equal(err, 0, "Failed to query response and hold status (err %d)", err);
	test_evt_none(EVT_INCOMING_HELD, TEST_AT_DELIVERY_TIME);

	test_br_sync();

	/* The call is reported as held, not as accepted. */
	hf_expect_call(EVT_INCOMING_HELD, call);
	test_evt_none(EVT_ACCEPT, TEST_AT_DELIVERY_TIME);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_032_ag_reject_twice)
{
	struct bt_hfp_hf_call *call;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_ringing_call(TEST_NUMBER_1);
	hf_expect_call(EVT_REJECT, call);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_033_terminate_then_query)
{
	struct bt_hfp_hf_call *call;
	int err;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_ringing_call(TEST_NUMBER_1);
	hf_expect_call(EVT_ACCEPT, call);

	/* AT+CLCC is sent right after the OK of AT+CHUP, maybe before the AG ends the call. */
	hf_call_op(bt_hfp_hf_terminate, call, "terminate");
	err = bt_hfp_hf_query_list_of_current_calls(hf);
	zassert_equal(err, 0, "Failed to query current calls (err %d)", err);
	hf_expect_call(EVT_TERMINATE, call);

	/* The terminated call is not reported again. */
	test_evt_none(EVT_INCOMING, TEST_AT_DELIVERY_TIME);
	test_evt_none(EVT_ACCEPT, K_NO_WAIT);
	test_evt_none(EVT_TERMINATE, K_NO_WAIT);

	test_br_sync();
}

ZTEST(hfp_hf_call, test_034_rah_reject_then_query)
{
	struct bt_hfp_hf_call *call;
	int err;

	test_br_sync();
	hf_slc_connect();

	call = hf_wait_incoming(TEST_NUMBER_1);
	hf_call_op(bt_hfp_hf_hold_incoming, call, "hold");
	hf_expect_call(EVT_INCOMING_HELD, call);

	/* AT+CLCC is sent right after the OK of AT+BTRH=2, maybe before the AG ends the call. */
	hf_call_op(bt_hfp_hf_reject, call, "reject");
	err = bt_hfp_hf_query_list_of_current_calls(hf);
	zassert_equal(err, 0, "Failed to query current calls (err %d)", err);
	hf_expect_call(EVT_REJECT, call);

	/* The rejected call is not reported again. */
	test_evt_none(EVT_INCOMING, TEST_AT_DELIVERY_TIME);
	test_evt_none(EVT_ACCEPT, K_NO_WAIT);
	test_evt_none(EVT_TERMINATE, K_NO_WAIT);
	test_evt_none(EVT_REJECT, K_NO_WAIT);

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

ZTEST_SUITE(hfp_hf_call, NULL, setup, before, after, teardown);
