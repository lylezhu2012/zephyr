/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/bluetooth/classic/hfp_ag.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"
#include "test_ag.h"

/* Notify an incoming call and wait until it is ringing on the HF. */
static struct bt_hfp_ag_call *ag_ringing_call(const char *number)
{
	struct bt_hfp_ag_call *call = ag_incoming(number);

	ag_expect_call(EVT_RINGING, call);

	/* Act on the call once the HF has seen it ringing (RING and +CLIP). */
	test_br_sync();

	return call;
}

static void ag_call_op(int (*op)(struct bt_hfp_ag_call *call), struct bt_hfp_ag_call *call,
		       const char *name)
{
	int err = op(call);

	zassert_equal(err, 0, "Failed to %s call (err %d)", name, err);
}

/* b2b: PSI_007 (AG) - call status of a call rejected, accepted and terminated by the AG. */
ZTEST(hfp_ag_call, test_001_psi_007_ag)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_connect();

	call = ag_ringing_call(TEST_NUMBER_1);
	ag_call_op(bt_hfp_ag_reject, call, "reject");
	ag_expect_call(EVT_REJECT, call);

	call = ag_ringing_call(TEST_NUMBER_1);
	ag_call_op(bt_hfp_ag_accept, call, "accept");
	ag_expect_call(EVT_ACCEPT, call);

	ag_call_op(bt_hfp_ag_terminate, call, "terminate");
	ag_expect_call(EVT_TERMINATE, call);
	ag_wait_no_sco();

	test_br_sync();
}

/* b2b: PSI_007 (HF) - call status of a call rejected, accepted and terminated by the HF. */
ZTEST(hfp_ag_call, test_002_psi_007_hf)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_incoming(TEST_NUMBER_PSI);
	ag_expect_call(EVT_REJECT, call);

	call = ag_incoming(TEST_NUMBER_PSI);
	ag_expect_call(EVT_ACCEPT, call);
	ag_expect_call(EVT_TERMINATE, call);
	ag_wait_no_sco();

	test_br_sync();
}

/* b2b: AIC_001 - incoming call accepted by the HF. */
ZTEST(hfp_ag_call, test_003_aic_001)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_incoming(TEST_NUMBER_1);
	ag_expect_call(EVT_ACCEPT, call);

	test_br_sync();
}

/* b2b: AIC_002 - incoming call accepted by the AG. */
ZTEST(hfp_ag_call, test_004_aic_002)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_ringing_call(TEST_NUMBER_1);
	ag_call_op(bt_hfp_ag_accept, call, "accept");
	ag_expect_call(EVT_ACCEPT, call);

	test_br_sync();
}

/* b2b: RIC_001 - incoming call rejected by the HF. */
ZTEST(hfp_ag_call, test_005_ric_001)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_incoming(TEST_NUMBER_1);
	ag_expect_call(EVT_REJECT, call);

	test_br_sync();
}

/* b2b: RIC_002 - incoming call rejected by the AG. */
ZTEST(hfp_ag_call, test_006_ric_002)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_ringing_call(TEST_NUMBER_1);
	ag_call_op(bt_hfp_ag_reject, call, "reject");
	ag_expect_call(EVT_REJECT, call);

	test_br_sync();
}

/* b2b: TC_001 - call accepted by the AG, terminated by the HF. */
ZTEST(hfp_ag_call, test_007_tc_001)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_ringing_call(TEST_NUMBER_1);
	ag_call_op(bt_hfp_ag_accept, call, "accept");
	ag_expect_call(EVT_ACCEPT, call);
	ag_expect_call(EVT_TERMINATE, call);

	test_br_sync();
}

/* b2b: TC_002 - call accepted and terminated by the AG. */
ZTEST(hfp_ag_call, test_008_tc_002)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_ringing_call(TEST_NUMBER_1);
	ag_call_op(bt_hfp_ag_accept, call, "accept");
	ag_expect_call(EVT_ACCEPT, call);

	ag_call_op(bt_hfp_ag_terminate, call, "terminate");
	ag_expect_call(EVT_TERMINATE, call);

	test_br_sync();
}

/* b2b: ACT_001 - audio connection of an incoming call with in-band ring tone. */
ZTEST(hfp_ag_call, test_009_act_001)
{
	test_br_sync();
	ag_slc_wait();

	(void)ag_incoming(TEST_NUMBER_1);
	ag_wait_sco_connected(TEST_AIR_MODE_ANY);

	test_br_sync();
}

/* b2b: ACT_002 (AG) - audio released by the AG during a call, then the AG terminates. */
ZTEST(hfp_ag_call, test_010_act_002_ag)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_incoming(TEST_NUMBER_1);
	ag_wait_sco_connected(TEST_AIR_MODE_ANY);
	ag_expect_call(EVT_RINGING, call);
	test_br_sync();

	ag_call_op(bt_hfp_ag_accept, call, "accept");
	ag_expect_call(EVT_ACCEPT, call);

	ag_sco_disconnect();

	ag_call_op(bt_hfp_ag_terminate, call, "terminate");
	ag_expect_call(EVT_TERMINATE, call);

	test_br_sync();
}

/* b2b: ACT_002 (HF) - audio released by the HF during a call, then the HF terminates. */
ZTEST(hfp_ag_call, test_011_act_002_hf)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_incoming(TEST_NUMBER_1);
	ag_wait_sco_connected(TEST_AIR_MODE_ANY);
	ag_expect_call(EVT_ACCEPT, call);
	ag_wait_sco_disconnected();
	ag_expect_call(EVT_TERMINATE, call);

	test_br_sync();
}

/* b2b: PCN_001 - phone number call from the HF, refused for an unknown number. */
ZTEST(hfp_ag_call, test_012_pcn_001)
{
	struct test_event evt;

	test_br_sync();
	ag_slc_wait();

	TEST_EXPECT(EVT_NUMBER_CALL, TEST_HFP_TIMEOUT, &evt);
	zassert_str_equal(evt.str, TEST_NUMBER_1, "Unexpected number %s", evt.str);

	TEST_EXPECT(EVT_NUMBER_CALL, TEST_HFP_TIMEOUT, &evt);
	zassert_str_equal(evt.str, TEST_AG_DIAL_NUMBER, "Unexpected number %s", evt.str);

	TEST_EXPECT(EVT_OUTGOING, TEST_HFP_TIMEOUT, &evt);
	zassert_str_equal(evt.str, TEST_AG_DIAL_NUMBER, "Unexpected number %s", evt.str);

	test_br_sync();
}

/* b2b: PMD_001 - memory dialing from the HF, refused for an unknown location. */
ZTEST(hfp_ag_call, test_013_pmd_001)
{
	struct test_event evt;

	test_br_sync();
	ag_slc_wait();

	TEST_EXPECT(EVT_MEMORY_DIAL, TEST_HFP_TIMEOUT, &evt);
	zassert_str_equal(evt.str, "5", "Unexpected location %s", evt.str);

	TEST_EXPECT(EVT_MEMORY_DIAL, TEST_HFP_TIMEOUT, &evt);
	zassert_str_equal(evt.str, TEST_AG_MEMORY_LOCATION, "Unexpected location %s", evt.str);

	TEST_EXPECT(EVT_OUTGOING, TEST_HFP_TIMEOUT, &evt);
	zassert_str_equal(evt.str, TEST_AG_DIAL_NUMBER, "Unexpected number %s", evt.str);

	test_br_sync();
}

/* b2b: PLN_001 - last number redial from the HF. */
ZTEST(hfp_ag_call, test_014_pln_001)
{
	struct test_event evt;

	strcpy(ag_cfg.last_number, TEST_NUMBER_LAST);

	test_br_sync();
	ag_slc_wait();

	TEST_EXPECT(EVT_REDIAL, TEST_HFP_TIMEOUT, NULL);

	TEST_EXPECT(EVT_OUTGOING, TEST_HFP_TIMEOUT, &evt);
	zassert_str_equal(evt.str, TEST_NUMBER_LAST, "Unexpected number %s", evt.str);

	test_br_sync();
}

/* b2b: CLI_001 - calling line identification disabled and enabled by the HF. */
ZTEST(hfp_ag_call, test_015_cli_001)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	/* The HF has disabled the CLIP notification. */
	test_br_sync();

	call = ag_incoming(TEST_NUMBER_1);
	ag_expect_call(EVT_REJECT, call);

	/* The HF has enabled the CLIP notification. */
	test_br_sync();

	(void)ag_ringing_call(TEST_NUMBER_1);

	test_br_sync();
}

/* b2b: DTMF_001 (AG) - DTMF code sent by the HF during a call accepted by the AG. */
ZTEST(hfp_ag_call, test_016_dtmf_001_ag)
{
	struct bt_hfp_ag_call *call;
	struct test_event evt;

	test_br_sync();
	ag_slc_wait();

	call = ag_ringing_call(TEST_NUMBER_1);
	ag_call_op(bt_hfp_ag_accept, call, "accept");
	ag_expect_call(EVT_ACCEPT, call);

	TEST_EXPECT(EVT_DTMF_CODE, TEST_HFP_TIMEOUT, &evt);
	zassert_equal(evt.value, '3', "Unexpected DTMF code %d", evt.value);

	test_br_sync();
}

/* b2b: DTMF_001 (HF) - DTMF code sent by the HF during a call accepted by the HF. */
ZTEST(hfp_ag_call, test_017_dtmf_001_hf)
{
	struct bt_hfp_ag_call *call;
	struct test_event evt;

	test_br_sync();
	ag_slc_wait();

	call = ag_incoming(TEST_NUMBER_1);
	ag_expect_call(EVT_ACCEPT, call);

	TEST_EXPECT(EVT_DTMF_CODE, TEST_HFP_TIMEOUT, &evt);
	zassert_equal(evt.value, '3', "Unexpected DTMF code %d", evt.value);

	test_br_sync();
}

/* b2b: RAH_001 - incoming call held by the AG, status queried by the HF. */
ZTEST(hfp_ag_call, test_018_rah_001)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_ringing_call(TEST_NUMBER_1);
	ag_call_op(bt_hfp_ag_hold_incoming, call, "hold");
	ag_expect_call(EVT_INCOMING_HELD, call);

	/* The HF queries the response and hold status twice. */
	test_br_sync();
	zassert_not_null(ag, "SLC released");
}

/* b2b: RAH_002 - incoming call held by the HF. */
ZTEST(hfp_ag_call, test_019_rah_002)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_incoming(TEST_NUMBER_1);
	ag_expect_call(EVT_INCOMING_HELD, call);

	test_br_sync();
}

/* b2b: RAH_003 - incoming call held by the AG. */
ZTEST(hfp_ag_call, test_020_rah_003)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_ringing_call(TEST_NUMBER_1);
	ag_call_op(bt_hfp_ag_hold_incoming, call, "hold");
	ag_expect_call(EVT_INCOMING_HELD, call);

	test_br_sync();
}

/* b2b: RAH_004 - incoming call held and then accepted by the HF. */
ZTEST(hfp_ag_call, test_021_rah_004)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_incoming(TEST_NUMBER_1);
	ag_expect_call(EVT_INCOMING_HELD, call);
	ag_expect_call(EVT_ACCEPT, call);

	test_br_sync();
}

/* b2b: RAH_005 - incoming call held and then accepted by the AG. */
ZTEST(hfp_ag_call, test_022_rah_005)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_ringing_call(TEST_NUMBER_1);
	ag_call_op(bt_hfp_ag_hold_incoming, call, "hold");
	ag_expect_call(EVT_INCOMING_HELD, call);

	ag_call_op(bt_hfp_ag_accept, call, "accept");
	ag_expect_call(EVT_ACCEPT, call);

	test_br_sync();
}

/* b2b: RAH_006 - incoming call held and then rejected by the HF. */
ZTEST(hfp_ag_call, test_023_rah_006)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_incoming(TEST_NUMBER_1);
	ag_expect_call(EVT_INCOMING_HELD, call);
	ag_expect_call(EVT_REJECT, call);

	test_br_sync();
}

/* b2b: RAH_007 - incoming call held and then rejected by the AG. */
ZTEST(hfp_ag_call, test_024_rah_007)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_ringing_call(TEST_NUMBER_1);
	ag_call_op(bt_hfp_ag_hold_incoming, call, "hold");
	ag_expect_call(EVT_INCOMING_HELD, call);

	ag_call_op(bt_hfp_ag_reject, call, "reject");
	ag_expect_call(EVT_REJECT, call);

	test_br_sync();
}

/* b2b: RAH_008 - held incoming call terminated by the remote party. */
ZTEST(hfp_ag_call, test_025_rah_008)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_ringing_call(TEST_NUMBER_1);
	ag_call_op(bt_hfp_ag_hold_incoming, call, "hold");
	ag_expect_call(EVT_INCOMING_HELD, call);

	ag_call_op(bt_hfp_ag_remote_terminate, call, "terminate");
	ag_wait_no_sco();

	test_br_sync();
}

/* b2b: ECS_001 (AG) - active call reported by the AG while the SLC is established. */
ZTEST(hfp_ag_call, test_026_ecs_001_ag)
{
	struct bt_hfp_ag_ongoing_call ongoing = {
		.number = TEST_NUMBER_ONGOING,
		.type = 129,
		.dir = BT_HFP_AG_CALL_DIR_OUTGOING,
		.status = BT_HFP_AG_CALL_STATUS_ACTIVE,
	};
	struct bt_hfp_ag *new_ag = NULL;
	struct test_event evt;
	uint8_t channel;
	int err;

	ag_cfg.ongoing_calls = true;

	test_br_sync();

	channel = test_sdp_get_rfcomm_channel(test_br_conn(), BT_SDP_HANDSFREE_SVCLASS);
	err = bt_hfp_ag_connect(test_br_conn(), &new_ag, channel);
	zassert_equal(err, 0, "Failed to connect AG (err %d)", err);

	/* The ongoing calls are requested when the HF reads the indicators. */
	TEST_EXPECT(EVT_GET_ONGOING_CALL, TEST_HFP_TIMEOUT, &evt);
	err = bt_hfp_ag_ongoing_calls(evt.obj, &ongoing, 1);
	zassert_equal(err, 0, "Failed to set ongoing calls (err %d)", err);

	TEST_EXPECT(EVT_CONNECTED, TEST_HFP_TIMEOUT, NULL);
	(void)ag_wait_call(EVT_ACCEPT);

	test_br_sync();
}

/* In-band ring tone disabled by the AG; the audio is set up once the HF accepts. */
ZTEST(hfp_ag_call, test_027_inband_ringtone)
{
	struct bt_hfp_ag_call *call;
	struct test_event evt;
	int err;

	test_br_sync();
	ag_slc_wait();

	err = bt_hfp_ag_inband_ringtone(ag, false);
	zassert_equal(err, 0, "Failed to disable in-band ring tone (err %d)", err);

	call = ag_incoming(TEST_NUMBER_1);
	TEST_EXPECT(EVT_RINGING, TEST_HFP_CALL_TIMEOUT, &evt);
	zassert_equal_ptr(evt.obj, call, "Ringing for another call");
	zassert_equal(evt.value, 0, "In-band ringing");
	zassert_is_null(ag_sco, "SCO connection while ringing");

	ag_expect_call(EVT_ACCEPT, call);
	ag_wait_sco_connected(TEST_AIR_MODE_ANY);

	test_br_sync();
}

/* Outgoing call of the AG, alerting and accepted by the remote party, then terminated by it. */
ZTEST(hfp_ag_call, test_028_ag_outgoing)
{
	struct bt_hfp_ag_call *call;
	int err;

	test_br_sync();
	ag_slc_wait();

	err = bt_hfp_ag_outgoing(ag, TEST_NUMBER_2);
	zassert_equal(err, 0, "Failed to dial (err %d)", err);
	call = ag_wait_call(EVT_OUTGOING);

	ag_call_op(bt_hfp_ag_remote_ringing, call, "ring");
	ag_expect_call(EVT_RINGING, call);
	ag_call_op(bt_hfp_ag_remote_accept, call, "accept");
	ag_expect_call(EVT_ACCEPT, call);

	ag_call_op(bt_hfp_ag_remote_terminate, call, "terminate");
	ag_expect_call(EVT_TERMINATE, call);

	test_br_sync();
}

/* Outgoing call of the AG rejected by the remote party. */
ZTEST(hfp_ag_call, test_029_ag_outgoing_rejected)
{
	struct bt_hfp_ag_call *call;
	int err;

	test_br_sync();
	ag_slc_wait();

	err = bt_hfp_ag_outgoing(ag, TEST_NUMBER_2);
	zassert_equal(err, 0, "Failed to dial (err %d)", err);
	call = ag_wait_call(EVT_OUTGOING);

	ag_call_op(bt_hfp_ag_remote_ringing, call, "ring");
	ag_expect_call(EVT_RINGING, call);
	ag_call_op(bt_hfp_ag_remote_reject, call, "reject");
	ag_expect_call(EVT_REJECT, call);

	test_br_sync();
}

/* Response and hold status queried by the HF without a held call, then an indicator update. */
ZTEST(hfp_ag_call, test_030_rah_query_no_held_call)
{
	int err;

	test_br_sync();
	ag_slc_wait();

	/* The HF has queried the response and hold status. */
	test_br_sync();

	err = bt_hfp_ag_battery_level(ag, 1);
	zassert_equal(err, 0, "Failed to set battery level (err %d)", err);

	test_br_sync();
	zassert_not_null(ag, "SLC released");
}

/* Incoming call held by the AG right after a response and hold status query of the HF. */
ZTEST(hfp_ag_call, test_031_rah_query_then_hold)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_ringing_call(TEST_NUMBER_1);

	/* The HF has queried the response and hold status. */
	test_br_sync();

	ag_call_op(bt_hfp_ag_hold_incoming, call, "hold");
	ag_expect_call(EVT_INCOMING_HELD, call);

	test_br_sync();
}

/*
 * Incoming call rejected twice by the AG. The second request is made before the callsetup
 * indicator of the first one is sent; the call is reported rejected once.
 */
ZTEST(hfp_ag_call, test_032_ag_reject_twice)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_ringing_call(TEST_NUMBER_1);
	ag_call_op(bt_hfp_ag_reject, call, "reject");
	/*
	 * The callsetup indicator is cleared already: the second request rejects the call at once
	 * and returns an error. Its result is not checked.
	 */
	(void)bt_hfp_ag_reject(call);

	ag_expect_call(EVT_REJECT, call);
	test_evt_none(EVT_REJECT, TEST_AT_DELIVERY_TIME);

	test_br_sync();
}

/* Call terminated by the HF (AT+CHUP), the current calls queried by the HF right after it. */
ZTEST(hfp_ag_call, test_033_terminate_then_query)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_ringing_call(TEST_NUMBER_1);
	ag_call_op(bt_hfp_ag_accept, call, "accept");
	ag_expect_call(EVT_ACCEPT, call);
	ag_expect_call(EVT_TERMINATE, call);

	/* The HF has seen the call terminated once. */
	test_br_sync();
}

/*
 * Incoming call held, then rejected by the HF (AT+BTRH=2), the current calls queried by the HF
 * right after it.
 */
ZTEST(hfp_ag_call, test_034_rah_reject_then_query)
{
	struct bt_hfp_ag_call *call;

	test_br_sync();
	ag_slc_wait();

	call = ag_incoming(TEST_NUMBER_1);
	ag_expect_call(EVT_INCOMING_HELD, call);
	ag_expect_call(EVT_REJECT, call);

	/* The HF has seen the call rejected once. */
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

ZTEST_SUITE(hfp_ag_call, NULL, setup, before, after, teardown);
