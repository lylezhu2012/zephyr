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

/* Codec ID that is neither CVSD, mSBC nor LC3-SWB. */
#define TEST_UNSUPPORTED_CODEC 4

static void hf_audio_connect(void)
{
	int err;

	err = bt_hfp_hf_audio_connect(hf);
	zassert_equal(err, 0, "Failed to request audio connection (err %d)", err);
}

/* Answer the codec negotiation of the AG, which proposes @p codec. */
static void hf_select_codec(uint8_t codec)
{
	int err;

	hf_expect_value(EVT_CODEC_NEGOTIATE, codec);

	err = bt_hfp_hf_select_codec(hf, codec);
	zassert_equal(err, 0, "Failed to select codec %u (err %d)", codec, err);
}

ZTEST(hfp_hf_audio, test_001_ach_001_ag)
{
	test_br_sync();
	hf_slc_wait();

	hf_wait_sco_connected(TEST_AIR_MODE_CVSD);

	test_br_sync();
}

ZTEST(hfp_hf_audio, test_002_ach_001_hf)
{
	test_br_sync();
	hf_slc_connect();

	hf_audio_connect();
	hf_wait_sco_connected(TEST_AIR_MODE_CVSD);

	test_br_sync();
}

ZTEST(hfp_hf_audio, test_003_ach_003_ag)
{
	test_br_sync();
	hf_slc_wait();

	hf_wait_sco_connected(TEST_AIR_MODE_CVSD);
	hf_wait_sco_disconnected();

	test_br_sync();
}

ZTEST(hfp_hf_audio, test_004_ach_003_hf)
{
	test_br_sync();
	hf_slc_connect();

	hf_audio_connect();
	hf_wait_sco_connected(TEST_AIR_MODE_CVSD);
	hf_sco_disconnect();

	test_br_sync();
}

ZTEST(hfp_hf_audio, test_005_ach_004_ag)
{
	test_br_sync();
	hf_slc_wait();

	hf_wait_sco_connected(TEST_AIR_MODE_CVSD);
	hf_sco_disconnect();

	test_br_sync();
}

ZTEST(hfp_hf_audio, test_006_ach_004_hf)
{
	test_br_sync();
	hf_slc_connect();

	hf_audio_connect();
	hf_wait_sco_connected(TEST_AIR_MODE_CVSD);
	hf_wait_sco_disconnected();

	test_br_sync();
}

ZTEST(hfp_hf_audio, test_007_ach_005_ag)
{
	hf_codec_negotiation_by_app(true);

	test_br_sync();
	hf_slc_wait();

	hf_select_codec(BT_HFP_HF_CODEC_CVSD);
	hf_wait_sco_connected(TEST_AIR_MODE_CVSD);

	test_br_sync();
}

ZTEST(hfp_hf_audio, test_008_ach_005_hf)
{
	hf_codec_negotiation_by_app(true);

	test_br_sync();
	hf_slc_connect();

	hf_audio_connect();
	hf_select_codec(BT_HFP_HF_CODEC_MSBC);
	hf_wait_sco_connected(TEST_AIR_MODE_TRANSPARENT);

	test_br_sync();
}

ZTEST(hfp_hf_audio, test_009_ach_006_hf)
{
	hf_codec_negotiation_by_app(true);

	test_br_sync();
	hf_slc_wait();

	hf_select_codec(BT_HFP_HF_CODEC_MSBC);
	hf_wait_sco_connected(TEST_AIR_MODE_TRANSPARENT);

	test_br_sync();
}

ZTEST(hfp_hf_audio, test_010_ach_006_ag)
{
	int err;

	test_br_sync();
	hf_slc_wait();

	/* The CVSD codec is mandatory in AT+BAC. */
	err = bt_hfp_hf_set_codecs(hf, BIT(BT_HFP_HF_CODEC_MSBC));
	zassert_equal(err, -EINVAL, "Codecs without CVSD returned %d", err);

	err = bt_hfp_hf_set_codecs(hf, BIT(BT_HFP_HF_CODEC_CVSD));
	zassert_equal(err, 0, "Failed to set codecs (err %d)", err);

	test_br_sync();
}

ZTEST(hfp_hf_audio, test_011_cn_003_ag)
{
	hf_codec_negotiation_by_app(true);

	test_br_sync();
	hf_slc_wait();

	hf_select_codec(BT_HFP_HF_CODEC_MSBC);
	hf_wait_sco_connected(TEST_AIR_MODE_TRANSPARENT);
	hf_wait_sco_disconnected();

	hf_select_codec(BT_HFP_HF_CODEC_CVSD);
	hf_wait_sco_connected(TEST_AIR_MODE_CVSD);

	test_br_sync();
}

ZTEST(hfp_hf_audio, test_012_cn_003_hf)
{
	hf_codec_negotiation_by_app(true);

	test_br_sync();
	hf_slc_connect();

	hf_audio_connect();
	hf_select_codec(BT_HFP_HF_CODEC_MSBC);
	hf_wait_sco_connected(TEST_AIR_MODE_TRANSPARENT);
	hf_sco_disconnect();

	hf_audio_connect();
	hf_select_codec(BT_HFP_HF_CODEC_CVSD);
	hf_wait_sco_connected(TEST_AIR_MODE_CVSD);

	test_br_sync();
}

ZTEST(hfp_hf_audio, test_013_cn_004)
{
	int err;

	hf_codec_negotiation_by_app(true);

	test_br_sync();
	hf_slc_wait();

	hf_expect_value(EVT_CODEC_NEGOTIATE, BT_HFP_HF_CODEC_MSBC);

	err = bt_hfp_hf_select_codec(hf, TEST_UNSUPPORTED_CODEC);
	zassert_equal(err, -ENOTSUP, "Unsupported codec returned %d", err);

	test_br_sync();
	zassert_is_null(hf_sco, "Unexpected SCO connection");
}

ZTEST(hfp_hf_audio, test_014_swbs_001_ag)
{
	hf_codec_negotiation_by_app(true);

	test_br_sync();
	hf_slc_wait();

	hf_select_codec(BT_HFP_HF_CODEC_LC3_SWB);
	hf_wait_sco_connected(TEST_AIR_MODE_TRANSPARENT);

	test_br_sync();
}

ZTEST(hfp_hf_audio, test_015_swbs_001_hf)
{
	hf_codec_negotiation_by_app(true);

	test_br_sync();
	hf_slc_connect();

	hf_audio_connect();
	hf_select_codec(BT_HFP_HF_CODEC_LC3_SWB);
	hf_wait_sco_connected(TEST_AIR_MODE_TRANSPARENT);

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

ZTEST_SUITE(hfp_hf_audio, NULL, setup, before, after, teardown);
