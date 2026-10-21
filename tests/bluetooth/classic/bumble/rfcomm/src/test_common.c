/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"

LOG_MODULE_REGISTER(test_rfcomm_common, CONFIG_TEST_CLASSIC_RFCOMM_SIM_LOG_LEVEL);

NET_BUF_POOL_FIXED_DEFINE(test_tx_pool, CONFIG_TEST_RFCOMM_TX_POOL_COUNT,
			  BT_RFCOMM_BUF_SIZE(TEST_MTU), CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

void test_fill_pattern(uint8_t *data, size_t len, uint8_t seed)
{
	for (size_t i = 0; i < len; i++) {
		data[i] = (uint8_t)(seed + (i * 3U) + (i >> 8));
	}
}

uint16_t test_mtu_frame_len(uint32_t index, uint16_t mtu)
{
	/* A one-octet frame, 127 and 128 octets (the stack encodes the UIH
	 * length in one octet up to 127 and in two octets above that) and a
	 * frame of the full DLC MTU (0 in the table).
	 */
	static const uint16_t lens[] = {1, 127, 128, 0};
	uint16_t len = lens[index % ARRAY_SIZE(lens)];

	return (len == 0 || len > mtu) ? mtu : len;
}

static void test_dlc_flush_held(struct test_dlc *td)
{
	struct net_buf *buf;

	while ((buf = k_fifo_get(&td->held_fifo, K_NO_WAIT)) != NULL) {
		net_buf_unref(buf);
	}
}

static void dlc_connected(struct bt_rfcomm_dlc *dlc)
{
	struct test_dlc *td = CONTAINER_OF(dlc, struct test_dlc, dlc);

	LOG_DBG("DLC %p connected, mtu %u", (void *)dlc, dlc->mtu);

	atomic_set_bit(td->flags, TEST_DLC_FLAG_CONNECTED);
	atomic_set_bit(td->flags, TEST_DLC_FLAG_EVER_CONNECTED);
	k_sem_give(&td->connected_sem);
}

static void dlc_disconnected(struct bt_rfcomm_dlc *dlc)
{
	struct test_dlc *td = CONTAINER_OF(dlc, struct test_dlc, dlc);

	LOG_DBG("DLC %p disconnected", (void *)dlc);

	atomic_clear_bit(td->flags, TEST_DLC_FLAG_CONNECTED);
	/* The application owns held buffers once the DLC is gone. */
	test_dlc_flush_held(td);
	k_sem_give(&td->disconnected_sem);
}

static int dlc_recv(struct bt_rfcomm_dlc *dlc, struct net_buf *buf)
{
	struct test_dlc *td = CONTAINER_OF(dlc, struct test_dlc, dlc);
	atomic_val_t slot;

	LOG_DBG("DLC %p received %u bytes", (void *)dlc, buf->len);

	slot = atomic_get(&td->rx_count);
	if (slot >= TEST_RX_SLOTS || buf->len > TEST_MTU) {
		LOG_ERR("RX overflow (slot %ld len %u)", (long)slot, buf->len);
		atomic_set_bit(td->flags, TEST_DLC_FLAG_RX_OVERFLOW);
		k_sem_give(&td->rx_sem);
		return 0;
	}

	td->rx_len[slot] = buf->len;
	memcpy(td->rx_data[slot], buf->data, buf->len);
	atomic_inc(&td->rx_count);

	if (atomic_test_bit(td->flags, TEST_DLC_FLAG_HOLD_RX)) {
		k_fifo_put(&td->held_fifo, buf);
		k_sem_give(&td->rx_sem);
		return -EINPROGRESS;
	}

	k_sem_give(&td->rx_sem);
	return 0;
}

static void dlc_sent(struct bt_rfcomm_dlc *dlc, int err)
{
	struct test_dlc *td = CONTAINER_OF(dlc, struct test_dlc, dlc);

	if (err != 0) {
		LOG_ERR("DLC %p send failed (err %d)", (void *)dlc, err);
		atomic_inc(&td->sent_err_count);
	}

	atomic_inc(&td->sent_count);
	k_sem_give(&td->sent_sem);
}

static struct bt_rfcomm_dlc_ops test_dlc_ops = {
	.connected = dlc_connected,
	.disconnected = dlc_disconnected,
	.recv = dlc_recv,
	.sent = dlc_sent,
};

void test_dlc_init(struct test_dlc *td)
{
	memset(td, 0, sizeof(*td));

	k_sem_init(&td->connected_sem, 0, 1);
	k_sem_init(&td->disconnected_sem, 0, 1);
	k_sem_init(&td->rx_sem, 0, K_SEM_MAX_LIMIT);
	k_sem_init(&td->sent_sem, 0, K_SEM_MAX_LIMIT);
	k_fifo_init(&td->held_fifo);

	td->dlc.ops = &test_dlc_ops;
	td->dlc.mtu = TEST_MTU;
	td->dlc.required_sec_level = BT_SECURITY_L1;
}

void test_dlc_release_held(struct test_dlc *td)
{
	struct net_buf *buf;
	int err;

	while ((buf = k_fifo_get(&td->held_fifo, K_NO_WAIT)) != NULL) {
		err = bt_rfcomm_dlc_recv_complete(&td->dlc, buf);
		zassert_equal(err, 0, "Failed to complete RX (err %d)", err);
	}
}

void test_dlc_wait_connected(struct test_dlc *td)
{
	int err;

	err = test_br_wait(&td->connected_sem, TEST_DLC_TIMEOUT);
	zassert_equal(err, 0, "DLC connection timeout (err %d)", err);
	zassert_true(atomic_test_bit(td->flags, TEST_DLC_FLAG_CONNECTED), "DLC not connected");
	zassert_true(td->dlc.mtu >= TEST_MIN_MTU && td->dlc.mtu <= TEST_MTU,
		     "Unexpected DLC MTU %u", td->dlc.mtu);
}

void test_dlc_wait_disconnected(struct test_dlc *td)
{
	int err;

	err = k_sem_take(&td->disconnected_sem, TEST_DLC_TIMEOUT);
	zassert_equal(err, 0, "DLC disconnection timeout (err %d)", err);
	zassert_false(atomic_test_bit(td->flags, TEST_DLC_FLAG_CONNECTED), "DLC still connected");
}

int test_dlc_try_send(struct test_dlc *td, uint16_t len, uint8_t seed)
{
	struct net_buf *buf;
	int err;

	buf = bt_rfcomm_create_pdu(&test_tx_pool);
	zassert_not_null(buf, "Failed to allocate TX buffer");
	zassert_true(net_buf_tailroom(buf) >= len + BT_RFCOMM_FCS_SIZE,
		     "TX buffer too small (%zu < %u)", net_buf_tailroom(buf), len);

	test_fill_pattern(net_buf_add(buf, len), len, seed);

	err = bt_rfcomm_dlc_send(&td->dlc, buf);
	if (err < 0) {
		net_buf_unref(buf);
	}

	return err;
}

void test_dlc_send_pattern(struct test_dlc *td, uint16_t len, uint8_t seed)
{
	int err;

	err = test_dlc_try_send(td, len, seed);
	zassert_true(err >= 0, "Failed to send %u bytes (err %d)", len, err);
}

void test_dlc_expect_pattern(struct test_dlc *td, uint16_t len, uint8_t seed)
{
	static uint8_t expected[TEST_MTU];
	uint32_t idx;
	int err;

	zassert_true(len <= sizeof(expected), "Expected length %u too large", len);

	err = test_br_wait(&td->rx_sem, TEST_RX_TIMEOUT);
	zassert_equal(err, 0, "RX timeout waiting for %u bytes (err %d)", len, err);
	zassert_false(atomic_test_bit(td->flags, TEST_DLC_FLAG_RX_OVERFLOW), "RX overflow");

	idx = td->rx_read++;
	zassert_true(idx < (uint32_t)atomic_get(&td->rx_count), "No RX frame recorded");
	zassert_equal(td->rx_len[idx], len, "RX frame %u length %u != %u", idx, td->rx_len[idx],
		      len);

	test_fill_pattern(expected, len, seed);
	zassert_mem_equal(td->rx_data[idx], expected, len, "RX frame %u payload mismatch", idx);
}

void test_dlc_wait_sent(struct test_dlc *td, uint32_t count)
{
	int err;

	while ((uint32_t)atomic_get(&td->sent_count) < count) {
		err = test_br_wait(&td->sent_sem, TEST_RX_TIMEOUT);
		zassert_equal(err, 0, "TX complete timeout (%ld of %u sent)",
			      (long)atomic_get(&td->sent_count), count);
	}

	zassert_equal(atomic_get(&td->sent_err_count), 0, "TX completed with errors");
}
