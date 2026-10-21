/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/bluetooth/classic/hfp_hf.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"
#include "test_hf.h"

LOG_MODULE_REGISTER(test_hfp_hf, CONFIG_TEST_CLASSIC_HFP_SIM_LOG_LEVEL);

struct bt_hfp_hf *hf;
struct bt_conn *hf_sco;

static uint8_t sco_air_mode(struct bt_conn *sco_conn)
{
	struct bt_conn_info info;

	if (bt_conn_get_info(sco_conn, &info) != 0) {
		return UINT8_MAX;
	}

	return info.sco.air_mode;
}

static void hf_connected_cb(struct bt_conn *conn, struct bt_hfp_hf *hfp_hf)
{
	hf = hfp_hf;
	test_evt_post(EVT_CONNECTED, hfp_hf, 0, 0, NULL);
}

static void hf_disconnected_cb(struct bt_hfp_hf *hfp_hf)
{
	if (hf == hfp_hf) {
		hf = NULL;
	}
	test_evt_post(EVT_DISCONNECTED, hfp_hf, 0, 0, NULL);
}

static void hf_sco_connected_cb(struct bt_hfp_hf *hfp_hf, struct bt_conn *sco_conn)
{
	hf_sco = sco_conn;
	test_evt_post(EVT_SCO_CONNECTED, sco_conn, sco_air_mode(sco_conn), 0, NULL);
}

static void hf_sco_disconnected_cb(struct bt_conn *sco_conn, uint8_t reason)
{
	if (hf_sco == sco_conn) {
		hf_sco = NULL;
	}
	test_evt_post(EVT_SCO_DISCONNECTED, sco_conn, reason, 0, NULL);
}

static void hf_service_cb(struct bt_hfp_hf *hfp_hf, uint32_t value)
{
	test_evt_post(EVT_SERVICE, hfp_hf, (int32_t)value, 0, NULL);
}

static void hf_outgoing_cb(struct bt_hfp_hf *hfp_hf, struct bt_hfp_hf_call *call)
{
	test_evt_post(EVT_OUTGOING, call, 0, 0, NULL);
}

static void hf_remote_ringing_cb(struct bt_hfp_hf_call *call)
{
	test_evt_post(EVT_REMOTE_RINGING, call, 0, 0, NULL);
}

static void hf_incoming_cb(struct bt_hfp_hf *hfp_hf, struct bt_hfp_hf_call *call)
{
	test_evt_post(EVT_INCOMING, call, 0, 0, NULL);
}

static void hf_incoming_held_cb(struct bt_hfp_hf_call *call)
{
	test_evt_post(EVT_INCOMING_HELD, call, 0, 0, NULL);
}

static void hf_accept_cb(struct bt_hfp_hf_call *call)
{
	test_evt_post(EVT_ACCEPT, call, 0, 0, NULL);
}

static void hf_reject_cb(struct bt_hfp_hf_call *call)
{
	test_evt_post(EVT_REJECT, call, 0, 0, NULL);
}

static void hf_terminate_cb(struct bt_hfp_hf_call *call)
{
	test_evt_post(EVT_TERMINATE, call, 0, 0, NULL);
}

static void hf_held_cb(struct bt_hfp_hf_call *call)
{
	test_evt_post(EVT_HELD, call, 0, 0, NULL);
}

static void hf_retrieve_cb(struct bt_hfp_hf_call *call)
{
	test_evt_post(EVT_RETRIEVE, call, 0, 0, NULL);
}

static void hf_signal_cb(struct bt_hfp_hf *hfp_hf, uint32_t value)
{
	test_evt_post(EVT_SIGNAL, hfp_hf, (int32_t)value, 0, NULL);
}

static void hf_roam_cb(struct bt_hfp_hf *hfp_hf, uint32_t value)
{
	test_evt_post(EVT_ROAM, hfp_hf, (int32_t)value, 0, NULL);
}

static void hf_battery_cb(struct bt_hfp_hf *hfp_hf, uint32_t value)
{
	test_evt_post(EVT_BATTERY, hfp_hf, (int32_t)value, 0, NULL);
}

static void hf_ring_indication_cb(struct bt_hfp_hf_call *call)
{
	test_evt_post(EVT_RING_INDICATION, call, 0, 0, NULL);
}

static void hf_dialing_cb(struct bt_hfp_hf *hfp_hf, int err)
{
	test_evt_post(EVT_DIALING, hfp_hf, err, 0, NULL);
}

static void hf_clip_cb(struct bt_hfp_hf_call *call, const char *number, uint8_t type)
{
	test_evt_post(EVT_CLIP, call, type, 0, number);
}

static void hf_vgm_cb(struct bt_hfp_hf *hfp_hf, uint8_t gain)
{
	test_evt_post(EVT_VGM, hfp_hf, gain, 0, NULL);
}

static void hf_vgs_cb(struct bt_hfp_hf *hfp_hf, uint8_t gain)
{
	test_evt_post(EVT_VGS, hfp_hf, gain, 0, NULL);
}

static void hf_inband_ring_cb(struct bt_hfp_hf *hfp_hf, bool inband)
{
	test_evt_post(EVT_INBAND_RING, hfp_hf, inband, 0, NULL);
}

static void hf_operator_cb(struct bt_hfp_hf *hfp_hf, uint8_t mode, uint8_t format,
			   const char *operator)
{
	test_evt_post(EVT_OPERATOR, hfp_hf, mode, format, operator);
}

static void hf_codec_negotiate_cb(struct bt_hfp_hf *hfp_hf, uint8_t id)
{
	test_evt_post(EVT_CODEC_NEGOTIATE, hfp_hf, id, 0, NULL);
}

static void hf_ecnr_turn_off_cb(struct bt_hfp_hf *hfp_hf, int err)
{
	test_evt_post(EVT_ECNR_TURN_OFF, hfp_hf, err, 0, NULL);
}

static void hf_call_waiting_cb(struct bt_hfp_hf_call *call, const char *number, uint8_t type)
{
	test_evt_post(EVT_CALL_WAITING, call, type, 0, number);
}

static void hf_voice_recognition_cb(struct bt_hfp_hf *hfp_hf, bool activate)
{
	test_evt_post(EVT_VOICE_RECOGNITION, hfp_hf, activate, 0, NULL);
}

static void hf_vre_state_cb(struct bt_hfp_hf *hfp_hf, uint8_t state)
{
	test_evt_post(EVT_VRE_STATE, hfp_hf, state, 0, NULL);
}

static void hf_textual_representation_cb(struct bt_hfp_hf *hfp_hf, char *id, uint8_t type,
					 uint8_t operation, const char *text)
{
	char str[TEST_EVT_STR_LEN];

	/* Reported as "<id>:<text>" */
	snprintf(str, sizeof(str), "%s:%s", id, text);
	test_evt_post(EVT_TEXTUAL_REPRESENTATION, hfp_hf, type, operation, str);
}

static void hf_request_phone_number_cb(struct bt_hfp_hf *hfp_hf, const char *number)
{
	test_evt_post(EVT_REQUEST_PHONE_NUMBER, hfp_hf, (number != NULL) ? 0 : -1, 0, number);
}

static void hf_subscriber_number_cb(struct bt_hfp_hf *hfp_hf, const char *number, uint8_t type,
				    uint8_t service)
{
	test_evt_post(EVT_SUBSCRIBER_NUMBER, hfp_hf, type, service, number);
}

static void hf_query_call_cb(struct bt_hfp_hf *hfp_hf, struct bt_hfp_hf_current_call *call)
{
	int32_t info = (int32_t)call->dir | ((int32_t)call->status << 8) |
		       ((int32_t)call->mode << 16) | ((int32_t)call->multiparty << 24);

	test_evt_post(EVT_QUERY_CALL, hfp_hf, call->index, info, call->number);
}

static struct bt_hfp_hf_cb hf_cb = {
	.connected = hf_connected_cb,
	.disconnected = hf_disconnected_cb,
	.sco_connected = hf_sco_connected_cb,
	.sco_disconnected = hf_sco_disconnected_cb,
	.service = hf_service_cb,
	.outgoing = hf_outgoing_cb,
	.remote_ringing = hf_remote_ringing_cb,
	.incoming = hf_incoming_cb,
	.incoming_held = hf_incoming_held_cb,
	.accept = hf_accept_cb,
	.reject = hf_reject_cb,
	.terminate = hf_terminate_cb,
	.held = hf_held_cb,
	.retrieve = hf_retrieve_cb,
	.signal = hf_signal_cb,
	.roam = hf_roam_cb,
	.battery = hf_battery_cb,
	.ring_indication = hf_ring_indication_cb,
	.dialing = hf_dialing_cb,
	.clip = hf_clip_cb,
	.vgm = hf_vgm_cb,
	.vgs = hf_vgs_cb,
	.inband_ring = hf_inband_ring_cb,
	.operator = hf_operator_cb,
	.codec_negotiate = NULL,
	.ecnr_turn_off = hf_ecnr_turn_off_cb,
	.call_waiting = hf_call_waiting_cb,
	.voice_recognition = hf_voice_recognition_cb,
	.vre_state = hf_vre_state_cb,
	.textual_representation = hf_textual_representation_cb,
	.request_phone_number = hf_request_phone_number_cb,
	.subscriber_number = hf_subscriber_number_cb,
	.query_call = hf_query_call_cb,
};

void hf_suite_setup(void)
{
	int err;

	test_br_setup(TEST_BR_ACCEPTOR, BT_SECURITY_L1);

	err = bt_hfp_hf_register(&hf_cb);
	zassert_equal(err, 0, "Failed to register HF (err %d)", err);
}

void hf_suite_teardown(void)
{
	test_br_teardown();
}

void hf_test_before(void)
{
	test_evt_reset();

	hf = NULL;
	hf_sco = NULL;
	hf_codec_negotiation_by_app(false);
}

void hf_test_after(void)
{
	/* The HF objects are released together with the ACL link after the test. */
}

void hf_codec_negotiation_by_app(bool app)
{
	/* The stack reads the callback each time +BCS is received. */
	hf_cb.codec_negotiate = app ? hf_codec_negotiate_cb : NULL;
}

void hf_expect_value(enum test_evt type, int32_t value)
{
	struct test_event evt;

	TEST_EXPECT(type, TEST_HFP_TIMEOUT, &evt);
	zassert_equal(evt.value, value, "Unexpected %s value %d (expected %d)", test_evt_str(type),
		      evt.value, value);
}

/* The HF reports the indicator values of the AT+CIND? response during the SLC setup. */
static void hf_expect_initial_indicators(void)
{
	hf_expect_value(EVT_SERVICE, TEST_AG_IND_SERVICE);
	hf_expect_value(EVT_SIGNAL, TEST_AG_IND_SIGNAL);
	hf_expect_value(EVT_ROAM, TEST_AG_IND_ROAM);
	hf_expect_value(EVT_BATTERY, TEST_AG_IND_BATTERY);
}

static void hf_slc_settle(void)
{
	/* Let the HF finish the AT commands it sends right after the SLC is established. */
	k_sleep(TEST_SLC_SETTLE_TIME);
}

void hf_slc_connect(void)
{
	struct bt_hfp_hf *new_hf = NULL;
	struct test_event evt;
	uint8_t channel;
	int err;

	channel = test_sdp_get_rfcomm_channel(test_br_conn(), BT_SDP_HANDSFREE_AGW_SVCLASS);

	err = bt_hfp_hf_connect(test_br_conn(), &new_hf, channel);
	zassert_equal(err, 0, "Failed to connect HF (err %d)", err);
	zassert_not_null(new_hf, "No HF object");

	TEST_EXPECT(EVT_CONNECTED, TEST_HFP_TIMEOUT, &evt);
	zassert_equal_ptr(evt.obj, new_hf, "Unexpected HF object");

	hf_expect_initial_indicators();
	hf_slc_settle();
}

void hf_slc_wait(void)
{
	TEST_EXPECT(EVT_CONNECTED, TEST_HFP_TIMEOUT, NULL);
	zassert_not_null(hf, "No HF object");

	hf_expect_initial_indicators();
	hf_slc_settle();
}

void hf_slc_disconnect(void)
{
	int err;

	err = bt_hfp_hf_disconnect(hf);
	zassert_equal(err, 0, "Failed to disconnect HF (err %d)", err);

	hf_slc_wait_disconnected();
}

void hf_slc_wait_disconnected(void)
{
	TEST_EXPECT(EVT_DISCONNECTED, TEST_HFP_TIMEOUT, NULL);
	zassert_is_null(hf, "HF object not released");
}

struct bt_hfp_hf_call *hf_wait_call(enum test_evt type)
{
	struct test_event evt;

	TEST_EXPECT(type, TEST_HFP_CALL_TIMEOUT, &evt);
	zassert_not_null(evt.obj, "No call object for %s", test_evt_str(type));

	return evt.obj;
}

void hf_wait_sco_connected(uint8_t air_mode)
{
	struct test_event evt;

	TEST_EXPECT(EVT_SCO_CONNECTED, TEST_HFP_CALL_TIMEOUT, &evt);
	/* The AG may already have released the SCO connection, check the reported one. */
	zassert_not_null(evt.obj, "No SCO connection");
	if (air_mode != TEST_AIR_MODE_ANY) {
		zassert_equal(evt.value, air_mode, "Unexpected air mode %d", evt.value);
	}
}

void hf_wait_sco_disconnected(void)
{
	TEST_EXPECT(EVT_SCO_DISCONNECTED, TEST_HFP_CALL_TIMEOUT, NULL);
}

void hf_wait_no_sco(void)
{
	k_timepoint_t end = sys_timepoint_calc(TEST_HFP_CALL_TIMEOUT);

	while ((hf_sco != NULL) && !sys_timepoint_expired(end)) {
		k_sleep(K_MSEC(20));
	}

	zassert_is_null(hf_sco, "SCO connection not released");
}

struct bt_hfp_hf_call *hf_wait_incoming(const char *number)
{
	struct bt_hfp_hf_call *call;
	struct test_event evt;

	call = hf_wait_call(EVT_INCOMING);

	if (number != NULL) {
		/* RING and +CLIP are repeated while the call rings; take one of this call. */
		int err = test_evt_wait_obj(EVT_CLIP, call, TEST_HFP_CALL_TIMEOUT, &evt);

		zassert_equal(err, 0, "No CLIP for the call (err %d)", err);
		zassert_str_equal(evt.str, number, "Unexpected CLIP number %s", evt.str);
	}

	return call;
}

struct bt_hfp_hf_call *hf_wait_ringing_call(const char *number)
{
	struct bt_hfp_hf_call *call = hf_wait_incoming(number);

	/* The AG acts on the call once the HF has seen it ringing. */
	test_br_sync();

	return call;
}

void hf_expect_call(enum test_evt type, struct bt_hfp_hf_call *call)
{
	int err;

	/* Events of the same type of other calls are skipped. */
	err = test_evt_wait_obj(type, call, TEST_HFP_CALL_TIMEOUT, NULL);
	zassert_equal(err, 0, "No %s event for the call (err %d)", test_evt_str(type), err);
}

void hf_sco_disconnect(void)
{
	int err;

	zassert_not_null(hf_sco, "No SCO connection");

	err = bt_conn_disconnect(hf_sco, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	zassert_equal(err, 0, "Failed to disconnect SCO (err %d)", err);

	hf_wait_sco_disconnected();
}
