/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_DID_COMMON_H_
#define TEST_DID_COMMON_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/classic/sdp.h>

/* SpecificationID of Device ID v1.3 (DID v1.3, section 5.1), set by the DID module. */
#define TEST_DID_SPEC_ID 0x0103

/* VendorIDSource values (DID v1.3, section 5.6). */
#define TEST_DID_SOURCE_BT_SIG 0x0001
#define TEST_DID_SOURCE_USB_IF 0x0002

/*
 * Values of the second, non-primary PnP Information record that only the server role registers.
 * Both roles run the same image, so both have the record of the DID module. Finding this record
 * proves that the client read the SDP database of the peer.
 */
#define TEST_DID_SECONDARY_VENDOR_ID  0x4d21
#define TEST_DID_SECONDARY_PRODUCT_ID 0x3c5a
#define TEST_DID_SECONDARY_VERSION    0x0102

/* Reserved attribute IDs (DID v1.3, section 5.7). */
#define TEST_DID_ATTR_RESERVED_FIRST 0x0206
#define TEST_DID_ATTR_RESERVED_LAST  0x02ff

/* Bits of test_did_record.found, one per attribute of the records of this test. */
#define TEST_DID_HAS_HANDLE     BIT(0)
#define TEST_DID_HAS_SVCLASS    BIT(1)
#define TEST_DID_HAS_SPEC_ID    BIT(2)
#define TEST_DID_HAS_VENDOR_ID  BIT(3)
#define TEST_DID_HAS_PRODUCT_ID BIT(4)
#define TEST_DID_HAS_VERSION    BIT(5)
#define TEST_DID_HAS_PRIMARY    BIT(6)
#define TEST_DID_HAS_SOURCE     BIT(7)
#define TEST_DID_HAS_STATE      BIT(8)
#define TEST_DID_HAS_BROWSE     BIT(9)
#define TEST_DID_HAS_LANG       BIT(10)

/* Attributes that BT_SDP_NEW_SERVICE adds next to the record handle. */
#define TEST_DID_HAS_NEW_SERVICE (TEST_DID_HAS_STATE | TEST_DID_HAS_BROWSE | TEST_DID_HAS_LANG)

/* The mandatory attributes of DID v1.3, table 5.1 (0x0200 to 0x0205). */
#define TEST_DID_HAS_DID                                                                           \
	(TEST_DID_HAS_SPEC_ID | TEST_DID_HAS_VENDOR_ID | TEST_DID_HAS_PRODUCT_ID |                 \
	 TEST_DID_HAS_VERSION | TEST_DID_HAS_PRIMARY | TEST_DID_HAS_SOURCE)

/* Every attribute of the Device ID records of this test. */
#define TEST_DID_HAS_ALL                                                                           \
	(TEST_DID_HAS_HANDLE | TEST_DID_HAS_NEW_SERVICE | TEST_DID_HAS_SVCLASS | TEST_DID_HAS_DID)

#define TEST_DID_MAX_RECORDS 4

/* Content of one service record, parsed in the SDP discovery callback. */
struct test_did_record {
	/* TEST_DID_HAS_* bits of the attributes in the record */
	uint32_t found;
	/* Number of attributes that are not one of TEST_DID_HAS_* */
	uint8_t unknown_count;
	/* First parsing error, 0 if none */
	int err;
	uint32_t handle;
	/* Number of elements and of PnP Information UUIDs in ServiceClassIDList */
	uint8_t svclass_count;
	uint8_t svclass_pnp_count;
	uint16_t spec_id;
	uint16_t vendor_id;
	uint16_t product_id;
	uint16_t version;
	uint16_t source;
	bool primary;
	/* Results of the SDP client helpers on the record */
	int vendor_id_err;
	uint16_t vendor_id_val;
	int product_id_err;
	uint16_t product_id_val;
	int profile_version_err;
	int features_err;
};

/* Expected values of a Device ID record. */
struct test_did_expect {
	uint16_t vendor_id;
	uint16_t product_id;
	uint16_t version;
	uint16_t source;
	bool primary;
};

/* One SDP discovery request and its results. */
struct test_did_discover {
	struct bt_sdp_discover_params params;
	struct k_sem done_sem;
	/*
	 * Return BT_SDP_DISCOVER_UUID_STOP after this many records of a Service Attribute or
	 * Service Search Attribute request, 0 to read all records
	 */
	uint8_t stop_after;
	/* Call bt_sdp_discover_cancel() for this request in its first callback */
	bool cancel_in_cb;
	int cancel_err;
	/* Number of callbacks, and of callbacks without result */
	uint8_t cb_count;
	uint8_t empty_count;
	/* Completion order of the requests, starting at 1 */
	atomic_val_t done_order;
	/* Record handles of Service Search responses */
	uint32_t handles[TEST_DID_MAX_RECORDS];
	uint8_t handle_count;
	/* Records of Service Attribute and Service Search Attribute responses */
	struct test_did_record records[TEST_DID_MAX_RECORDS];
	uint8_t record_count;
	/* First error found in the callback, 0 if none */
	int err;
};

extern const struct bt_uuid_16 test_did_pnp_uuid;

/* Prepare a Service Search or Service Search Attribute request for @p uuid. */
void test_did_discover_init(struct test_did_discover *d, uint8_t type, const struct bt_uuid *uuid,
			    struct bt_sdp_attribute_id_list *ids);

/* Prepare a Service Attribute request for the record @p handle. */
void test_did_discover_init_handle(struct test_did_discover *d, uint32_t handle,
				   struct bt_sdp_attribute_id_list *ids);

/* Queue the request on the ACL link of the test, return the bt_sdp_discover() result. */
int test_did_discover_start(struct test_did_discover *d);

/* Cancel the request if a failed test left it queued; for the suite after() hook. */
void test_did_discover_release(struct test_did_discover *d);

/* Wait until the request is complete. */
void test_did_discover_wait(struct test_did_discover *d);

/* Start the request and wait until it is complete. */
void test_did_discover_run(struct test_did_discover *d);

/* Check that the request returned @p count records and no empty result. */
void test_did_expect_records(const struct test_did_discover *d, uint8_t count);

/* Check that the request ended with one empty result only. */
void test_did_expect_empty(const struct test_did_discover *d);

/*
 * Check that @p rec holds exactly the attributes @p found with the values of @p exp.
 */
void test_did_check_record(const struct test_did_record *rec, const struct test_did_expect *exp,
			   uint32_t found);

#endif /* TEST_DID_COMMON_H_ */
