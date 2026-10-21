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

/* b2b: CM_001 (AG), CM_002 (HF) - SLC established by the AG. */
ZTEST(hfp_ag_slc, test_001_cm_001_ag)
{
	test_br_sync();
	ag_slc_connect();
	test_br_sync();
}

/* b2b: CM_001 (HF), CM_002 (AG) - SLC established by the HF. */
ZTEST(hfp_ag_slc, test_002_cm_001_hf)
{
	test_br_sync();
	ag_slc_wait();
	test_br_sync();
}

/* b2b: CM_003 (AG) - SLC established and released by the AG. */
ZTEST(hfp_ag_slc, test_003_cm_003_ag)
{
	test_br_sync();
	ag_slc_connect();
	ag_slc_disconnect();
	test_br_sync();
}

/* b2b: CM_003 (HF) - SLC established and released by the HF. */
ZTEST(hfp_ag_slc, test_004_cm_003_hf)
{
	test_br_sync();
	ag_slc_wait();
	ag_slc_wait_disconnected();
	test_br_sync();
}

/* b2b: CM_004 (AG) - SLC established by the AG, released by the HF. */
ZTEST(hfp_ag_slc, test_005_cm_004_ag)
{
	test_br_sync();
	ag_slc_connect();
	ag_slc_wait_disconnected();
	test_br_sync();
}

/* b2b: CM_004 (HF) - SLC established by the HF, released by the AG. */
ZTEST(hfp_ag_slc, test_006_cm_004_hf)
{
	test_br_sync();
	ag_slc_wait();
	ag_slc_disconnect();
	test_br_sync();
}

/* b2b: PSI_001 - service availability indicator. */
ZTEST(hfp_ag_slc, test_007_psi_001)
{
	int err;

	test_br_sync();
	ag_slc_wait();

	err = bt_hfp_ag_service_availability(ag, false);
	zassert_equal(err, 0, "Failed to clear service (err %d)", err);

	err = bt_hfp_ag_service_availability(ag, true);
	zassert_equal(err, 0, "Failed to set service (err %d)", err);

	test_br_sync();
}

/* b2b: PSI_002 - signal strength indicator. */
ZTEST(hfp_ag_slc, test_008_psi_002)
{
	int err;

	test_br_sync();
	ag_slc_wait();

	err = bt_hfp_ag_signal_strength(ag, 2);
	zassert_equal(err, 0, "Failed to set signal strength (err %d)", err);

	/* An unchanged value is not sent again. */
	err = bt_hfp_ag_signal_strength(ag, 2);
	zassert_equal(err, -EINVAL, "Duplicate signal strength returned %d", err);

	err = bt_hfp_ag_signal_strength(ag, 6);
	zassert_equal(err, -EINVAL, "Signal strength 6 returned %d", err);

	test_br_sync();
}

/* b2b: PSI_003 - roaming status indicator. */
ZTEST(hfp_ag_slc, test_009_psi_003)
{
	int err;

	test_br_sync();
	ag_slc_wait();

	err = bt_hfp_ag_roaming_status(ag, 1);
	zassert_equal(err, 0, "Failed to set roaming status (err %d)", err);

	err = bt_hfp_ag_roaming_status(ag, 2);
	zassert_equal(err, -EINVAL, "Roaming status 2 returned %d", err);

	test_br_sync();
}

/* b2b: PSI_004 - battery level indicator. */
ZTEST(hfp_ag_slc, test_010_psi_004)
{
	int err;

	test_br_sync();
	ag_slc_wait();

	err = bt_hfp_ag_battery_level(ag, 1);
	zassert_equal(err, 0, "Failed to set battery level (err %d)", err);

	err = bt_hfp_ag_battery_level(ag, 6);
	zassert_equal(err, -EINVAL, "Battery level 6 returned %d", err);

	test_br_sync();
}

/* b2b: PSI_005 - network operator. */
ZTEST(hfp_ag_slc, test_011_psi_005)
{
	static char name[] = TEST_OPERATOR_NAME;
	int err;

	test_br_sync();
	ag_slc_wait();

	err = bt_hfp_ag_set_operator(ag, 1, name);
	zassert_equal(err, 0, "Failed to set operator (err %d)", err);

	/* The HF reads the operator now. */
	test_br_sync();
	test_br_sync();
}

/* b2b: RAVC_001 - microphone gain, from the HF and from the AG. */
ZTEST(hfp_ag_slc, test_012_ravc_001)
{
	int err;

	test_br_sync();
	ag_slc_wait();

	/* The HF reports its initial gain after the SLC is established. */
	err = test_evt_wait_value(EVT_VGM, 3, TEST_HFP_TIMEOUT, NULL);
	zassert_equal(err, 0, "No VGM 3 (err %d)", err);

	err = bt_hfp_ag_vgm(ag, 5);
	zassert_equal(err, 0, "Failed to set VGM (err %d)", err);

	err = bt_hfp_ag_vgm(ag, 16);
	zassert_equal(err, -EINVAL, "VGM 16 returned %d", err);

	test_br_sync();
}

/* b2b: RAVC_002 - speaker gain, from the HF and from the AG. */
ZTEST(hfp_ag_slc, test_013_ravc_002)
{
	int err;

	test_br_sync();
	ag_slc_wait();

	err = test_evt_wait_value(EVT_VGS, 3, TEST_HFP_TIMEOUT, NULL);
	zassert_equal(err, 0, "No VGS 3 (err %d)", err);

	err = bt_hfp_ag_vgs(ag, 7);
	zassert_equal(err, 0, "Failed to set VGS (err %d)", err);

	err = bt_hfp_ag_vgs(ag, 16);
	zassert_equal(err, -EINVAL, "VGS 16 returned %d", err);

	test_br_sync();
}

/* b2b: ECNR_001 - the HF turns off echo canceling and noise reduction of the AG. */
ZTEST(hfp_ag_slc, test_014_ecnr_001)
{
	test_br_sync();
	ag_slc_wait();

	TEST_EXPECT(EVT_ECNR_TURN_OFF, TEST_HFP_TIMEOUT, NULL);

	test_br_sync();
}

/* b2b: VRA_001 - voice recognition activated and deactivated by the HF. */
ZTEST(hfp_ag_slc, test_015_vra_001)
{
	struct test_event evt;

	test_br_sync();
	ag_slc_wait();

	TEST_EXPECT(EVT_VOICE_RECOGNITION, TEST_HFP_TIMEOUT, &evt);
	zassert_equal(evt.value, 1, "Voice recognition not activated");

	TEST_EXPECT(EVT_VOICE_RECOGNITION, TEST_HFP_TIMEOUT, &evt);
	zassert_equal(evt.value, 0, "Voice recognition not deactivated");

	test_br_sync();
}

/* Voice recognition activated by the AG, with engine state and textual representation. */
ZTEST(hfp_ag_slc, test_016_vra_ag)
{
	struct test_event evt;
	int err;

	test_br_sync();
	ag_slc_wait();

	err = bt_hfp_ag_voice_recognition(ag, true);
	zassert_equal(err, 0, "Failed to activate voice recognition (err %d)", err);

	/* The AG application sets up the audio connection of an AG activated session. */
	err = bt_hfp_ag_audio_connect(ag, BT_HFP_AG_CODEC_CVSD);
	zassert_equal(err, 0, "Failed to connect audio (err %d)", err);
	ag_wait_sco_connected(TEST_AIR_MODE_CVSD);

	err = bt_hfp_ag_vre_state(ag, BIT(0));
	zassert_equal(err, 0, "Failed to set engine state (err %d)", err);

	/* The textual representation is refused until the HF is ready to accept audio. */
	TEST_EXPECT(EVT_READY_TO_ACCEPT_AUDIO, TEST_HFP_TIMEOUT, &evt);

	err = bt_hfp_ag_vre_textual_representation(ag, BIT(1), "1", 1, 1, "Hello");
	zassert_equal(err, 0, "Failed to send textual representation (err %d)", err);

	/* The HF has received the text. */
	test_br_sync();

	err = bt_hfp_ag_voice_recognition(ag, false);
	zassert_equal(err, 0, "Failed to deactivate voice recognition (err %d)", err);

	test_br_sync();
}

/* b2b: VT_001 - phone number for a voice tag, refused and provided twice. */
ZTEST(hfp_ag_slc, test_017_vt_001)
{
	struct test_event evt;

	test_br_sync();
	ag_slc_wait();

	for (int32_t i = 1; i <= 4; i++) {
		TEST_EXPECT(EVT_REQUEST_PHONE_NUMBER, TEST_HFP_TIMEOUT, &evt);
		zassert_equal(evt.value, i, "Unexpected request %d", evt.value);
	}

	test_br_sync();
}

/* b2b: SNI_001 - subscriber number information. */
ZTEST(hfp_ag_slc, test_018_sni_001)
{
	ag_cfg.subscriber = true;

	test_br_sync();
	ag_slc_wait();

	TEST_EXPECT(EVT_SUBSCRIBER_NUMBER, TEST_HFP_TIMEOUT, NULL);

	test_br_sync();
}

/* b2b: IIA_001 - only the battery indicator is activated by the HF. */
ZTEST(hfp_ag_slc, test_019_iia_001)
{
	int err;

	test_br_sync();
	ag_slc_wait();

	/* The HF has deactivated every indicator but the battery level. */
	test_br_sync();

	err = bt_hfp_ag_signal_strength(ag, 2);
	zassert_equal(err, 0, "Failed to set signal strength (err %d)", err);

	err = bt_hfp_ag_battery_level(ag, 1);
	zassert_equal(err, 0, "Failed to set battery level (err %d)", err);

	test_br_sync();
}

/* b2b: HFI_001 - HF indicator Enhanced Safety, deactivated and activated by the AG. */
ZTEST(hfp_ag_slc, test_020_hfi_001)
{
	struct test_event evt;
	int err;

	test_br_sync();
	ag_slc_wait();

	err = bt_hfp_ag_hf_indicator(ag, HFP_AG_ENHANCED_SAFETY_IND, false);
	zassert_equal(err, 0, "Failed to disable indicator (err %d)", err);
	k_sleep(TEST_AT_DELIVERY_TIME);

	/* The HF fails to send a disabled indicator. */
	test_br_sync();
	test_br_sync();

	err = bt_hfp_ag_hf_indicator(ag, HFP_AG_ENHANCED_SAFETY_IND, true);
	zassert_equal(err, 0, "Failed to enable indicator (err %d)", err);
	k_sleep(TEST_AT_DELIVERY_TIME);

	test_br_sync();

	TEST_EXPECT(EVT_HF_INDICATOR_VALUE, TEST_HFP_TIMEOUT, &evt);
	zassert_equal(evt.value, HFP_AG_ENHANCED_SAFETY_IND, "Unexpected indicator %d", evt.value);
	zassert_equal(evt.value2, 1, "Unexpected value %d", evt.value2);

	TEST_EXPECT(EVT_HF_INDICATOR_VALUE, TEST_HFP_TIMEOUT, &evt);
	zassert_equal(evt.value, HFP_AG_ENHANCED_SAFETY_IND, "Unexpected indicator %d", evt.value);
	zassert_equal(evt.value2, 0, "Unexpected value %d", evt.value2);

	test_br_sync();
}

/* b2b: HFI_002 - HF indicator battery level, deactivated and activated by the AG. */
ZTEST(hfp_ag_slc, test_021_hfi_002)
{
	struct test_event evt;
	int err;

	test_br_sync();
	ag_slc_wait();

	err = bt_hfp_ag_hf_indicator(ag, HFP_AG_BATTERY_LEVEL_IND, false);
	zassert_equal(err, 0, "Failed to disable indicator (err %d)", err);
	k_sleep(TEST_AT_DELIVERY_TIME);

	test_br_sync();
	test_br_sync();

	err = bt_hfp_ag_hf_indicator(ag, HFP_AG_BATTERY_LEVEL_IND, true);
	zassert_equal(err, 0, "Failed to enable indicator (err %d)", err);
	k_sleep(TEST_AT_DELIVERY_TIME);

	test_br_sync();

	TEST_EXPECT(EVT_HF_INDICATOR_VALUE, TEST_HFP_TIMEOUT, &evt);
	zassert_equal(evt.value, HFP_AG_BATTERY_LEVEL_IND, "Unexpected indicator %d", evt.value);
	zassert_equal(evt.value2, 2, "Unexpected value %d", evt.value2);

	TEST_EXPECT(EVT_HF_INDICATOR_VALUE, TEST_HFP_TIMEOUT, &evt);
	zassert_equal(evt.value, HFP_AG_BATTERY_LEVEL_IND, "Unexpected indicator %d", evt.value);
	zassert_equal(evt.value2, 1, "Unexpected value %d", evt.value2);

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

ZTEST_SUITE(hfp_ag_slc, NULL, setup, before, after, teardown);
