/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_BR_H_
#define TEST_BR_H_

#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

/*
 * ACL lifecycle and test synchronization shared by the two simulated devices.
 *
 * Both devices run a suite with the same test names, ztest runs them in name order. Before each
 * test the initiator (device 0) creates the ACL connection and the acceptor waits for it. Each
 * test then calls test_br_sync() once this side is ready (e.g. servers registered): both sides
 * exchange the test name in an L2CAP ECHO request/response, the initiator only returns when the
 * acceptor is in the same test. After each test the ACL connection is released, so every test
 * starts from a fresh ACL link.
 *
 * Test names must start with a unique, zero-padded index (test_NNN_...), the first
 * TEST_BR_SYNC_LEN characters are exchanged and compared in name order.
 */

enum test_br_role {
	TEST_BR_INITIATOR,
	TEST_BR_ACCEPTOR,
};

#define TEST_BR_SYNC_LEN 40

/* Default wait for a protocol step of the peer. */
#define TEST_BR_STEP_TIMEOUT K_SECONDS(10)

extern bt_addr_t peer_addr;

/* Enable Bluetooth and set the role of this device; the initiator waits until the peer is found. */
void test_br_setup(enum test_br_role role, bt_security_t sec_level);
void test_br_teardown(void);

/* Security level the ACL link is raised to before each test (BT_SECURITY_L1 for none). */
void test_br_set_security(bt_security_t sec_level);

enum test_br_role test_br_role(void);
struct bt_conn *test_br_conn(void);
bool test_br_connected(void);

/* Wait until the peer reached the same test; call once per test when this side is ready. */
void test_br_sync(void);

/* Take @p sem; return -ENOTCONN as soon as the ACL link is lost, -EAGAIN on timeout. */
int test_br_wait(struct k_sem *sem, k_timeout_t timeout);

/* ACL operations inside a test body; failures are reported with zassert. */
void test_br_connect(void);
void test_br_disconnect(void);
void test_br_wait_connected(k_timeout_t timeout);
void test_br_wait_disconnected(k_timeout_t timeout);
void test_br_security(bt_security_t sec_level);

#endif /* TEST_BR_H_ */
