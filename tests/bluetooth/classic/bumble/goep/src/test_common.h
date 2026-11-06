/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_GOEP_COMMON_H_
#define TEST_GOEP_COMMON_H_

#include <zephyr/bluetooth/classic/goep.h>

enum test_goep_operation {
	TEST_GOEP_CONNECT,
	TEST_GOEP_PUT,
	TEST_GOEP_GET,
	TEST_GOEP_ACTION,
	TEST_GOEP_ABORT,
	TEST_GOEP_SETPATH,
};

extern struct bt_obex_client test_client;
extern struct bt_obex_server test_server;

void *test_goep_client_setup(void);
void *test_goep_server_setup(void);
void test_goep_teardown(void *fixture);
void test_goep_open(void);
void test_goep_close(bool server_initiated);
bool test_goep_is_client(void);
struct net_buf *test_goep_pdu(void);
void test_goep_send(int err, struct net_buf *buf);
struct net_buf *test_goep_expect(enum test_goep_operation operation, uint8_t status);

#endif
