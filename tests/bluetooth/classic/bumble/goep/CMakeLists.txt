# SPDX-FileCopyrightText: Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bluetooth_classic_goep_sim)

target_sources(app PRIVATE
  src/test_main.c
  src/test_br.c
  src/test_connect.c
  src/test_put_get.c
  src/test_misc.c
  src/test_regressions.c
)

if(CONFIG_TEST_GOEP_L2CAP)
  zephyr_link_libraries(
    -Wl,--wrap=l2cap_br_data_pull
    -Wl,--wrap=bt_l2cap_br_recv
  )
endif()
