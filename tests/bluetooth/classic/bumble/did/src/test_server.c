/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"

LOG_MODULE_REGISTER(test_did_server, CONFIG_TEST_CLASSIC_DID_SIM_LOG_LEVEL);

/* Second, non-primary PnP Information record, laid out like the record of the DID module. */
static struct bt_sdp_attribute secondary_attrs[] = {
	BT_SDP_NEW_SERVICE,
	BT_SDP_LIST(
		BT_SDP_ATTR_SVCLASS_ID_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
			BT_SDP_ARRAY_16(BT_SDP_PNP_INFO_SVCLASS)
		},
		)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_SPECIFICATION_ID,
		BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
		BT_SDP_ARRAY_16(TEST_DID_SPEC_ID)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_VENDOR_ID,
		BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
		BT_SDP_ARRAY_16(TEST_DID_SECONDARY_VENDOR_ID)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PRODUCT_ID,
		BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
		BT_SDP_ARRAY_16(TEST_DID_SECONDARY_PRODUCT_ID)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_VERSION,
		BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
		BT_SDP_ARRAY_16(TEST_DID_SECONDARY_VERSION)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PRIMARY_RECORD,
		BT_SDP_TYPE_SIZE(BT_SDP_BOOL),
		BT_SDP_ARRAY_8(0)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_VENDOR_ID_SOURCE,
		BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
		BT_SDP_ARRAY_16(TEST_DID_SOURCE_USB_IF)
	),
};

static struct bt_sdp_record secondary_rec = BT_SDP_RECORD(secondary_attrs);
static bool secondary_registered;

/*
 * Register or unregister the secondary record. Tests call this before test_br_sync(): the client
 * starts SDP only after the sync and the previous ACL link is gone, so no SDP channel is open.
 */
static void secondary_set(bool registered)
{
	int err;

	if (registered == secondary_registered) {
		return;
	}

	if (registered) {
		err = bt_sdp_register_service(&secondary_rec);
		zassert_equal(err, 0, "Failed to register the secondary record (err %d)", err);
		zassert_not_equal(secondary_rec.handle, 0U, "Secondary record has no handle");
		LOG_DBG("Secondary record registered, handle 0x%08x", secondary_rec.handle);
	} else {
		err = bt_sdp_unregister_service(&secondary_rec);
		zassert_equal(err, 0, "Failed to unregister the secondary record (err %d)", err);
		LOG_DBG("Secondary record unregistered");
	}

	secondary_registered = registered;
}

ZTEST(did_server_sdp, test_001_ssa_single_record)
{
	secondary_set(false);
	test_br_sync();
}

ZTEST(did_server_sdp, test_002_ss_sa_single_record)
{
	secondary_set(false);
	test_br_sync();
}

ZTEST(did_server_sdp, test_003_ssa_two_records)
{
	int err;

	secondary_set(true);

	err = bt_sdp_register_service(&secondary_rec);
	zassert_equal(err, -EEXIST, "Second registration of a record returned %d", err);

	test_br_sync();
}

ZTEST(did_server_sdp, test_004_ss_sa_two_records)
{
	secondary_set(true);
	test_br_sync();
}

ZTEST(did_server_sdp, test_005_ssa_attr_subset)
{
	secondary_set(true);
	test_br_sync();
}

ZTEST(did_server_sdp, test_006_ssa_reserved_range)
{
	secondary_set(true);
	test_br_sync();
}

ZTEST(did_server_sdp, test_007_record_removed)
{
	int err;

	secondary_set(true);
	secondary_set(false);

	err = bt_sdp_unregister_service(&secondary_rec);
	zassert_equal(err, -ENOENT, "Unregistration of a removed record returned %d", err);

	err = bt_sdp_unregister_service(NULL);
	zassert_equal(err, -EINVAL, "Unregistration of NULL returned %d", err);

	test_br_sync();
}

ZTEST(did_server_sdp, test_008_discover_queue)
{
	secondary_set(true);
	test_br_sync();
}

ZTEST(did_server_sdp, test_009_discover_cancel)
{
	secondary_set(true);
	test_br_sync();
}

ZTEST(did_server_sdp, test_010_sa_reserved_range)
{
	secondary_set(true);
	test_br_sync();
}

static void *setup(void)
{
	/* With CONFIG_BT_DID, bt_enable() registers the record of the DID module. */
	test_br_setup(TEST_BR_ACCEPTOR, BT_SECURITY_L1);
	return NULL;
}

static void teardown(void *f)
{
	test_br_teardown();
}

ZTEST_SUITE(did_server_sdp, NULL, setup, NULL, NULL, teardown);
