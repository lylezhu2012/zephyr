/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/classic/hfp_hf.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"
#include "test_ag.h"
#include "test_hf.h"

LOG_MODULE_REGISTER(test_hfp_hf_slc, CONFIG_TEST_CLASSIC_HFP_SIM_LOG_LEVEL);

/* Bit of the battery level in the AT+BIA indicator bitmap, see bt_hfp_hf_indicator_status(). */
#define TEST_IND_BATTERY_BIT BIT(6)

ZTEST(hfp_hf_slc, test_001_cm_001_ag)
{
	test_br_sync();
	hf_slc_wait();
	test_br_sync();
}

ZTEST(hfp_hf_slc, test_002_cm_001_hf)
{
	test_br_sync();
	hf_slc_connect();
	test_br_sync();
}

ZTEST(hfp_hf_slc, test_003_cm_003_ag)
{
	test_br_sync();
	hf_slc_wait();
	hf_slc_wait_disconnected();
	test_br_sync();
}

ZTEST(hfp_hf_slc, test_004_cm_003_hf)
{
	test_br_sync();
	hf_slc_connect();
	hf_slc_disconnect();
	test_br_sync();
}

ZTEST(hfp_hf_slc, test_005_cm_004_ag)
{
	test_br_sync();
	hf_slc_wait();
	hf_slc_disconnect();
	test_br_sync();
}

ZTEST(hfp_hf_slc, test_006_cm_004_hf)
{
	test_br_sync();
	hf_slc_connect();
	hf_slc_wait_disconnected();
	test_br_sync();
}

ZTEST(hfp_hf_slc, test_007_psi_001)
{
	test_br_sync();
	hf_slc_connect();

	hf_expect_value(EVT_SERVICE, 0);
	hf_expect_value(EVT_SERVICE, 1);

	test_br_sync();
}

ZTEST(hfp_hf_slc, test_008_psi_002)
{
	test_br_sync();
	hf_slc_connect();

	hf_expect_value(EVT_SIGNAL, 2);

	test_br_sync();
	zassert_false(test_evt_pending(EVT_SIGNAL), "Unexpected signal indicator");
}

ZTEST(hfp_hf_slc, test_009_psi_003)
{
	test_br_sync();
	hf_slc_connect();

	hf_expect_value(EVT_ROAM, 1);

	test_br_sync();
}

ZTEST(hfp_hf_slc, test_010_psi_004)
{
	test_br_sync();
	hf_slc_connect();

	hf_expect_value(EVT_BATTERY, 1);

	test_br_sync();
}

/*
 * The HF stack sends AT+COPS=3,0 itself after the SLC is established, and the AG answers
 * AT+COPS? with an error result code until it has received it. bt_hfp_hf_cb has no callback for
 * that result, so the read is retried until +COPS is received.
 */
ZTEST(hfp_hf_slc, test_011_psi_005)
{
	struct test_event evt;
	int err = -EAGAIN;

	test_br_sync();
	hf_slc_connect();

	/* The AG has set the operator. */
	test_br_sync();

	for (int i = 0; i < 3; i++) {
		int ret = bt_hfp_hf_get_operator(hf);

		zassert_equal(ret, 0, "Failed to get operator (err %d)", ret);

		err = test_evt_wait(EVT_OPERATOR, K_SECONDS(3), &evt);
		if (err == 0) {
			break;
		}
		LOG_WRN("No +COPS received (attempt %d)", i + 1);
	}

	zassert_equal(err, 0, "Operator not received (err %d)", err);
	zassert_equal(evt.value, 1, "Unexpected mode %d", evt.value);
	zassert_equal(evt.value2, 0, "Unexpected format %d", evt.value2);
	zassert_str_equal(evt.str, TEST_OPERATOR_NAME, "Unexpected operator %s", evt.str);

	test_br_sync();
}

ZTEST(hfp_hf_slc, test_012_ravc_001)
{
	int err;

	test_br_sync();
	hf_slc_connect();

	err = bt_hfp_hf_vgm(hf, 16);
	zassert_equal(err, -EINVAL, "VGM 16 returned %d", err);

	err = bt_hfp_hf_vgm(hf, 3);
	zassert_equal(err, 0, "Failed to send VGM (err %d)", err);

	hf_expect_value(EVT_VGM, 5);

	test_br_sync();
}

ZTEST(hfp_hf_slc, test_013_ravc_002)
{
	int err;

	test_br_sync();
	hf_slc_connect();

	err = bt_hfp_hf_vgs(hf, 16);
	zassert_equal(err, -EINVAL, "VGS 16 returned %d", err);

	err = bt_hfp_hf_vgs(hf, 3);
	zassert_equal(err, 0, "Failed to send VGS (err %d)", err);

	hf_expect_value(EVT_VGS, 7);

	test_br_sync();
}

ZTEST(hfp_hf_slc, test_014_ecnr_001)
{
	int err;

	test_br_sync();
	hf_slc_connect();

	err = bt_hfp_hf_turn_off_ecnr(hf);
	zassert_equal(err, 0, "Failed to turn off ECNR (err %d)", err);

	hf_expect_value(EVT_ECNR_TURN_OFF, 0);

	test_br_sync();
}

ZTEST(hfp_hf_slc, test_015_vra_001)
{
	int err;

	test_br_sync();
	hf_slc_connect();

	err = bt_hfp_hf_voice_recognition(hf, true);
	zassert_equal(err, 0, "Failed to activate voice recognition (err %d)", err);
	hf_expect_value(EVT_VOICE_RECOGNITION, 1);

	err = bt_hfp_hf_voice_recognition(hf, false);
	zassert_equal(err, 0, "Failed to deactivate voice recognition (err %d)", err);
	hf_expect_value(EVT_VOICE_RECOGNITION, 0);

	test_br_sync();
}

ZTEST(hfp_hf_slc, test_016_vra_ag)
{
	struct test_event evt;
	int err;

	test_br_sync();
	hf_slc_connect();

	hf_expect_value(EVT_VOICE_RECOGNITION, 1);
	hf_wait_sco_connected(TEST_AIR_MODE_CVSD);

	/* The activation reports engine state 0, then the AG sets it. */
	err = test_evt_wait_value(EVT_VRE_STATE, BIT(0), TEST_HFP_TIMEOUT, NULL);
	zassert_equal(err, 0, "No engine state 1 (err %d)", err);

	err = bt_hfp_hf_ready_to_accept_audio(hf);
	zassert_equal(err, 0, "Failed to send ready to accept audio (err %d)", err);

	TEST_EXPECT(EVT_TEXTUAL_REPRESENTATION, TEST_HFP_TIMEOUT, &evt);
	zassert_str_equal(evt.str, "1:Hello", "Unexpected text %s", evt.str);
	zassert_equal(evt.value, 1, "Unexpected text type %d", evt.value);
	zassert_equal(evt.value2, 1, "Unexpected text operation %d", evt.value2);

	test_br_sync();

	hf_expect_value(EVT_VOICE_RECOGNITION, 0);

	test_br_sync();
}

ZTEST(hfp_hf_slc, test_017_vt_001)
{
	struct test_event evt;
	int err;

	test_br_sync();
	hf_slc_connect();

	for (int i = 0; i < 4; i++) {
		/*
		 * A number is reported when +BINP is received, so the next request is sent before
		 * the final result code that follows it.
		 */
		err = bt_hfp_hf_request_phone_number(hf);
		zassert_equal(err, 0, "Failed to request phone number (err %d)", err);

		TEST_EXPECT(EVT_REQUEST_PHONE_NUMBER, TEST_HFP_TIMEOUT, &evt);
		if ((i % 2) == 0) {
			zassert_equal(evt.value, -1, "Request %d not refused", i);
		} else {
			zassert_equal(evt.value, 0, "Request %d refused", i);
			zassert_str_equal(evt.str, TEST_AG_VOICE_TAG_NUMBER, "Unexpected number %s",
					  evt.str);
		}
	}

	/* Each request is reported once. */
	test_evt_none(EVT_REQUEST_PHONE_NUMBER, TEST_AT_DELIVERY_TIME);

	test_br_sync();
}

ZTEST(hfp_hf_slc, test_018_sni_001)
{
	struct test_event evt;
	int err;

	test_br_sync();
	hf_slc_connect();

	err = bt_hfp_hf_query_subscriber(hf);
	zassert_equal(err, 0, "Failed to query subscriber (err %d)", err);

	TEST_EXPECT(EVT_SUBSCRIBER_NUMBER, TEST_HFP_TIMEOUT, &evt);
	zassert_str_equal(evt.str, TEST_AG_SUBSCRIBER_NUMBER, "Unexpected number %s", evt.str);
	zassert_equal(evt.value, TEST_AG_SUBSCRIBER_TYPE, "Unexpected type %d", evt.value);
	zassert_equal(evt.value2, TEST_AG_SUBSCRIBER_SERVICE, "Unexpected service %d", evt.value2);

	test_br_sync();
}

ZTEST(hfp_hf_slc, test_019_iia_001)
{
	int err;

	test_br_sync();
	hf_slc_connect();

	err = bt_hfp_hf_indicator_status(hf, TEST_IND_BATTERY_BIT);
	zassert_equal(err, 0, "Failed to set indicator status (err %d)", err);
	k_sleep(TEST_AT_DELIVERY_TIME);

	test_br_sync();

	/* The AG sends the signal strength first; it must not be reported. */
	hf_expect_value(EVT_BATTERY, 1);
	zassert_false(test_evt_pending(EVT_SIGNAL), "Deactivated signal indicator reported");

	test_br_sync();
}

ZTEST(hfp_hf_slc, test_020_hfi_001)
{
	int err;

	test_br_sync();
	hf_slc_connect();

	/* The AG has deactivated the indicator. */
	test_br_sync();

	err = bt_hfp_hf_enhanced_safety(hf, true);
	zassert_equal(err, -EINVAL, "Enhanced safety while disabled returned %d", err);

	test_br_sync();

	/* The AG has activated the indicator. */
	test_br_sync();

	err = bt_hfp_hf_enhanced_safety(hf, true);
	zassert_equal(err, 0, "Failed to enable enhanced safety (err %d)", err);

	err = bt_hfp_hf_enhanced_safety(hf, false);
	zassert_equal(err, 0, "Failed to disable enhanced safety (err %d)", err);

	test_br_sync();
}

ZTEST(hfp_hf_slc, test_021_hfi_002)
{
	int err;

	test_br_sync();
	hf_slc_connect();

	test_br_sync();

	err = bt_hfp_hf_battery(hf, 2);
	zassert_equal(err, -EINVAL, "Battery while disabled returned %d", err);

	test_br_sync();
	test_br_sync();

	err = bt_hfp_hf_battery(hf, 2);
	zassert_equal(err, 0, "Failed to send battery level (err %d)", err);

	err = bt_hfp_hf_battery(hf, 1);
	zassert_equal(err, 0, "Failed to send battery level (err %d)", err);

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

ZTEST_SUITE(hfp_hf_slc, NULL, setup, before, after, teardown);
