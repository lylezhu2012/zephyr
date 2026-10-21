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

static void ag_audio_connect(uint8_t codec)
{
	int err;

	err = bt_hfp_ag_audio_connect(ag, codec);
	zassert_equal(err, 0, "Failed to connect audio with codec %u (err %d)", codec, err);
}

/* Answer the audio connection request of the HF (AT+BCC) with @p codec. */
static void ag_accept_audio_request(uint8_t codec)
{
	TEST_EXPECT(EVT_AUDIO_CONNECT_REQ, TEST_HFP_TIMEOUT, NULL);
	ag_audio_connect(codec);
}

static void ag_expect_codec_negotiated(void)
{
	struct test_event evt;

	TEST_EXPECT(EVT_CODEC_NEGOTIATE, TEST_HFP_TIMEOUT, &evt);
	zassert_equal(evt.value, 0, "Codec negotiation failed (err %d)", evt.value);
}

/* b2b: ACH_001 (AG), ACH_002 (HF) - audio connection set up by the AG. */
ZTEST(hfp_ag_audio, test_001_ach_001_ag)
{
	test_br_sync();
	ag_slc_connect();

	ag_audio_connect(BT_HFP_AG_CODEC_CVSD);
	ag_wait_sco_connected(TEST_AIR_MODE_CVSD);

	test_br_sync();
}

/* b2b: ACH_001 (HF), ACH_002 (AG) - audio connection requested by the HF. */
ZTEST(hfp_ag_audio, test_002_ach_001_hf)
{
	test_br_sync();
	ag_slc_wait();

	ag_accept_audio_request(BT_HFP_AG_CODEC_CVSD);
	ag_wait_sco_connected(TEST_AIR_MODE_CVSD);

	test_br_sync();
}

/* b2b: ACH_003 (AG) - audio connection set up and released by the AG. */
ZTEST(hfp_ag_audio, test_003_ach_003_ag)
{
	test_br_sync();
	ag_slc_connect();

	ag_audio_connect(BT_HFP_AG_CODEC_CVSD);
	ag_wait_sco_connected(TEST_AIR_MODE_CVSD);
	ag_sco_disconnect();

	test_br_sync();
}

/* b2b: ACH_003 (HF) - audio connection requested and released by the HF. */
ZTEST(hfp_ag_audio, test_004_ach_003_hf)
{
	test_br_sync();
	ag_slc_wait();

	ag_accept_audio_request(BT_HFP_AG_CODEC_CVSD);
	ag_wait_sco_connected(TEST_AIR_MODE_CVSD);
	ag_wait_sco_disconnected();

	test_br_sync();
}

/* b2b: ACH_004 (AG) - audio connection set up by the AG, released by the HF. */
ZTEST(hfp_ag_audio, test_005_ach_004_ag)
{
	test_br_sync();
	ag_slc_connect();

	ag_audio_connect(BT_HFP_AG_CODEC_CVSD);
	ag_wait_sco_connected(TEST_AIR_MODE_CVSD);
	ag_wait_sco_disconnected();

	test_br_sync();
}

/* b2b: ACH_004 (HF) - audio connection requested by the HF, released by the AG. */
ZTEST(hfp_ag_audio, test_006_ach_004_hf)
{
	test_br_sync();
	ag_slc_wait();

	ag_accept_audio_request(BT_HFP_AG_CODEC_CVSD);
	ag_wait_sco_connected(TEST_AIR_MODE_CVSD);
	ag_sco_disconnect();

	test_br_sync();
}

/* b2b: ACH_005 (AG) - CVSD selected by the HF application in the codec negotiation. */
ZTEST(hfp_ag_audio, test_007_ach_005_ag)
{
	test_br_sync();
	ag_slc_connect();

	ag_audio_connect(BT_HFP_AG_CODEC_CVSD);
	ag_expect_codec_negotiated();
	ag_wait_sco_connected(TEST_AIR_MODE_CVSD);

	test_br_sync();
}

/*
 * b2b: ACH_005 (HF), CN_001 (HF), WBS_001 (HF), CN_002 (AG) - mSBC audio connection requested
 * by the HF.
 */
ZTEST(hfp_ag_audio, test_008_ach_005_hf)
{
	test_br_sync();
	ag_slc_wait();

	ag_accept_audio_request(BT_HFP_AG_CODEC_MSBC);
	ag_expect_codec_negotiated();
	ag_wait_sco_connected(TEST_AIR_MODE_TRANSPARENT);

	test_br_sync();
}

/*
 * b2b: ACH_006 (HF), CN_002 (HF), CN_001 (AG), WBS_001 (AG) - mSBC audio connection set up by
 * the AG.
 */
ZTEST(hfp_ag_audio, test_009_ach_006_hf)
{
	test_br_sync();
	ag_slc_connect();

	ag_audio_connect(BT_HFP_AG_CODEC_MSBC);
	ag_expect_codec_negotiated();
	ag_wait_sco_connected(TEST_AIR_MODE_TRANSPARENT);

	test_br_sync();
}

/* b2b: ACH_006 (AG) - codec IDs updated by the HF (AT+BAC). */
ZTEST(hfp_ag_audio, test_010_ach_006_ag)
{
	int err;

	test_br_sync();
	ag_slc_connect();

	err = test_evt_wait_value(EVT_CODEC_IDS, BIT(BT_HFP_AG_CODEC_CVSD), TEST_HFP_TIMEOUT, NULL);
	zassert_equal(err, 0, "Codec IDs not updated (err %d)", err);

	test_br_sync();
}

/* b2b: CN_003 (AG) - codec renegotiated by the AG from mSBC to CVSD. */
ZTEST(hfp_ag_audio, test_011_cn_003_ag)
{
	test_br_sync();
	ag_slc_connect();

	ag_audio_connect(BT_HFP_AG_CODEC_MSBC);
	ag_expect_codec_negotiated();
	ag_wait_sco_connected(TEST_AIR_MODE_TRANSPARENT);
	ag_sco_disconnect();

	ag_audio_connect(BT_HFP_AG_CODEC_CVSD);
	ag_expect_codec_negotiated();
	ag_wait_sco_connected(TEST_AIR_MODE_CVSD);

	test_br_sync();
}

/* b2b: CN_003 (HF) - codec renegotiated from mSBC to CVSD on requests of the HF. */
ZTEST(hfp_ag_audio, test_012_cn_003_hf)
{
	test_br_sync();
	ag_slc_wait();

	ag_accept_audio_request(BT_HFP_AG_CODEC_MSBC);
	ag_expect_codec_negotiated();
	ag_wait_sco_connected(TEST_AIR_MODE_TRANSPARENT);
	ag_wait_sco_disconnected();

	ag_accept_audio_request(BT_HFP_AG_CODEC_CVSD);
	ag_expect_codec_negotiated();
	ag_wait_sco_connected(TEST_AIR_MODE_CVSD);

	test_br_sync();
}

/* b2b: CN_004 - the HF application selects an unsupported codec. */
ZTEST(hfp_ag_audio, test_013_cn_004)
{
	test_br_sync();
	ag_slc_connect();

	ag_audio_connect(BT_HFP_AG_CODEC_MSBC);

	/* The HF has refused codec 4 locally; no audio connection is set up. */
	test_br_sync();
	zassert_is_null(ag_sco, "Unexpected SCO connection");
}

/* b2b: SWBS_001 (AG) - LC3-SWB audio connection set up by the AG. */
ZTEST(hfp_ag_audio, test_014_swbs_001_ag)
{
	test_br_sync();
	ag_slc_connect();

	ag_audio_connect(BT_HFP_AG_CODEC_LC3_SWB);
	ag_expect_codec_negotiated();
	ag_wait_sco_connected(TEST_AIR_MODE_TRANSPARENT);

	test_br_sync();
}

/* b2b: SWBS_001 (HF) - LC3-SWB audio connection requested by the HF. */
ZTEST(hfp_ag_audio, test_015_swbs_001_hf)
{
	test_br_sync();
	ag_slc_wait();

	ag_accept_audio_request(BT_HFP_AG_CODEC_LC3_SWB);
	ag_expect_codec_negotiated();
	ag_wait_sco_connected(TEST_AIR_MODE_TRANSPARENT);

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

ZTEST_SUITE(hfp_ag_audio, NULL, setup, before, after, teardown);
