/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"

LOG_MODULE_REGISTER(test_hfp_common, CONFIG_TEST_CLASSIC_HFP_SIM_LOG_LEVEL);

#define EVT_SLOTS 64

static struct test_event evt_slots[EVT_SLOTS];
static bool evt_used[EVT_SLOTS];
static uint32_t evt_seq[EVT_SLOTS];
static uint32_t evt_next_seq;

static K_MUTEX_DEFINE(evt_lock);
static K_CONDVAR_DEFINE(evt_cond);

static const char *const evt_names[] = {
	[EVT_CONNECTED] = "connected",
	[EVT_DISCONNECTED] = "disconnected",
	[EVT_SCO_CONNECTED] = "sco_connected",
	[EVT_SCO_DISCONNECTED] = "sco_disconnected",
	[EVT_SERVICE] = "service",
	[EVT_SIGNAL] = "signal",
	[EVT_ROAM] = "roam",
	[EVT_BATTERY] = "battery",
	[EVT_VGM] = "vgm",
	[EVT_VGS] = "vgs",
	[EVT_OPERATOR] = "operator",
	[EVT_OUTGOING] = "outgoing",
	[EVT_REMOTE_RINGING] = "remote_ringing",
	[EVT_INCOMING] = "incoming",
	[EVT_INCOMING_HELD] = "incoming_held",
	[EVT_RINGING] = "ringing",
	[EVT_ACCEPT] = "accept",
	[EVT_REJECT] = "reject",
	[EVT_TERMINATE] = "terminate",
	[EVT_HELD] = "held",
	[EVT_RETRIEVE] = "retrieve",
	[EVT_RING_INDICATION] = "ring_indication",
	[EVT_DIALING] = "dialing",
	[EVT_CLIP] = "clip",
	[EVT_INBAND_RING] = "inband_ring",
	[EVT_CODEC_IDS] = "codec_ids",
	[EVT_CODEC_NEGOTIATE] = "codec_negotiate",
	[EVT_AUDIO_CONNECT_REQ] = "audio_connect_req",
	[EVT_ECNR_TURN_OFF] = "ecnr_turn_off",
	[EVT_CALL_WAITING] = "call_waiting",
	[EVT_EXPLICIT_CALL_TRANSFER] = "explicit_call_transfer",
	[EVT_VOICE_RECOGNITION] = "voice_recognition",
	[EVT_VRE_STATE] = "vre_state",
	[EVT_TEXTUAL_REPRESENTATION] = "textual_representation",
	[EVT_READY_TO_ACCEPT_AUDIO] = "ready_to_accept_audio",
	[EVT_REQUEST_PHONE_NUMBER] = "request_phone_number",
	[EVT_DTMF_CODE] = "dtmf_code",
	[EVT_SUBSCRIBER_NUMBER] = "subscriber_number",
	[EVT_HF_INDICATOR_VALUE] = "hf_indicator_value",
	[EVT_QUERY_CALL] = "query_call",
	[EVT_NUMBER_CALL] = "number_call",
	[EVT_MEMORY_DIAL] = "memory_dial",
	[EVT_REDIAL] = "redial",
	[EVT_GET_ONGOING_CALL] = "get_ongoing_call",
};

BUILD_ASSERT(ARRAY_SIZE(evt_names) == EVT_COUNT);

const char *test_evt_str(enum test_evt type)
{
	return ((type < EVT_COUNT) && (evt_names[type] != NULL)) ? evt_names[type] : "?";
}

void test_evt_reset(void)
{
	k_mutex_lock(&evt_lock, K_FOREVER);
	memset(evt_used, 0, sizeof(evt_used));
	k_mutex_unlock(&evt_lock);
}

void test_evt_post(enum test_evt type, void *obj, int32_t value, int32_t value2, const char *str)
{
	size_t slot = EVT_SLOTS;

	LOG_DBG("event %s obj %p value %d %d %s", test_evt_str(type), obj, value, value2,
		(str != NULL) ? str : "");

	k_mutex_lock(&evt_lock, K_FOREVER);

	for (size_t i = 0; i < EVT_SLOTS; i++) {
		if (!evt_used[i]) {
			slot = i;
			break;
		}
	}

	if (slot == EVT_SLOTS) {
		k_mutex_unlock(&evt_lock);
		LOG_ERR("Event store full, %s dropped", test_evt_str(type));
		return;
	}

	evt_slots[slot].type = type;
	evt_slots[slot].obj = obj;
	evt_slots[slot].value = value;
	evt_slots[slot].value2 = value2;
	evt_slots[slot].str[0] = '\0';
	if (str != NULL) {
		strncpy(evt_slots[slot].str, str, sizeof(evt_slots[slot].str) - 1);
		evt_slots[slot].str[sizeof(evt_slots[slot].str) - 1] = '\0';
	}
	evt_seq[slot] = evt_next_seq++;
	evt_used[slot] = true;

	k_condvar_broadcast(&evt_cond);
	k_mutex_unlock(&evt_lock);
}

/* Index of the oldest unconsumed event of @p type, or -1. Called with evt_lock held. */
static int evt_find(enum test_evt type)
{
	int found = -1;

	for (int i = 0; i < EVT_SLOTS; i++) {
		if (evt_used[i] && (evt_slots[i].type == type) &&
		    ((found < 0) || ((int32_t)(evt_seq[i] - evt_seq[found]) < 0))) {
			found = i;
		}
	}

	return found;
}

int test_evt_wait(enum test_evt type, k_timeout_t timeout, struct test_event *evt)
{
	k_timepoint_t end = sys_timepoint_calc(timeout);
	int idx;

	k_mutex_lock(&evt_lock, K_FOREVER);

	while ((idx = evt_find(type)) < 0) {
		if (!test_br_connected()) {
			k_mutex_unlock(&evt_lock);
			return -ENOTCONN;
		}

		if (sys_timepoint_expired(end)) {
			k_mutex_unlock(&evt_lock);
			return -EAGAIN;
		}

		(void)k_condvar_wait(&evt_cond, &evt_lock, K_MSEC(50));
	}

	if (evt != NULL) {
		*evt = evt_slots[idx];
	}
	evt_used[idx] = false;

	k_mutex_unlock(&evt_lock);

	return 0;
}

int test_evt_wait_value(enum test_evt type, int32_t value, k_timeout_t timeout,
			struct test_event *evt)
{
	k_timepoint_t end = sys_timepoint_calc(timeout);
	struct test_event e;
	int err;

	do {
		err = test_evt_wait(type, sys_timepoint_timeout(end), &e);
		if (err != 0) {
			return err;
		}
	} while (e.value != value);

	if (evt != NULL) {
		*evt = e;
	}

	return 0;
}

int test_evt_wait_obj(enum test_evt type, const void *obj, k_timeout_t timeout,
		      struct test_event *evt)
{
	k_timepoint_t end = sys_timepoint_calc(timeout);
	struct test_event e;
	int err;

	do {
		err = test_evt_wait(type, sys_timepoint_timeout(end), &e);
		if (err != 0) {
			return err;
		}
	} while (e.obj != obj);

	if (evt != NULL) {
		*evt = e;
	}

	return 0;
}

bool test_evt_pending(enum test_evt type)
{
	bool pending;

	k_mutex_lock(&evt_lock, K_FOREVER);
	pending = evt_find(type) >= 0;
	k_mutex_unlock(&evt_lock);

	return pending;
}

void test_evt_none(enum test_evt type, k_timeout_t timeout)
{
	struct test_event evt;
	int err;

	err = test_evt_wait(type, timeout, &evt);
	zassert_not_equal(err, 0, "Unexpected %s event (value %d)", test_evt_str(type), evt.value);
}

uint8_t test_sco_air_mode(struct bt_conn *sco_conn)
{
	struct bt_conn_info info;
	int err;

	err = bt_conn_get_info(sco_conn, &info);
	zassert_equal(err, 0, "Failed to get SCO info (err %d)", err);
	zassert_equal(info.type, BT_CONN_TYPE_SCO, "Not a SCO connection");

	return info.sco.air_mode;
}

#define SDP_CLIENT_USER_BUF_LEN 512
NET_BUF_POOL_FIXED_DEFINE(sdp_client_pool, 1, SDP_CLIENT_USER_BUF_LEN,
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

static struct bt_uuid_16 sdp_uuid;
static struct bt_sdp_discover_params sdp_discover_params;
static K_SEM_DEFINE(sdp_discover_sem, 0, 1);
static int sdp_discover_err;
static uint16_t sdp_rfcomm_channel;

static uint8_t sdp_discover_cb(struct bt_conn *conn, struct bt_sdp_client_result *result,
			       const struct bt_sdp_discover_params *params)
{
	uint16_t channel;
	int err;

	if ((result == NULL) || (result->resp_buf == NULL) || (result->resp_buf->len == 0)) {
		LOG_DBG("No (more) SDP record found");
		if (sdp_rfcomm_channel == 0) {
			sdp_discover_err = -ENOENT;
		}
		k_sem_give(&sdp_discover_sem);
		return BT_SDP_DISCOVER_UUID_STOP;
	}

	err = bt_sdp_get_proto_param(result->resp_buf, BT_SDP_PROTO_RFCOMM, &channel);
	if (err != 0) {
		LOG_ERR("No RFCOMM channel in SDP record (err %d)", err);
		sdp_discover_err = err;
	} else {
		LOG_DBG("RFCOMM channel %u", channel);
		sdp_rfcomm_channel = channel;
		sdp_discover_err = 0;
	}

	k_sem_give(&sdp_discover_sem);
	return BT_SDP_DISCOVER_UUID_STOP;
}

uint8_t test_sdp_get_rfcomm_channel(struct bt_conn *conn, uint16_t svclass)
{
	int err;

	sdp_uuid.uuid.type = BT_UUID_TYPE_16;
	sdp_uuid.val = svclass;

	sdp_rfcomm_channel = 0;
	sdp_discover_err = -EIO;

	sdp_discover_params.uuid = &sdp_uuid.uuid;
	sdp_discover_params.func = sdp_discover_cb;
	sdp_discover_params.pool = &sdp_client_pool;
	sdp_discover_params.type = BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR;
	sdp_discover_params.ids = NULL;

	k_sem_reset(&sdp_discover_sem);
	err = bt_sdp_discover(conn, &sdp_discover_params);
	zassert_equal(err, 0, "Failed to start SDP discovery (err %d)", err);

	err = test_br_wait(&sdp_discover_sem, TEST_HFP_TIMEOUT);
	zassert_equal(err, 0, "SDP discovery timeout (err %d)", err);
	zassert_equal(sdp_discover_err, 0, "SDP discovery failed (err %d)", sdp_discover_err);
	zassert_true((sdp_rfcomm_channel > 0) && (sdp_rfcomm_channel <= 30),
		     "Invalid RFCOMM channel %u", sdp_rfcomm_channel);

	return (uint8_t)sdp_rfcomm_channel;
}
