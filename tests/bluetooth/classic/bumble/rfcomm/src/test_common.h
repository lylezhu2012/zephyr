/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_RFCOMM_COMMON_H_
#define TEST_RFCOMM_COMMON_H_

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include <zephyr/net_buf.h>

#define TEST_CHANNEL_A            CONFIG_TEST_RFCOMM_CHANNEL_A
#define TEST_CHANNEL_B            CONFIG_TEST_RFCOMM_CHANNEL_B
#define TEST_CHANNEL_CLIENT       CONFIG_TEST_RFCOMM_CHANNEL_CLIENT
#define TEST_CHANNEL_UNREGISTERED CONFIG_TEST_RFCOMM_CHANNEL_UNREGISTERED

/* MTU requested by both roles; the stack lowers it to what the session supports. */
#define TEST_MTU CONFIG_BT_RFCOMM_L2CAP_MTU

/* Smallest DLC MTU that the Zephyr RFCOMM stack accepts. */
#define TEST_MIN_MTU 23

/* Number of RX frames recorded per DLC. */
#define TEST_RX_SLOTS 24

#define TEST_DLC_TIMEOUT K_SECONDS(30)
#define TEST_RX_TIMEOUT  K_SECONDS(10)

/* Time for an RFCOMM session without DLCs to be torn down by the stack. */
#define TEST_SESSION_RELEASE_DELAY K_SECONDS(3)

/* Payload lengths and pattern seeds shared by both roles. */
#define TEST_BASIC_C2S_LEN  48
#define TEST_BASIC_C2S_SEED 0x10
#define TEST_BASIC_S2C_LEN  64
#define TEST_BASIC_S2C_SEED 0x20

#define TEST_MULTI_A_C2S_SEED  0x31
#define TEST_MULTI_B_C2S_SEED  0x32
#define TEST_MULTI_A_S2C_SEED  0x41
#define TEST_MULTI_B_S2C_SEED  0x42
#define TEST_MULTI_A_C2S_LEN   40
#define TEST_MULTI_B_C2S_LEN   50
#define TEST_MULTI_A_S2C_LEN   60
#define TEST_MULTI_B_S2C_LEN   70
#define TEST_MULTI_A_LAST_SEED 0x51
#define TEST_MULTI_A_LAST_LEN  33

#define TEST_MTU_C2S_SEED 0x60
#define TEST_MTU_S2C_SEED 0x90

/* Frames sent by the client while the server defers RX completion. */
#define TEST_HOLD_CREDITS   3
#define TEST_HOLD_FRAMES    8
#define TEST_HOLD_LEN       20
#define TEST_HOLD_C2S_SEED  0xa0
#define TEST_HOLD_DONE_SEED 0xb0
#define TEST_HOLD_DONE_LEN  10

#define TEST_REV_S2C_SEED  0xc0
#define TEST_REV_S2C_LEN   24
#define TEST_REV_C2S_SEED  0xd0
#define TEST_REV_C2S_LEN   36
#define TEST_REV_DONE_SEED 0xe0
#define TEST_REV_DONE_LEN  12

enum {
	TEST_DLC_FLAG_CONNECTED,
	TEST_DLC_FLAG_EVER_CONNECTED,
	TEST_DLC_FLAG_HOLD_RX,
	TEST_DLC_FLAG_RX_OVERFLOW,

	TEST_DLC_NUM_FLAGS,
};

struct test_dlc {
	struct bt_rfcomm_dlc dlc;
	struct k_sem connected_sem;
	struct k_sem disconnected_sem;
	struct k_sem rx_sem;
	struct k_sem sent_sem;
	/* Buffers whose processing was deferred with -EINPROGRESS. */
	struct k_fifo held_fifo;
	ATOMIC_DEFINE(flags, TEST_DLC_NUM_FLAGS);
	atomic_t rx_count;
	uint32_t rx_read;
	atomic_t sent_count;
	atomic_t sent_err_count;
	uint16_t rx_len[TEST_RX_SLOTS];
	uint8_t rx_data[TEST_RX_SLOTS][TEST_MTU];
};

void test_fill_pattern(uint8_t *data, size_t len, uint8_t seed);
uint16_t test_mtu_frame_len(uint32_t index, uint16_t mtu);

void test_dlc_init(struct test_dlc *td);
void test_dlc_release_held(struct test_dlc *td);
void test_dlc_wait_connected(struct test_dlc *td);
void test_dlc_wait_disconnected(struct test_dlc *td);
int test_dlc_try_send(struct test_dlc *td, uint16_t len, uint8_t seed);
void test_dlc_send_pattern(struct test_dlc *td, uint16_t len, uint8_t seed);
void test_dlc_expect_pattern(struct test_dlc *td, uint16_t len, uint8_t seed);
void test_dlc_wait_sent(struct test_dlc *td, uint32_t count);

#endif /* TEST_RFCOMM_COMMON_H_ */
