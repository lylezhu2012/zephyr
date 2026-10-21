/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/bluetooth/classic/hfp_ag.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"
#include "test_ag.h"

LOG_MODULE_REGISTER(test_hfp_ag, CONFIG_TEST_CLASSIC_HFP_SIM_LOG_LEVEL);

struct bt_hfp_ag *ag;
struct bt_conn *ag_sco;
struct ag_config ag_cfg;

static uint32_t phone_number_requests;
static char memory_number[] = TEST_AG_DIAL_NUMBER;
static char voice_tag_number[] = TEST_AG_VOICE_TAG_NUMBER;
static char subscriber_number[] = TEST_AG_SUBSCRIBER_NUMBER;

static uint8_t sco_air_mode(struct bt_conn *sco_conn)
{
	struct bt_conn_info info;

	if (bt_conn_get_info(sco_conn, &info) != 0) {
		return UINT8_MAX;
	}

	return info.sco.air_mode;
}

static void ag_connected_cb(struct bt_conn *conn, struct bt_hfp_ag *hfp_ag)
{
	ag = hfp_ag;
	test_evt_post(EVT_CONNECTED, hfp_ag, 0, 0, NULL);
}

static void ag_disconnected_cb(struct bt_hfp_ag *hfp_ag)
{
	if (ag == hfp_ag) {
		ag = NULL;
	}
	test_evt_post(EVT_DISCONNECTED, hfp_ag, 0, 0, NULL);
}

static void ag_sco_connected_cb(struct bt_hfp_ag *hfp_ag, struct bt_conn *sco_conn)
{
	ag_sco = sco_conn;
	test_evt_post(EVT_SCO_CONNECTED, sco_conn, sco_air_mode(sco_conn), 0, NULL);
}

static void ag_sco_disconnected_cb(struct bt_conn *sco_conn, uint8_t reason)
{
	if (ag_sco == sco_conn) {
		ag_sco = NULL;
	}
	test_evt_post(EVT_SCO_DISCONNECTED, sco_conn, reason, 0, NULL);
}

static int ag_get_indicator_value_cb(struct bt_hfp_ag *hfp_ag, uint8_t *service, uint8_t *strength,
				     uint8_t *roam, uint8_t *battery)
{
	*service = ag_cfg.ind.service;
	*strength = ag_cfg.ind.signal;
	*roam = ag_cfg.ind.roam;
	*battery = ag_cfg.ind.battery;

	return 0;
}

static int ag_get_ongoing_call_cb(struct bt_hfp_ag *hfp_ag)
{
	if (!ag_cfg.ongoing_calls) {
		return -ENOTSUP;
	}

	test_evt_post(EVT_GET_ONGOING_CALL, hfp_ag, 0, 0, NULL);
	return 0;
}

static int ag_memory_dial_cb(struct bt_hfp_ag *hfp_ag, const char *location, char **number)
{
	test_evt_post(EVT_MEMORY_DIAL, hfp_ag, 0, 0, location);

	if (strcmp(location, TEST_AG_MEMORY_LOCATION) != 0) {
		return -ENOTSUP;
	}

	*number = memory_number;
	return 0;
}

static int ag_number_call_cb(struct bt_hfp_ag *hfp_ag, const char *number)
{
	test_evt_post(EVT_NUMBER_CALL, hfp_ag, 0, 0, number);

	return (strcmp(number, TEST_AG_DIAL_NUMBER) == 0) ? 0 : -ENOTSUP;
}

static int ag_redial_cb(struct bt_hfp_ag *hfp_ag,
			char number[CONFIG_BT_HFP_AG_PHONE_NUMBER_MAX_LEN + 1])
{
	test_evt_post(EVT_REDIAL, hfp_ag, 0, 0, NULL);

	if (ag_cfg.last_number[0] == '\0') {
		return -ENOENT;
	}

	strcpy(number, ag_cfg.last_number);
	return 0;
}

static void ag_outgoing_cb(struct bt_hfp_ag *hfp_ag, struct bt_hfp_ag_call *call,
			   const char *number)
{
	test_evt_post(EVT_OUTGOING, call, 0, 0, number);
}

static void ag_incoming_cb(struct bt_hfp_ag *hfp_ag, struct bt_hfp_ag_call *call,
			   const char *number)
{
	test_evt_post(EVT_INCOMING, call, 0, 0, number);
}

static void ag_incoming_held_cb(struct bt_hfp_ag_call *call)
{
	test_evt_post(EVT_INCOMING_HELD, call, 0, 0, NULL);
}

static void ag_ringing_cb(struct bt_hfp_ag_call *call, bool in_band)
{
	test_evt_post(EVT_RINGING, call, in_band, 0, NULL);
}

static void ag_accept_cb(struct bt_hfp_ag_call *call)
{
	test_evt_post(EVT_ACCEPT, call, 0, 0, NULL);
}

static void ag_held_cb(struct bt_hfp_ag_call *call)
{
	test_evt_post(EVT_HELD, call, 0, 0, NULL);
}

static void ag_retrieve_cb(struct bt_hfp_ag_call *call)
{
	test_evt_post(EVT_RETRIEVE, call, 0, 0, NULL);
}

static void ag_reject_cb(struct bt_hfp_ag_call *call)
{
	test_evt_post(EVT_REJECT, call, 0, 0, NULL);
}

static void ag_terminate_cb(struct bt_hfp_ag_call *call)
{
	test_evt_post(EVT_TERMINATE, call, 0, 0, NULL);
}

static void ag_codec_cb(struct bt_hfp_ag *hfp_ag, uint32_t ids)
{
	test_evt_post(EVT_CODEC_IDS, hfp_ag, (int32_t)ids, 0, NULL);
}

static void ag_codec_negotiate_cb(struct bt_hfp_ag *hfp_ag, int err)
{
	test_evt_post(EVT_CODEC_NEGOTIATE, hfp_ag, err, 0, NULL);
}

static void ag_audio_connect_req_cb(struct bt_hfp_ag *hfp_ag)
{
	test_evt_post(EVT_AUDIO_CONNECT_REQ, hfp_ag, 0, 0, NULL);
}

static void ag_vgm_cb(struct bt_hfp_ag *hfp_ag, uint8_t gain)
{
	test_evt_post(EVT_VGM, hfp_ag, gain, 0, NULL);
}

static void ag_vgs_cb(struct bt_hfp_ag *hfp_ag, uint8_t gain)
{
	test_evt_post(EVT_VGS, hfp_ag, gain, 0, NULL);
}

static void ag_ecnr_turn_off_cb(struct bt_hfp_ag *hfp_ag)
{
	test_evt_post(EVT_ECNR_TURN_OFF, hfp_ag, 0, 0, NULL);
}

static void ag_explicit_call_transfer_cb(struct bt_hfp_ag *hfp_ag)
{
	test_evt_post(EVT_EXPLICIT_CALL_TRANSFER, hfp_ag, 0, 0, NULL);
}

static void ag_voice_recognition_cb(struct bt_hfp_ag *hfp_ag, bool activate)
{
	test_evt_post(EVT_VOICE_RECOGNITION, hfp_ag, activate, 0, NULL);
}

static void ag_ready_to_accept_audio_cb(struct bt_hfp_ag *hfp_ag)
{
	test_evt_post(EVT_READY_TO_ACCEPT_AUDIO, hfp_ag, 0, 0, NULL);
}

static int ag_request_phone_number_cb(struct bt_hfp_ag *hfp_ag, char **number)
{
	phone_number_requests++;
	test_evt_post(EVT_REQUEST_PHONE_NUMBER, hfp_ag, (int32_t)phone_number_requests, 0, NULL);

	/* Refuse every odd request, so that both results are seen by the HF. */
	if ((phone_number_requests % 2U) != 0U) {
		return -EINVAL;
	}

	*number = voice_tag_number;
	return 0;
}

static void ag_transmit_dtmf_code_cb(struct bt_hfp_ag *hfp_ag, char code)
{
	test_evt_post(EVT_DTMF_CODE, hfp_ag, code, 0, NULL);
}

static int ag_subscriber_number_cb(struct bt_hfp_ag *hfp_ag, bt_hfp_ag_query_subscriber_func_t func)
{
	test_evt_post(EVT_SUBSCRIBER_NUMBER, hfp_ag, 0, 0, NULL);

	if (ag_cfg.subscriber) {
		(void)func(hfp_ag, subscriber_number, TEST_AG_SUBSCRIBER_TYPE,
			   TEST_AG_SUBSCRIBER_SERVICE);
	}

	return 0;
}

static void ag_hf_indicator_value_cb(struct bt_hfp_ag *hfp_ag, enum hfp_ag_hf_indicators indicator,
				     uint32_t value)
{
	test_evt_post(EVT_HF_INDICATOR_VALUE, hfp_ag, indicator, (int32_t)value, NULL);
}

static struct bt_hfp_ag_cb ag_cb = {
	.connected = ag_connected_cb,
	.disconnected = ag_disconnected_cb,
	.sco_connected = ag_sco_connected_cb,
	.sco_disconnected = ag_sco_disconnected_cb,
	.get_indicator_value = ag_get_indicator_value_cb,
	.get_ongoing_call = ag_get_ongoing_call_cb,
	.memory_dial = ag_memory_dial_cb,
	.number_call = ag_number_call_cb,
	.redial = ag_redial_cb,
	.outgoing = ag_outgoing_cb,
	.incoming = ag_incoming_cb,
	.incoming_held = ag_incoming_held_cb,
	.ringing = ag_ringing_cb,
	.accept = ag_accept_cb,
	.held = ag_held_cb,
	.retrieve = ag_retrieve_cb,
	.reject = ag_reject_cb,
	.terminate = ag_terminate_cb,
	.codec = ag_codec_cb,
	.codec_negotiate = ag_codec_negotiate_cb,
	.audio_connect_req = ag_audio_connect_req_cb,
	.vgm = ag_vgm_cb,
	.vgs = ag_vgs_cb,
	.ecnr_turn_off = ag_ecnr_turn_off_cb,
	.explicit_call_transfer = ag_explicit_call_transfer_cb,
	.voice_recognition = ag_voice_recognition_cb,
	.ready_to_accept_audio = ag_ready_to_accept_audio_cb,
	.request_phone_number = ag_request_phone_number_cb,
	.transmit_dtmf_code = ag_transmit_dtmf_code_cb,
	.subscriber_number = ag_subscriber_number_cb,
	.hf_indicator_value = ag_hf_indicator_value_cb,
};

void ag_suite_setup(void)
{
	int err;

	test_br_setup(TEST_BR_INITIATOR, BT_SECURITY_L1);

	err = bt_hfp_ag_register(&ag_cb);
	zassert_equal(err, 0, "Failed to register AG (err %d)", err);
}

void ag_suite_teardown(void)
{
	test_br_teardown();
}

void ag_test_before(void)
{
	test_evt_reset();

	ag = NULL;
	ag_sco = NULL;
	phone_number_requests = 0;

	memset(&ag_cfg, 0, sizeof(ag_cfg));
	ag_cfg.ind.service = TEST_AG_IND_SERVICE;
	ag_cfg.ind.signal = TEST_AG_IND_SIGNAL;
	ag_cfg.ind.roam = TEST_AG_IND_ROAM;
	ag_cfg.ind.battery = TEST_AG_IND_BATTERY;
}

void ag_test_after(void)
{
	/* The AG objects are released together with the ACL link after the test. */
}

static void ag_slc_settle(void)
{
	/* Let the HF finish the AT commands it sends right after the SLC is established. */
	k_sleep(TEST_SLC_SETTLE_TIME);
}

void ag_slc_connect(void)
{
	struct bt_hfp_ag *new_ag = NULL;
	struct test_event evt;
	uint8_t channel;
	int err;

	channel = test_sdp_get_rfcomm_channel(test_br_conn(), BT_SDP_HANDSFREE_SVCLASS);

	err = bt_hfp_ag_connect(test_br_conn(), &new_ag, channel);
	zassert_equal(err, 0, "Failed to connect AG (err %d)", err);
	zassert_not_null(new_ag, "No AG object");

	TEST_EXPECT(EVT_CONNECTED, TEST_HFP_TIMEOUT, &evt);
	zassert_equal_ptr(evt.obj, new_ag, "Unexpected AG object");

	ag_slc_settle();
}

void ag_slc_wait(void)
{
	TEST_EXPECT(EVT_CONNECTED, TEST_HFP_TIMEOUT, NULL);
	zassert_not_null(ag, "No AG object");

	ag_slc_settle();
}

void ag_slc_disconnect(void)
{
	int err;

	err = bt_hfp_ag_disconnect(ag);
	zassert_equal(err, 0, "Failed to disconnect AG (err %d)", err);

	ag_slc_wait_disconnected();
}

void ag_slc_wait_disconnected(void)
{
	TEST_EXPECT(EVT_DISCONNECTED, TEST_HFP_TIMEOUT, NULL);
	zassert_is_null(ag, "AG object not released");
}

struct bt_hfp_ag_call *ag_wait_call(enum test_evt type)
{
	struct test_event evt;

	TEST_EXPECT(type, TEST_HFP_CALL_TIMEOUT, &evt);
	zassert_not_null(evt.obj, "No call object for %s", test_evt_str(type));

	return evt.obj;
}

void ag_expect_call(enum test_evt type, struct bt_hfp_ag_call *call)
{
	int err;

	/* Events of the same type of other calls (e.g. a first call ringing) are skipped. */
	err = test_evt_wait_obj(type, call, TEST_HFP_CALL_TIMEOUT, NULL);
	zassert_equal(err, 0, "No %s event for the call (err %d)", test_evt_str(type), err);
}

struct bt_hfp_ag_call *ag_incoming(const char *number)
{
	struct test_event evt;
	int err;

	err = bt_hfp_ag_remote_incoming(ag, number);
	zassert_equal(err, 0, "Failed to notify incoming call (err %d)", err);

	TEST_EXPECT(EVT_INCOMING, TEST_HFP_CALL_TIMEOUT, &evt);
	zassert_not_null(evt.obj, "No call object");
	zassert_str_equal(evt.str, number, "Unexpected number %s", evt.str);

	return evt.obj;
}

void ag_wait_sco_connected(uint8_t air_mode)
{
	struct test_event evt;

	TEST_EXPECT(EVT_SCO_CONNECTED, TEST_HFP_CALL_TIMEOUT, &evt);
	/* The HF may already have released the SCO connection, check the reported one. */
	zassert_not_null(evt.obj, "No SCO connection");
	if (air_mode != TEST_AIR_MODE_ANY) {
		zassert_equal(evt.value, air_mode, "Unexpected air mode %d", evt.value);
	}
}

void ag_wait_sco_disconnected(void)
{
	TEST_EXPECT(EVT_SCO_DISCONNECTED, TEST_HFP_CALL_TIMEOUT, NULL);
}

void ag_wait_no_sco(void)
{
	k_timepoint_t end = sys_timepoint_calc(TEST_HFP_CALL_TIMEOUT);

	while ((ag_sco != NULL) && !sys_timepoint_expired(end)) {
		k_sleep(K_MSEC(20));
	}

	zassert_is_null(ag_sco, "SCO connection not released");
}

void ag_sco_disconnect(void)
{
	int err;

	zassert_not_null(ag_sco, "No SCO connection");

	err = bt_conn_disconnect(ag_sco, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	zassert_equal(err, 0, "Failed to disconnect SCO (err %d)", err);

	ag_wait_sco_disconnected();
}
