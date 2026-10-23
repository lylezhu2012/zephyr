/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * White-box helper of the fragmentation tests, as the modify_mtu command of the board-to-board
 * tests: it changes the transmit MTU of the AVDTP L2CAP channels, which is only possible with the
 * host internal headers. No other file of the test includes them.
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/sys/slist.h>
#include <zephyr/ztest.h>

#include <host/conn_internal.h>
#include <host/classic/l2cap_br_internal.h>

#include "test_br.h"
#include "test_common.h"

/* PSM of AVDTP, BT_L2CAP_PSM_AVDTP of the stack. */
#define TEST_PSM_AVDTP 0x0019

void test_avdtp_set_tx_mtu(uint16_t mtu)
{
	struct bt_conn *conn = test_br_conn();
	struct bt_l2cap_chan *chan;
	size_t count = 0;

	zassert_not_null(conn, "No ACL connection");

	SYS_SLIST_FOR_EACH_CONTAINER(&conn->channels, chan, node) {
		struct bt_l2cap_br_chan *br_chan = BR_CHAN(chan);

		if (br_chan->psm == TEST_PSM_AVDTP) {
			br_chan->tx.mtu = mtu;
			count++;
		}
	}

	zassert_true(count > 0, "No AVDTP channel");
}
