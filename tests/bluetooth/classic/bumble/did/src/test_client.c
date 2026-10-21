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

LOG_MODULE_REGISTER(test_did_client, CONFIG_TEST_CLASSIC_DID_SIM_LOG_LEVEL);

/* Record of the DID module on the peer; both roles are built from the same image. */
static const struct test_did_expect expect_primary = {
	.vendor_id = CONFIG_BT_DEVICE_VENDOR_ID,
	.product_id = CONFIG_BT_DEVICE_PRODUCT_ID,
	.version = CONFIG_BT_DEVICE_VERSION,
	.source = TEST_DID_SOURCE_BT_SIG,
	.primary = true,
};

/* Record registered by the server role only (src/test_server.c). */
static const struct test_did_expect expect_secondary = {
	.vendor_id = TEST_DID_SECONDARY_VENDOR_ID,
	.product_id = TEST_DID_SECONDARY_PRODUCT_ID,
	.version = TEST_DID_SECONDARY_VERSION,
	.source = TEST_DID_SOURCE_USB_IF,
	.primary = false,
};

/*
 * Attribute ID lists. The SDP client reads them again for continuation requests, so they must
 * outlive a test that fails while a request is still in progress.
 */
static struct bt_sdp_attribute_id_range handle_did_ranges[] = {
	BT_SDP_ATTR_ID_RANGE(BT_SDP_ATTR_RECORD_HANDLE, BT_SDP_ATTR_RECORD_HANDLE),
	BT_SDP_ATTR_ID_RANGE(BT_SDP_ATTR_SPECIFICATION_ID, BT_SDP_ATTR_VENDOR_ID_SOURCE),
};

static struct bt_sdp_attribute_id_list handle_did_ids = {
	.count = ARRAY_SIZE(handle_did_ranges),
	.ranges = handle_did_ranges,
};

/* A single attribute ID and a range; SpecificationID and ProductID are left out. */
static struct bt_sdp_attribute_id_range subset_ranges[] = {
	BT_SDP_ATTR_ID_RANGE(BT_SDP_ATTR_VENDOR_ID, BT_SDP_ATTR_VENDOR_ID),
	BT_SDP_ATTR_ID_RANGE(BT_SDP_ATTR_VERSION, BT_SDP_ATTR_VENDOR_ID_SOURCE),
};

static struct bt_sdp_attribute_id_list subset_ids = {
	.count = ARRAY_SIZE(subset_ranges),
	.ranges = subset_ranges,
};

/* VendorIDSource and the reserved attribute IDs after it (DID v1.3, section 5.7). */
static struct bt_sdp_attribute_id_range source_reserved_ranges[] = {
	BT_SDP_ATTR_ID_RANGE(BT_SDP_ATTR_VENDOR_ID_SOURCE, TEST_DID_ATTR_RESERVED_LAST),
};

static struct bt_sdp_attribute_id_list source_reserved_ids = {
	.count = ARRAY_SIZE(source_reserved_ranges),
	.ranges = source_reserved_ranges,
};

/* The reserved attribute IDs only. */
static struct bt_sdp_attribute_id_range reserved_ranges[] = {
	BT_SDP_ATTR_ID_RANGE(TEST_DID_ATTR_RESERVED_FIRST, TEST_DID_ATTR_RESERVED_LAST),
};

static struct bt_sdp_attribute_id_list reserved_ids = {
	.count = ARRAY_SIZE(reserved_ranges),
	.ranges = reserved_ranges,
};

static struct test_did_discover disc_a;
static struct test_did_discover disc_b;
static struct test_did_discover disc_c;

/*
 * Check that @p recs hold the record of the DID module and the secondary record of the server,
 * in any order. The server has one primary and one non-primary record; DID v1.3, section 5.5:
 * "the PrimaryRecord attribute is set to TRUE for that record and FALSE for all other records".
 */
static void check_two_records(const struct test_did_record *recs, uint8_t count, uint32_t found)
{
	const struct test_did_record *primary = NULL;
	const struct test_did_record *secondary = NULL;

	zassert_true((found & TEST_DID_HAS_PRIMARY) != 0U, "PrimaryRecord is needed to match");
	zassert_equal(count, 2U, "Got %u records, expected 2", count);

	for (uint8_t i = 0U; i < count; i++) {
		if (recs[i].primary) {
			zassert_is_null(primary, "More than one record with PrimaryRecord TRUE");
			primary = &recs[i];
		} else {
			zassert_is_null(secondary, "More than one record with PrimaryRecord FALSE");
			secondary = &recs[i];
		}
	}

	zassert_not_null(primary, "No record with PrimaryRecord TRUE");
	zassert_not_null(secondary, "No record with PrimaryRecord FALSE");

	test_did_check_record(primary, &expect_primary, found);
	test_did_check_record(secondary, &expect_secondary, found);

	if ((found & TEST_DID_HAS_HANDLE) != 0U) {
		zassert_not_equal(primary->handle, secondary->handle,
				  "Both records have handle 0x%08x", primary->handle);
	}
}

/* Service Search for the PnP Information UUID; the server has @p count records. */
static void search_handles(struct test_did_discover *d, uint8_t count)
{
	test_did_discover_init(d, BT_SDP_DISCOVER_SERVICE_SEARCH, &test_did_pnp_uuid.uuid, NULL);
	test_did_discover_run(d);

	zassert_equal(d->empty_count, 0U, "Service Search returned no handle");
	zassert_equal(d->handle_count, count, "Got %u handles, expected %u", d->handle_count,
		      count);

	for (uint8_t i = 1U; i < d->handle_count; i++) {
		zassert_not_equal(d->handles[i - 1U], d->handles[i], "Handle 0x%08x returned twice",
				  d->handles[i]);
	}
}

ZTEST(did_client_sdp, test_001_ssa_single_record)
{
	test_br_sync();

	/* Default attribute ID range 0x0000-0xffff */
	test_did_discover_init(&disc_a, BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR,
			       &test_did_pnp_uuid.uuid, NULL);
	test_did_discover_run(&disc_a);

	/* A single Device ID record has PrimaryRecord TRUE (DID v1.3, section 5.5). */
	test_did_expect_records(&disc_a, 1U);
	test_did_check_record(&disc_a.records[0], &expect_primary, TEST_DID_HAS_ALL);
}

ZTEST(did_client_sdp, test_002_ss_sa_single_record)
{
	const struct test_did_record *rec;

	test_br_sync();

	search_handles(&disc_a, 1U);

	test_did_discover_init_handle(&disc_b, disc_a.handles[0], NULL);
	test_did_discover_run(&disc_b);
	test_did_expect_records(&disc_b, 1U);

	rec = &disc_b.records[0];
	test_did_check_record(rec, &expect_primary, TEST_DID_HAS_ALL);
	zassert_equal(rec->handle, disc_a.handles[0], "Record handle 0x%08x, requested 0x%08x",
		      rec->handle, disc_a.handles[0]);
}

ZTEST(did_client_sdp, test_003_ssa_two_records)
{
	test_br_sync();

	test_did_discover_init(&disc_a, BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR,
			       &test_did_pnp_uuid.uuid, NULL);
	test_did_discover_run(&disc_a);

	test_did_expect_records(&disc_a, 2U);
	check_two_records(disc_a.records, disc_a.record_count, TEST_DID_HAS_ALL);
}

ZTEST(did_client_sdp, test_004_ss_sa_two_records)
{
	struct test_did_record recs[2];

	test_br_sync();

	search_handles(&disc_a, ARRAY_SIZE(recs));

	for (size_t i = 0U; i < ARRAY_SIZE(recs); i++) {
		test_did_discover_init_handle(&disc_b, disc_a.handles[i], &handle_did_ids);
		test_did_discover_run(&disc_b);
		test_did_expect_records(&disc_b, 1U);
		zassert_equal(disc_b.records[0].handle, disc_a.handles[i],
			      "Record handle 0x%08x, requested 0x%08x", disc_b.records[0].handle,
			      disc_a.handles[i]);
		recs[i] = disc_b.records[0];
	}

	/* ServiceClassIDList (0x0001) is not in the requested ranges. */
	check_two_records(recs, ARRAY_SIZE(recs), TEST_DID_HAS_HANDLE | TEST_DID_HAS_DID);
}

ZTEST(did_client_sdp, test_005_ssa_attr_subset)
{
	test_br_sync();

	test_did_discover_init(&disc_a, BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR,
			       &test_did_pnp_uuid.uuid, &subset_ids);
	test_did_discover_run(&disc_a);

	test_did_expect_records(&disc_a, 2U);
	check_two_records(disc_a.records, disc_a.record_count,
			  TEST_DID_HAS_VENDOR_ID | TEST_DID_HAS_VERSION | TEST_DID_HAS_PRIMARY |
				  TEST_DID_HAS_SOURCE);
}

ZTEST(did_client_sdp, test_006_ssa_reserved_range)
{
	test_br_sync();

	/* A range that ends in the reserved IDs returns VendorIDSource of both records only. */
	test_did_discover_init(&disc_a, BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR,
			       &test_did_pnp_uuid.uuid, &source_reserved_ids);
	test_did_discover_run(&disc_a);
	test_did_expect_records(&disc_a, 2U);

	for (uint8_t i = 0U; i < disc_a.record_count; i++) {
		const struct test_did_record *rec = &disc_a.records[i];

		test_did_check_record(rec,
				      (rec->source == TEST_DID_SOURCE_BT_SIG) ? &expect_primary
									      : &expect_secondary,
				      TEST_DID_HAS_SOURCE);
	}

	zassert_not_equal(disc_a.records[0].source, disc_a.records[1].source,
			  "Both records have VendorIDSource 0x%04x", disc_a.records[0].source);

	/*
	 * Neither record has an attribute in the reserved range, and the Zephyr SDP server leaves
	 * out a record without matching attributes, so the client reports an empty result.
	 */
	test_did_discover_init(&disc_b, BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR,
			       &test_did_pnp_uuid.uuid, &reserved_ids);
	test_did_discover_run(&disc_b);
	test_did_expect_empty(&disc_b);
}

ZTEST(did_client_sdp, test_007_record_removed)
{
	test_br_sync();

	/* The server removed the secondary record: only the DID module record remains. */
	test_did_discover_init(&disc_a, BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR,
			       &test_did_pnp_uuid.uuid, NULL);
	test_did_discover_run(&disc_a);

	test_did_expect_records(&disc_a, 1U);
	test_did_check_record(&disc_a.records[0], &expect_primary, TEST_DID_HAS_ALL);
}

ZTEST(did_client_sdp, test_008_discover_queue)
{
	int err;

	test_br_sync();

	test_did_discover_init(&disc_a, BT_SDP_DISCOVER_SERVICE_SEARCH, &test_did_pnp_uuid.uuid,
			       NULL);
	test_did_discover_init(&disc_b, BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR,
			       &test_did_pnp_uuid.uuid, NULL);
	test_did_discover_init(&disc_c, BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR,
			       &test_did_pnp_uuid.uuid, NULL);

	/* B stops after the first of the two records. */
	disc_b.stop_after = 1U;

	/* Queue all requests before the first one is resolved. */
	err = test_did_discover_start(&disc_a);
	zassert_equal(err, 0, "Failed to queue request A (err %d)", err);
	err = test_did_discover_start(&disc_b);
	zassert_equal(err, 0, "Failed to queue request B (err %d)", err);
	err = test_did_discover_start(&disc_c);
	zassert_equal(err, 0, "Failed to queue request C (err %d)", err);

	test_did_discover_wait(&disc_a);
	test_did_discover_wait(&disc_b);
	test_did_discover_wait(&disc_c);

	zassert_true(disc_a.done_order < disc_b.done_order, "Request B completed before A");
	zassert_true(disc_b.done_order < disc_c.done_order, "Request C completed before B");

	zassert_equal(disc_a.empty_count, 0U, "Service Search returned no handle");
	zassert_equal(disc_a.handle_count, 2U, "Got %u handles, expected 2", disc_a.handle_count);

	/* No further callback for B after it returned BT_SDP_DISCOVER_UUID_STOP. */
	zassert_equal(disc_b.cb_count, 1U, "Request B got %u callbacks, expected 1",
		      disc_b.cb_count);
	test_did_expect_records(&disc_b, 1U);

	test_did_expect_records(&disc_c, 2U);
	check_two_records(disc_c.records, disc_c.record_count, TEST_DID_HAS_ALL);
}

ZTEST(did_client_sdp, test_009_discover_cancel)
{
	int err;

	test_br_sync();

	test_did_discover_init(&disc_a, BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR,
			       &test_did_pnp_uuid.uuid, NULL);
	test_did_discover_init(&disc_c, BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR,
			       &test_did_pnp_uuid.uuid, NULL);

	/* A tries to cancel itself while it is being resolved. */
	disc_a.cancel_in_cb = true;

	err = bt_sdp_discover_cancel(NULL, &disc_c.params);
	zassert_equal(err, -EINVAL, "Cancel without connection returned %d", err);
	err = bt_sdp_discover_cancel(test_br_conn(), NULL);
	zassert_equal(err, -EINVAL, "Cancel without parameters returned %d", err);
	err = bt_sdp_discover_cancel(test_br_conn(), &disc_c.params);
	zassert_equal(err, -ESRCH, "Cancel of a request that is not queued returned %d", err);

	err = test_did_discover_start(&disc_a);
	zassert_equal(err, 0, "Failed to queue request A (err %d)", err);
	err = test_did_discover_start(&disc_c);
	zassert_equal(err, 0, "Failed to queue request C (err %d)", err);

	/*
	 * The requests wait for the SDP channel to the peer, and C waits for A as well. The test
	 * thread is cooperative and does not block until the cancel below, so C is still queued.
	 */
	err = bt_sdp_discover_cancel(test_br_conn(), &disc_c.params);
	zassert_equal(err, 0, "Failed to cancel queued request C (err %d)", err);
	err = bt_sdp_discover_cancel(test_br_conn(), &disc_c.params);
	zassert_equal(err, -ESRCH, "Second cancel of request C returned %d", err);

	/* A canceled request may be reused immediately. */
	err = test_did_discover_start(&disc_c);
	zassert_equal(err, 0, "Failed to queue request C again (err %d)", err);

	test_did_discover_wait(&disc_a);
	test_did_discover_wait(&disc_c);

	zassert_equal(disc_a.cancel_err, -EINPROGRESS, "Cancel in the callback returned %d",
		      disc_a.cancel_err);
	test_did_expect_records(&disc_a, 2U);
	check_two_records(disc_a.records, disc_a.record_count, TEST_DID_HAS_ALL);

	/* One callback per record of the second submission, none for the canceled one. */
	zassert_true(disc_a.done_order < disc_c.done_order, "Request C completed before A");
	zassert_equal(disc_c.cb_count, 2U, "Request C got %u callbacks, expected 2",
		      disc_c.cb_count);
	test_did_expect_records(&disc_c, 2U);
	check_two_records(disc_c.records, disc_c.record_count, TEST_DID_HAS_ALL);
}

ZTEST(did_client_sdp, test_010_sa_reserved_range)
{
	test_br_sync();

	search_handles(&disc_a, 2U);

	/*
	 * The Zephyr SDP server answers a Service Attribute request without matching attributes
	 * with an empty attribute list, which the client reports as an empty result.
	 */
	for (uint8_t i = 0U; i < disc_a.handle_count; i++) {
		test_did_discover_init_handle(&disc_b, disc_a.handles[i], &reserved_ids);
		test_did_discover_run(&disc_b);
		test_did_expect_empty(&disc_b);
	}
}

static void *setup(void)
{
	test_br_setup(TEST_BR_INITIATOR, BT_SECURITY_L1);
	return NULL;
}

static void teardown(void *f)
{
	test_br_teardown();
}

static void after(void *f)
{
	test_did_discover_release(&disc_a);
	test_did_discover_release(&disc_b);
	test_did_discover_release(&disc_c);
}

ZTEST_SUITE(did_client_sdp, NULL, setup, NULL, after, teardown);
