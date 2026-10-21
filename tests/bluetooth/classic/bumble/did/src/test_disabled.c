/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"

LOG_MODULE_REGISTER(test_did_disabled, CONFIG_TEST_CLASSIC_DID_SIM_LOG_LEVEL);

/*
 * Without CONFIG_BT_DID the peer has no PnP Information record. The server registers this Serial
 * Port record instead, so the client can show that the SDP server of the peer answers.
 */
static struct bt_sdp_attribute control_attrs[] = {
	BT_SDP_NEW_SERVICE,
	BT_SDP_LIST(
		BT_SDP_ATTR_SVCLASS_ID_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
			BT_SDP_ARRAY_16(BT_SDP_SERIAL_PORT_SVCLASS)
		},
		)
	),
};

static struct bt_sdp_record control_rec = BT_SDP_RECORD(control_attrs);

static const struct bt_uuid_16 control_uuid = BT_UUID_INIT_16(BT_SDP_SERIAL_PORT_SVCLASS);

static struct test_did_discover disc;

ZTEST(did_client_disabled, test_001_no_record)
{
	const struct test_did_record *rec;

	test_br_sync();

	test_did_discover_init(&disc, BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR, &control_uuid.uuid,
			       NULL);
	test_did_discover_run(&disc);
	test_did_expect_records(&disc, 1U);

	rec = &disc.records[0];
	zassert_equal(rec->err, 0, "Record parsing failed (err %d)", rec->err);
	zassert_equal(rec->found,
		      TEST_DID_HAS_HANDLE | TEST_DID_HAS_NEW_SERVICE | TEST_DID_HAS_SVCLASS,
		      "Control record has attributes 0x%03x", rec->found);
	zassert_equal(rec->svclass_pnp_count, 0U, "Control record has a PnP Information UUID");

	/* DID v1.3, appendix D 12.2 (informative): no Device ID record, no handle. */
	test_did_discover_init(&disc, BT_SDP_DISCOVER_SERVICE_SEARCH, &test_did_pnp_uuid.uuid,
			       NULL);
	test_did_discover_run(&disc);
	test_did_expect_empty(&disc);

	test_did_discover_init(&disc, BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR, &test_did_pnp_uuid.uuid,
			       NULL);
	test_did_discover_run(&disc);
	test_did_expect_empty(&disc);
}

ZTEST(did_server_disabled, test_001_no_record)
{
	test_br_sync();
}

static void *client_setup(void)
{
	test_br_setup(TEST_BR_INITIATOR, BT_SECURITY_L1);
	return NULL;
}

static void *server_setup(void)
{
	int err;

	test_br_setup(TEST_BR_ACCEPTOR, BT_SECURITY_L1);

	err = bt_sdp_register_service(&control_rec);
	zassert_equal(err, 0, "Failed to register the control record (err %d)", err);

	return NULL;
}

static void teardown(void *f)
{
	test_br_teardown();
}

static void client_after(void *f)
{
	test_did_discover_release(&disc);
}

ZTEST_SUITE(did_client_disabled, NULL, client_setup, NULL, client_after, teardown);
ZTEST_SUITE(did_server_disabled, NULL, server_setup, NULL, NULL, teardown);
