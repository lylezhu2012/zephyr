/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_HFP_AG_H_
#define TEST_HFP_AG_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/bluetooth/classic/hfp_ag.h>

#include "test_common.h"

/* SLC of the current test, valid after the AG connected callback. */
extern struct bt_hfp_ag *ag;

/* SCO connection of the current SLC, NULL if there is none. */
extern struct bt_conn *ag_sco;

/* Indicator values returned by the get_indicator_value callback. */
struct ag_indicators {
	uint8_t service;
	uint8_t signal;
	uint8_t roam;
	uint8_t battery;
};

/* Behavior of the AG callbacks, reset before each test. */
struct ag_config {
	struct ag_indicators ind;
	/* Notify the get_ongoing_call callback and wait for bt_hfp_ag_ongoing_calls(). */
	bool ongoing_calls;
	/* Report TEST_AG_SUBSCRIBER_NUMBER from the subscriber_number callback. */
	bool subscriber;
	/* Number returned by the redial callback, empty to fail the request. */
	char last_number[CONFIG_BT_HFP_AG_PHONE_NUMBER_MAX_LEN + 1];
};

extern struct ag_config ag_cfg;

/* Phone number accepted by the number_call callback; others are refused with -ENOTSUP. */
#define TEST_AG_DIAL_NUMBER "123456789"

/* Memory location accepted by the memory_dial callback and the number it maps to. */
#define TEST_AG_MEMORY_LOCATION "0"

#define TEST_AG_SUBSCRIBER_NUMBER  "87654321"
#define TEST_AG_SUBSCRIBER_TYPE    128
#define TEST_AG_SUBSCRIBER_SERVICE 4

/* Number returned by every second request_phone_number callback; the others fail. */
#define TEST_AG_VOICE_TAG_NUMBER "123456789"

void ag_suite_setup(void);
void ag_suite_teardown(void);
void ag_test_before(void);
void ag_test_after(void);

/* Start the SLC from the AG and wait until it is established. */
void ag_slc_connect(void);
/* Wait for the SLC started by the HF. */
void ag_slc_wait(void);
/* Release the SLC from the AG and wait until it is released. */
void ag_slc_disconnect(void);
/* Wait until the SLC is released by the HF. */
void ag_slc_wait_disconnected(void);

/* Wait for a call event and return its call object. */
struct bt_hfp_ag_call *ag_wait_call(enum test_evt type);

/* Wait for a call event of @p call. */
void ag_expect_call(enum test_evt type, struct bt_hfp_ag_call *call);

/* Notify an incoming call and wait until it rings. */
struct bt_hfp_ag_call *ag_incoming(const char *number);

void ag_wait_sco_connected(uint8_t air_mode);
void ag_wait_sco_disconnected(void);
void ag_sco_disconnect(void);
/* Wait until no SCO connection is left, whatever events were reported before. */
void ag_wait_no_sco(void);

#endif /* TEST_HFP_AG_H_ */
