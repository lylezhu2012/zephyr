/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_HFP_HF_H_
#define TEST_HFP_HF_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/bluetooth/classic/hfp_hf.h>

#include "test_common.h"

/* SLC of the current test, valid after the HF connected callback. */
extern struct bt_hfp_hf *hf;

/* SCO connection of the current SLC, NULL if there is none. */
extern struct bt_conn *hf_sco;

/* bt_hfp_hf_current_call fields packed in test_event.value2 of EVT_QUERY_CALL. */
#define TEST_QUERY_CALL_DIR(v)        ((v) & 0xff)
#define TEST_QUERY_CALL_STATUS(v)     (((v) >> 8) & 0xff)
#define TEST_QUERY_CALL_MODE(v)       (((v) >> 16) & 0xff)
#define TEST_QUERY_CALL_MULTIPARTY(v) (((v) >> 24) & 0xff)

void hf_suite_setup(void);
void hf_suite_teardown(void);
void hf_test_before(void);
void hf_test_after(void);

/*
 * Select who answers the codec negotiation (+BCS) of the AG: the stack accepts the proposed
 * codec itself (false), or the test is notified with EVT_CODEC_NEGOTIATE and calls
 * bt_hfp_hf_select_codec() or bt_hfp_hf_set_codecs() (true).
 */
void hf_codec_negotiation_by_app(bool app);

/* Start the SLC from the HF and wait until it is established. */
void hf_slc_connect(void);
/* Wait for the SLC started by the AG. */
void hf_slc_wait(void);
/* Release the SLC from the HF and wait until it is released. */
void hf_slc_disconnect(void);
/* Wait until the SLC is released by the AG. */
void hf_slc_wait_disconnected(void);

/* Wait for a call event and return its call object. */
struct bt_hfp_hf_call *hf_wait_call(enum test_evt type);

/* Wait for an indicator, volume or result event with @p value. */
void hf_expect_value(enum test_evt type, int32_t value);

void hf_wait_sco_connected(uint8_t air_mode);
void hf_wait_sco_disconnected(void);
void hf_sco_disconnect(void);
/* Wait until no SCO connection is left, whatever events were reported before. */
void hf_wait_no_sco(void);

/* Wait for an incoming call and, if @p number is not NULL, for its CLIP notification. */
struct bt_hfp_hf_call *hf_wait_incoming(const char *number);

/*
 * Counterpart of ag_ringing_call(): wait for the incoming call and its CLIP notification, then
 * synchronize with the AG, which acts on the ringing call.
 */
struct bt_hfp_hf_call *hf_wait_ringing_call(const char *number);

/* Wait for a call event of @p call. */
void hf_expect_call(enum test_evt type, struct bt_hfp_hf_call *call);

#endif /* TEST_HFP_HF_H_ */
