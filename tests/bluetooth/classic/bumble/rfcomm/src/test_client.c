/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"

LOG_MODULE_REGISTER(test_rfcomm_client, CONFIG_TEST_CLASSIC_RFCOMM_SIM_LOG_LEVEL);

static struct test_dlc dlc_a;
static struct test_dlc dlc_b;
static struct test_dlc dlc_extra;
static struct test_dlc dlc_rev;

static atomic_t client_accept_count;
static bool client_server_registered;

static int client_accept(struct bt_conn *conn, struct bt_rfcomm_server *server,
			 struct bt_rfcomm_dlc **dlc)
{
	LOG_DBG("Incoming DLC on channel %u (conn %p)", server->channel, (void *)conn);

	if (atomic_inc(&client_accept_count) != 0) {
		LOG_ERR("Channel %u already accepted a DLC", server->channel);
		return -ENOMEM;
	}

	*dlc = &dlc_rev.dlc;
	return 0;
}

static struct bt_rfcomm_server client_server = {
	.channel = TEST_CHANNEL_CLIENT,
	.accept = client_accept,
};

static void dlc_connect(struct test_dlc *td, uint8_t channel)
{
	int err;

	err = bt_rfcomm_dlc_connect(test_br_conn(), &td->dlc, channel);
	zassert_equal(err, 0, "Failed to connect DLC to channel %u (err %d)", channel, err);
	test_dlc_wait_connected(td);
}

static void dlc_disconnect(struct test_dlc *td)
{
	int err;

	err = bt_rfcomm_dlc_disconnect(&td->dlc);
	zassert_equal(err, 0, "Failed to disconnect DLC (err %d)", err);
	test_dlc_wait_disconnected(td);
}

static void test_connect_api(void)
{
	int err;

	err = bt_rfcomm_dlc_connect(test_br_conn(), NULL, TEST_CHANNEL_A);
	zassert_equal(err, -EINVAL, "Connect with NULL DLC returned %d", err);

	err = bt_rfcomm_dlc_connect(test_br_conn(), &dlc_extra.dlc, 0);
	zassert_equal(err, -EINVAL, "Connect to channel 0 returned %d", err);

	err = bt_rfcomm_dlc_connect(test_br_conn(), &dlc_extra.dlc, 31);
	zassert_equal(err, -EINVAL, "Connect to channel 31 returned %d", err);

	dlc_extra.dlc.mtu = 0;
	err = bt_rfcomm_dlc_connect(test_br_conn(), &dlc_extra.dlc, TEST_CHANNEL_A);
	zassert_equal(err, -EINVAL, "Connect with MTU 0 returned %d", err);
	dlc_extra.dlc.mtu = TEST_MTU;
}

ZTEST(rfcomm_client, test_001_basic)
{
	int err;

	err = bt_rfcomm_dlc_connect(NULL, &dlc_a.dlc, TEST_CHANNEL_A);
	zassert_equal(err, -ENOTCONN, "Connect without ACL returned %d", err);

	test_br_sync();
	test_connect_api();

	err = test_dlc_try_send(&dlc_a, TEST_BASIC_C2S_LEN, TEST_BASIC_C2S_SEED);
	zassert_equal(err, -ENOTCONN, "Send on unconnected DLC returned %d", err);

	dlc_connect(&dlc_a, TEST_CHANNEL_A);

	test_dlc_send_pattern(&dlc_a, TEST_BASIC_C2S_LEN, TEST_BASIC_C2S_SEED);
	test_dlc_wait_sent(&dlc_a, 1);
	test_dlc_expect_pattern(&dlc_a, TEST_BASIC_S2C_LEN, TEST_BASIC_S2C_SEED);

	dlc_disconnect(&dlc_a);

	err = test_dlc_try_send(&dlc_a, TEST_BASIC_C2S_LEN, TEST_BASIC_C2S_SEED);
	zassert_equal(err, -ENOTCONN, "Send after disconnect returned %d", err);
}

ZTEST(rfcomm_client, test_002_unregistered_channel)
{
	int err;

	test_br_sync();

	err = bt_rfcomm_dlc_connect(test_br_conn(), &dlc_extra.dlc, TEST_CHANNEL_UNREGISTERED);
	zassert_equal(err, 0, "Failed to start DLC connection (err %d)", err);

	/* The peer refuses the DLC: only the disconnected callback is called. */
	test_dlc_wait_disconnected(&dlc_extra);
	zassert_false(atomic_test_bit(dlc_extra.flags, TEST_DLC_FLAG_EVER_CONNECTED),
		      "DLC to unregistered channel was connected");

	/* The refused DLC was the only one, so the stack closes the RFCOMM
	 * session. Give the session time to be released before opening a new
	 * DLC, since bt_rfcomm_dlc_connect() fails while it is still closing.
	 */
	k_sleep(TEST_SESSION_RELEASE_DELAY);

	dlc_connect(&dlc_a, TEST_CHANNEL_A);

	test_dlc_send_pattern(&dlc_a, TEST_BASIC_C2S_LEN, TEST_BASIC_C2S_SEED);
	test_dlc_expect_pattern(&dlc_a, TEST_BASIC_S2C_LEN, TEST_BASIC_S2C_SEED);

	dlc_disconnect(&dlc_a);
}

ZTEST(rfcomm_client, test_003_multiple_dlcs)
{
	int err;

	test_br_sync();

	dlc_connect(&dlc_a, TEST_CHANNEL_A);

	/* A second DLC to a channel that is already in use is rejected. */
	err = bt_rfcomm_dlc_connect(test_br_conn(), &dlc_extra.dlc, TEST_CHANNEL_A);
	zassert_equal(err, -EBUSY, "Second DLC to channel A returned %d", err);

	dlc_connect(&dlc_b, TEST_CHANNEL_B);

	test_dlc_send_pattern(&dlc_a, TEST_MULTI_A_C2S_LEN, TEST_MULTI_A_C2S_SEED);
	test_dlc_send_pattern(&dlc_b, TEST_MULTI_B_C2S_LEN, TEST_MULTI_B_C2S_SEED);

	test_dlc_expect_pattern(&dlc_a, TEST_MULTI_A_S2C_LEN, TEST_MULTI_A_S2C_SEED);
	test_dlc_expect_pattern(&dlc_b, TEST_MULTI_B_S2C_LEN, TEST_MULTI_B_S2C_SEED);

	/* The server closes DLC B; DLC A must stay usable. */
	test_dlc_wait_disconnected(&dlc_b);
	zassert_true(atomic_test_bit(dlc_a.flags, TEST_DLC_FLAG_CONNECTED),
		     "DLC A disconnected together with DLC B");

	test_dlc_send_pattern(&dlc_a, TEST_MULTI_A_LAST_LEN, TEST_MULTI_A_LAST_SEED);
	test_dlc_wait_sent(&dlc_a, 2);

	dlc_disconnect(&dlc_a);

	zassert_equal(atomic_get(&dlc_a.rx_count), 1, "DLC A received %ld frames",
		      (long)atomic_get(&dlc_a.rx_count));
	zassert_equal(atomic_get(&dlc_b.rx_count), 1, "DLC B received %ld frames",
		      (long)atomic_get(&dlc_b.rx_count));
}

ZTEST(rfcomm_client, test_004_mtu_and_credits)
{
	uint16_t mtu;
	int err;

	test_br_sync();

	dlc_connect(&dlc_a, TEST_CHANNEL_A);
	mtu = dlc_a.dlc.mtu;
	LOG_DBG("Negotiated MTU %u", mtu);

	err = test_dlc_try_send(&dlc_a, mtu + 1, TEST_MTU_C2S_SEED);
	zassert_equal(err, -EMSGSIZE, "Send of MTU + 1 bytes returned %d", err);

	for (uint32_t i = 0; i < CONFIG_TEST_RFCOMM_FRAME_COUNT; i++) {
		test_dlc_send_pattern(&dlc_a, test_mtu_frame_len(i, mtu),
				      (uint8_t)(TEST_MTU_C2S_SEED + i));
	}
	test_dlc_wait_sent(&dlc_a, CONFIG_TEST_RFCOMM_FRAME_COUNT);

	for (uint32_t i = 0; i < CONFIG_TEST_RFCOMM_FRAME_COUNT; i++) {
		test_dlc_expect_pattern(&dlc_a, test_mtu_frame_len(i, mtu),
					(uint8_t)(TEST_MTU_S2C_SEED + i));
	}

	dlc_disconnect(&dlc_a);
}

ZTEST(rfcomm_client, test_005_deferred_recv)
{
	test_br_sync();

	dlc_connect(&dlc_a, TEST_CHANNEL_A);

	/* Queue more frames than the server grants credits for. */
	for (uint32_t i = 0; i < TEST_HOLD_FRAMES; i++) {
		test_dlc_send_pattern(&dlc_a, TEST_HOLD_LEN, (uint8_t)(TEST_HOLD_C2S_SEED + i));
	}

	/* All frames leave only after the server completes the held buffers. */
	test_dlc_wait_sent(&dlc_a, TEST_HOLD_FRAMES);
	test_dlc_expect_pattern(&dlc_a, TEST_HOLD_DONE_LEN, TEST_HOLD_DONE_SEED);

	dlc_disconnect(&dlc_a);
}

ZTEST(rfcomm_client, test_006_rpn_rls)
{
	struct bt_rfcomm_rpn rpn = {
		.baud_rate = BT_RFCOMM_RPN_BAUD_RATE_115200,
		.line_settings = BT_RFCOMM_SET_LINE_SETTINGS(BT_RFCOMM_RPN_DATA_BITS_8,
							     BT_RFCOMM_RPN_STOP_BITS_1,
							     BT_RFCOMM_RPN_PARITY_NONE),
		.flow_control = BT_RFCOMM_RPN_FLOW_NONE,
		.xon_char = BT_RFCOMM_RPN_XON_CHAR,
		.xoff_char = BT_RFCOMM_RPN_XOFF_CHAR,
		.param_mask = sys_cpu_to_le16(BT_RFCOMM_RPN_PARAM_MASK_ALL),
	};
	struct bt_rfcomm_rpn bad_rpn = rpn;
	int err;

	test_br_sync();

	err = bt_rfcomm_send_rpn_cmd(&dlc_a.dlc, &rpn);
	zassert_equal(err, -ENOTCONN, "RPN on unconnected DLC returned %d", err);
	err = bt_rfcomm_send_rls_cmd(&dlc_a.dlc, BT_RFCOMM_RLS_NO_ERR);
	zassert_equal(err, -ENOTCONN, "RLS on unconnected DLC returned %d", err);

	dlc_connect(&dlc_a, TEST_CHANNEL_A);

	/* Argument validation */
	err = bt_rfcomm_send_rpn_cmd(&dlc_a.dlc, NULL);
	zassert_equal(err, -EINVAL, "RPN with NULL params returned %d", err);
	bad_rpn.baud_rate = BT_RFCOMM_RPN_BAUD_RATE_230400 + 1;
	err = bt_rfcomm_send_rpn_cmd(&dlc_a.dlc, &bad_rpn);
	zassert_equal(err, -EINVAL, "RPN with invalid baud rate returned %d", err);
	err = bt_rfcomm_send_rls_cmd(NULL, BT_RFCOMM_RLS_NO_ERR);
	zassert_equal(err, -EINVAL, "RLS with NULL DLC returned %d", err);
	err = bt_rfcomm_send_rls_cmd(&dlc_a.dlc, BIT(4));
	zassert_equal(err, -EINVAL, "RLS with reserved bit returned %d", err);
	err = bt_rfcomm_send_rls_cmd(&dlc_a.dlc, BIT(0));
	zassert_equal(err, -EINVAL, "RLS with error flag but no error returned %d", err);

	/* Valid commands; the peer answers them inside its RFCOMM stack. */
	err = bt_rfcomm_send_rpn_cmd(&dlc_a.dlc, &rpn);
	zassert_equal(err, 0, "Failed to send RPN (err %d)", err);
	err = bt_rfcomm_send_rls_cmd(&dlc_a.dlc, BT_RFCOMM_RLS_NO_ERR);
	zassert_equal(err, 0, "Failed to send RLS (err %d)", err);
	err = bt_rfcomm_send_rls_cmd(&dlc_a.dlc, BT_RFCOMM_RLS_ERR(BT_RFCOMM_RLS_ERR_PARITY_ERROR));
	zassert_equal(err, 0, "Failed to send RLS with error (err %d)", err);

	/* Data still flows in both directions after the control commands. */
	test_dlc_send_pattern(&dlc_a, TEST_BASIC_C2S_LEN, TEST_BASIC_C2S_SEED);
	test_dlc_expect_pattern(&dlc_a, TEST_BASIC_S2C_LEN, TEST_BASIC_S2C_SEED);

	dlc_disconnect(&dlc_a);

	err = bt_rfcomm_send_rpn_cmd(&dlc_a.dlc, &rpn);
	zassert_equal(err, -ENOTCONN, "RPN after disconnect returned %d", err);
	err = bt_rfcomm_send_rls_cmd(&dlc_a.dlc, BT_RFCOMM_RLS_NO_ERR);
	zassert_equal(err, -ENOTCONN, "RLS after disconnect returned %d", err);
}

ZTEST(rfcomm_client, test_007_acceptor_initiated_dlc)
{
	int err;

	err = bt_rfcomm_server_register(&client_server);
	zassert_equal(err, 0, "Failed to register client channel (err %d)", err);
	client_server_registered = true;

	test_br_sync();

	dlc_connect(&dlc_a, TEST_CHANNEL_A);

	/* Tell the server that the client channel is ready. */
	test_dlc_send_pattern(&dlc_a, TEST_BASIC_C2S_LEN, TEST_BASIC_C2S_SEED);

	/* The server opens a DLC to this side over the same session. */
	test_dlc_wait_connected(&dlc_rev);
	zassert_equal(atomic_get(&client_accept_count), 1, "Unexpected accept count");

	test_dlc_expect_pattern(&dlc_rev, TEST_REV_S2C_LEN, TEST_REV_S2C_SEED);
	test_dlc_send_pattern(&dlc_rev, TEST_REV_C2S_LEN, TEST_REV_C2S_SEED);

	/* The server closes the reverse DLC; DLC A must stay usable. */
	test_dlc_wait_disconnected(&dlc_rev);
	test_dlc_expect_pattern(&dlc_a, TEST_REV_DONE_LEN, TEST_REV_DONE_SEED);

	dlc_disconnect(&dlc_a);
}

static void *setup(void)
{
	test_br_setup(TEST_BR_INITIATOR, BT_SECURITY_L1);
	return NULL;
}

static void teardown(void *f)
{
	test_br_teardown();
}

static void before(void *f)
{
	test_dlc_init(&dlc_a);
	test_dlc_init(&dlc_b);
	test_dlc_init(&dlc_extra);
	test_dlc_init(&dlc_rev);
	atomic_set(&client_accept_count, 0);
}

static void after(void *f)
{
	int err;

	if (client_server_registered) {
		err = bt_rfcomm_server_unregister(&client_server);
		if (err != 0) {
			LOG_ERR("Failed to unregister client channel (err %d)", err);
		}
		client_server_registered = false;
	}
}

ZTEST_SUITE(rfcomm_client, NULL, setup, before, after, teardown);
