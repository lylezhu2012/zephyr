/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/ztest.h>

#include "cmdline.h"
#include "nsi_main.h"
#include "soc.h"

bt_addr_t peer_addr;

static void peer_address_found(char *argv, int offset)
{
	int err = bt_addr_from_str(&argv[offset], &peer_addr);

	if (err != 0) {
		nsi_exit(err);
	}
}

static void test_goep_args(void)
{
	static struct args_struct_t args[] = {
		{
			.is_mandatory = true,
			.option = "peer_bd_address",
			.name = "XX:XX:XX:XX:XX:XX",
			.type = 's',
			.call_when_found = peer_address_found,
			.descript = "Bluetooth peer address for GOEP test",
		},
		ARG_TABLE_ENDMARKER
	};

	native_add_command_line_opts(args);
}

NATIVE_TASK(test_goep_args, PRE_BOOT_1, 20);
