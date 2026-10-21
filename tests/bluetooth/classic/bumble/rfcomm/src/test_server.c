/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"

LOG_MODULE_REGISTER(test_rfcomm_server, CONFIG_TEST_CLASSIC_RFCOMM_SIM_LOG_LEVEL);

struct test_server {
	struct bt_rfcomm_server server;
	struct test_dlc *td;
	atomic_t accept_count;
	bool registered;
};

static struct test_dlc dlc_a;
static struct test_dlc dlc_b;
static struct test_dlc dlc_rev;

static struct test_server server_a;
static struct test_server server_b;

static int server_accept(struct bt_conn *conn, struct bt_rfcomm_server *server,
			 struct bt_rfcomm_dlc **dlc)
{
	struct test_server *ts = CONTAINER_OF(server, struct test_server, server);

	LOG_DBG("Incoming DLC on channel %u (conn %p)", server->channel, (void *)conn);

	if (atomic_inc(&ts->accept_count) != 0) {
		LOG_ERR("Channel %u already accepted a DLC", server->channel);
		return -ENOMEM;
	}

	*dlc = &ts->td->dlc;
	return 0;
}

static void server_register(struct test_server *ts, uint8_t channel, struct test_dlc *td)
{
	int err;

	ts->server.channel = channel;
	ts->server.accept = server_accept;
	ts->td = td;
	atomic_set(&ts->accept_count, 0);

	err = bt_rfcomm_server_register(&ts->server);
	zassert_equal(err, 0, "Failed to register channel %u (err %d)", channel, err);
	ts->registered = true;
}

static void server_unregister(struct test_server *ts)
{
	int err;

	if (!ts->registered) {
		return;
	}

	err = bt_rfcomm_server_unregister(&ts->server);
	if (err != 0) {
		LOG_ERR("Failed to unregister channel %u (err %d)", ts->server.channel, err);
	}
	ts->registered = false;
}

static void test_server_register_api(void)
{
	struct bt_rfcomm_server tmp = {0};
	int err;

	/* No accept callback */
	tmp.channel = TEST_CHANNEL_B;
	err = bt_rfcomm_server_register(&tmp);
	zassert_equal(err, -EINVAL, "Register without accept returned %d", err);

	/* Channel above the valid range */
	tmp.channel = 31;
	tmp.accept = server_accept;
	err = bt_rfcomm_server_register(&tmp);
	zassert_equal(err, -EINVAL, "Register of channel 31 returned %d", err);

	/* Channel already registered */
	tmp.channel = TEST_CHANNEL_A;
	err = bt_rfcomm_server_register(&tmp);
	zassert_equal(err, -EADDRINUSE, "Duplicate register returned %d", err);

	/* Channel 0 requests a dynamically allocated channel */
	tmp.channel = 0;
	err = bt_rfcomm_server_register(&tmp);
	zassert_equal(err, 0, "Dynamic register failed (err %d)", err);
	zassert_true(tmp.channel >= BT_RFCOMM_CHAN_DYNAMIC_START && tmp.channel <= 30,
		     "Dynamic channel %u out of range", tmp.channel);
	zassert_not_equal(tmp.channel, TEST_CHANNEL_A, "Dynamic channel reused channel A");

	err = bt_rfcomm_server_unregister(&tmp);
	zassert_equal(err, 0, "Unregister failed (err %d)", err);

	err = bt_rfcomm_server_unregister(&tmp);
	zassert_equal(err, -ENOENT, "Second unregister returned %d", err);
}

ZTEST(rfcomm_server, test_001_basic)
{
	server_register(&server_a, TEST_CHANNEL_A, &dlc_a);
	test_server_register_api();

	test_br_sync();

	test_dlc_wait_connected(&dlc_a);

	test_dlc_expect_pattern(&dlc_a, TEST_BASIC_C2S_LEN, TEST_BASIC_C2S_SEED);
	test_dlc_send_pattern(&dlc_a, TEST_BASIC_S2C_LEN, TEST_BASIC_S2C_SEED);
	test_dlc_wait_sent(&dlc_a, 1);

	/* The client disconnects the DLC. */
	test_dlc_wait_disconnected(&dlc_a);

	zassert_equal(atomic_get(&server_a.accept_count), 1, "Unexpected accept count");
}

ZTEST(rfcomm_server, test_002_unregistered_channel)
{
	server_register(&server_a, TEST_CHANNEL_A, &dlc_a);

	test_br_sync();

	/* The DLC to the unregistered channel is refused by the stack without
	 * reaching any accept callback; the client then connects to channel A.
	 */
	test_dlc_wait_connected(&dlc_a);
	test_dlc_expect_pattern(&dlc_a, TEST_BASIC_C2S_LEN, TEST_BASIC_C2S_SEED);
	test_dlc_send_pattern(&dlc_a, TEST_BASIC_S2C_LEN, TEST_BASIC_S2C_SEED);

	test_dlc_wait_disconnected(&dlc_a);

	zassert_equal(atomic_get(&server_a.accept_count), 1, "Unexpected accept count");
}

ZTEST(rfcomm_server, test_003_multiple_dlcs)
{
	int err;

	server_register(&server_a, TEST_CHANNEL_A, &dlc_a);
	server_register(&server_b, TEST_CHANNEL_B, &dlc_b);

	test_br_sync();

	test_dlc_wait_connected(&dlc_a);
	test_dlc_wait_connected(&dlc_b);

	/* Each DLC must receive only its own payload. */
	test_dlc_expect_pattern(&dlc_a, TEST_MULTI_A_C2S_LEN, TEST_MULTI_A_C2S_SEED);
	test_dlc_expect_pattern(&dlc_b, TEST_MULTI_B_C2S_LEN, TEST_MULTI_B_C2S_SEED);

	test_dlc_send_pattern(&dlc_a, TEST_MULTI_A_S2C_LEN, TEST_MULTI_A_S2C_SEED);
	test_dlc_send_pattern(&dlc_b, TEST_MULTI_B_S2C_LEN, TEST_MULTI_B_S2C_SEED);
	test_dlc_wait_sent(&dlc_a, 1);
	test_dlc_wait_sent(&dlc_b, 1);

	/* The acceptor closes DLC B; DLC A must stay usable. */
	err = bt_rfcomm_dlc_disconnect(&dlc_b.dlc);
	zassert_equal(err, 0, "Failed to disconnect DLC B (err %d)", err);
	test_dlc_wait_disconnected(&dlc_b);
	zassert_true(atomic_test_bit(dlc_a.flags, TEST_DLC_FLAG_CONNECTED),
		     "DLC A disconnected together with DLC B");

	test_dlc_expect_pattern(&dlc_a, TEST_MULTI_A_LAST_LEN, TEST_MULTI_A_LAST_SEED);

	test_dlc_wait_disconnected(&dlc_a);

	zassert_equal(atomic_get(&dlc_a.rx_count), 2, "DLC A received %ld frames",
		      (long)atomic_get(&dlc_a.rx_count));
	zassert_equal(atomic_get(&dlc_b.rx_count), 1, "DLC B received %ld frames",
		      (long)atomic_get(&dlc_b.rx_count));
}

ZTEST(rfcomm_server, test_004_mtu_and_credits)
{
	uint16_t mtu;

	server_register(&server_a, TEST_CHANNEL_A, &dlc_a);

	test_br_sync();

	test_dlc_wait_connected(&dlc_a);
	mtu = dlc_a.dlc.mtu;
	LOG_DBG("Negotiated MTU %u", mtu);

	for (uint32_t i = 0; i < CONFIG_TEST_RFCOMM_FRAME_COUNT; i++) {
		test_dlc_expect_pattern(&dlc_a, test_mtu_frame_len(i, mtu),
					(uint8_t)(TEST_MTU_C2S_SEED + i));
	}

	for (uint32_t i = 0; i < CONFIG_TEST_RFCOMM_FRAME_COUNT; i++) {
		test_dlc_send_pattern(&dlc_a, test_mtu_frame_len(i, mtu),
				      (uint8_t)(TEST_MTU_S2C_SEED + i));
	}
	test_dlc_wait_sent(&dlc_a, CONFIG_TEST_RFCOMM_FRAME_COUNT);

	test_dlc_wait_disconnected(&dlc_a);
}

ZTEST(rfcomm_server, test_005_deferred_recv)
{
	struct net_buf *buf;
	int err;

	server_register(&server_a, TEST_CHANNEL_A, &dlc_a);
	/* Small RX credit window, and keep every received buffer. */
	dlc_a.dlc.rx_credit_limit = TEST_HOLD_CREDITS;
	atomic_set_bit(dlc_a.flags, TEST_DLC_FLAG_HOLD_RX);

	test_br_sync();

	test_dlc_wait_connected(&dlc_a);
	zassert_equal(dlc_a.dlc.rx_credit_limit, TEST_HOLD_CREDITS, "RX credit limit %u",
		      dlc_a.dlc.rx_credit_limit);

	for (uint32_t i = 0; i < TEST_HOLD_CREDITS; i++) {
		err = test_br_wait(&dlc_a.rx_sem, TEST_RX_TIMEOUT);
		zassert_equal(err, 0, "Held frame %u not received (err %d)", i, err);
	}

	/* No more frames may arrive while all credits are held. */
	err = k_sem_take(&dlc_a.rx_sem, K_SECONDS(2));
	zassert_equal(err, -EAGAIN, "Frame received while credits were held");
	zassert_equal(atomic_get(&dlc_a.rx_count), TEST_HOLD_CREDITS,
		      "Received %ld frames while credits were held",
		      (long)atomic_get(&dlc_a.rx_count));

	err = bt_rfcomm_dlc_recv_complete(NULL, NULL);
	zassert_equal(err, -EINVAL, "recv_complete(NULL) returned %d", err);
	err = bt_rfcomm_dlc_recv_complete(&dlc_a.dlc, NULL);
	zassert_equal(err, -EINVAL, "recv_complete(buf NULL) returned %d", err);

	/* Complete the held buffers and consume the rest synchronously. */
	atomic_clear_bit(dlc_a.flags, TEST_DLC_FLAG_HOLD_RX);
	test_dlc_release_held(&dlc_a);

	/* The held frames were already counted by rx_sem above. */
	for (uint32_t i = 0; i < TEST_HOLD_CREDITS; i++) {
		k_sem_give(&dlc_a.rx_sem);
	}

	for (uint32_t i = 0; i < TEST_HOLD_FRAMES; i++) {
		test_dlc_expect_pattern(&dlc_a, TEST_HOLD_LEN, (uint8_t)(TEST_HOLD_C2S_SEED + i));
	}

	buf = k_fifo_get(&dlc_a.held_fifo, K_NO_WAIT);
	zassert_is_null(buf, "Buffer still held after release");

	test_dlc_send_pattern(&dlc_a, TEST_HOLD_DONE_LEN, TEST_HOLD_DONE_SEED);

	test_dlc_wait_disconnected(&dlc_a);
}

ZTEST(rfcomm_server, test_006_rpn_rls)
{
	int err;

	server_register(&server_a, TEST_CHANNEL_A, &dlc_a);

	test_br_sync();

	test_dlc_wait_connected(&dlc_a);

	test_dlc_expect_pattern(&dlc_a, TEST_BASIC_C2S_LEN, TEST_BASIC_C2S_SEED);

	/* The acceptor side sends an RLS command too before replying. */
	err = bt_rfcomm_send_rls_cmd(&dlc_a.dlc,
				     BT_RFCOMM_RLS_ERR(BT_RFCOMM_RLS_ERR_OVERRUN_ERROR));
	zassert_equal(err, 0, "Failed to send RLS (err %d)", err);

	test_dlc_send_pattern(&dlc_a, TEST_BASIC_S2C_LEN, TEST_BASIC_S2C_SEED);

	test_dlc_wait_disconnected(&dlc_a);
}

ZTEST(rfcomm_server, test_007_acceptor_initiated_dlc)
{
	int err;

	server_register(&server_a, TEST_CHANNEL_A, &dlc_a);

	test_br_sync();

	test_dlc_wait_connected(&dlc_a);

	/* The client sends on DLC A once its own server channel is ready. */
	test_dlc_expect_pattern(&dlc_a, TEST_BASIC_C2S_LEN, TEST_BASIC_C2S_SEED);

	/* Open a DLC from this side over the session the client created. */
	err = bt_rfcomm_dlc_connect(test_br_conn(), &dlc_rev.dlc, TEST_CHANNEL_CLIENT);
	zassert_equal(err, 0, "Failed to connect reverse DLC (err %d)", err);
	test_dlc_wait_connected(&dlc_rev);

	test_dlc_send_pattern(&dlc_rev, TEST_REV_S2C_LEN, TEST_REV_S2C_SEED);
	test_dlc_expect_pattern(&dlc_rev, TEST_REV_C2S_LEN, TEST_REV_C2S_SEED);

	err = bt_rfcomm_dlc_disconnect(&dlc_rev.dlc);
	zassert_equal(err, 0, "Failed to disconnect reverse DLC (err %d)", err);
	test_dlc_wait_disconnected(&dlc_rev);

	test_dlc_send_pattern(&dlc_a, TEST_REV_DONE_LEN, TEST_REV_DONE_SEED);

	test_dlc_wait_disconnected(&dlc_a);
}

static void *setup(void)
{
	test_br_setup(TEST_BR_ACCEPTOR, BT_SECURITY_L1);
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
	test_dlc_init(&dlc_rev);
}

static void after(void *f)
{
	server_unregister(&server_a);
	server_unregister(&server_b);
}

ZTEST_SUITE(rfcomm_server, NULL, setup, before, after, teardown);
