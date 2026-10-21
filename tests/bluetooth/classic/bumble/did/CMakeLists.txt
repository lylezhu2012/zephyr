# SPDX-FileCopyrightText: Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
set(NO_QEMU_SERIAL_BT_SERVER 1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bluetooth)

target_sources(app PRIVATE
  src/test_br.c
  src/test_common.c
  src/test_main.c
)

# The disabled scenario is built without the DID module and has its own suites.
target_sources_ifdef(CONFIG_BT_DID app PRIVATE
  src/test_client.c
  src/test_server.c
)
target_sources_ifndef(CONFIG_BT_DID app PRIVATE
  src/test_disabled.c
)
