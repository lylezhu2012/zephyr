/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include "test_br.h"
#include "test_common.h"

LOG_MODULE_REGISTER(test_did_common, CONFIG_TEST_CLASSIC_DID_SIM_LOG_LEVEL);

/*
 * The SDP client holds one buffer while a request is resolved and may take a second one when a
 * response is continued. A buffer holds the responses of both Device ID records.
 */
#define SDP_POOL_COUNT    2
#define SDP_POOL_BUF_SIZE 512

NET_BUF_POOL_FIXED_DEFINE(did_sdp_pool, SDP_POOL_COUNT, SDP_POOL_BUF_SIZE,
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

const struct bt_uuid_16 test_did_pnp_uuid = BT_UUID_INIT_16(BT_SDP_PNP_INFO_SVCLASS);

static const struct {
	uint16_t id;
	uint32_t bit;
} known_attrs[] = {
	{BT_SDP_ATTR_RECORD_HANDLE, TEST_DID_HAS_HANDLE},
	{BT_SDP_ATTR_SVCLASS_ID_LIST, TEST_DID_HAS_SVCLASS},
	{BT_SDP_ATTR_RECORD_STATE, TEST_DID_HAS_STATE},
	{BT_SDP_ATTR_BROWSE_GRP_LIST, TEST_DID_HAS_BROWSE},
	{BT_SDP_ATTR_LANG_BASE_ATTR_ID_LIST, TEST_DID_HAS_LANG},
	{BT_SDP_ATTR_SPECIFICATION_ID, TEST_DID_HAS_SPEC_ID},
	{BT_SDP_ATTR_VENDOR_ID, TEST_DID_HAS_VENDOR_ID},
	{BT_SDP_ATTR_PRODUCT_ID, TEST_DID_HAS_PRODUCT_ID},
	{BT_SDP_ATTR_VERSION, TEST_DID_HAS_VERSION},
	{BT_SDP_ATTR_PRIMARY_RECORD, TEST_DID_HAS_PRIMARY},
	{BT_SDP_ATTR_VENDOR_ID_SOURCE, TEST_DID_HAS_SOURCE},
};

static atomic_t done_count;

static bool record_attr_cb(const struct bt_sdp_attribute *attr, void *user_data)
{
	struct test_did_record *rec = user_data;

	ARRAY_FOR_EACH(known_attrs, i) {
		if (known_attrs[i].id == attr->id) {
			rec->found |= known_attrs[i].bit;
			return true;
		}
	}

	rec->unknown_count++;
	return true;
}

static bool svclass_cb(const struct bt_sdp_attr_value_pair *value, void *user_data)
{
	struct test_did_record *rec = user_data;

	rec->svclass_count++;

	if ((value->uuid != NULL) && (value->value == NULL) &&
	    (bt_uuid_cmp(value->uuid, &test_did_pnp_uuid.uuid) == 0)) {
		rec->svclass_pnp_count++;
	}

	return true;
}

static int read_value(const struct net_buf *buf, uint16_t attr_id, struct bt_sdp_attr_value *value)
{
	struct bt_sdp_attribute attr;
	int err;

	err = bt_sdp_get_attr(buf, attr_id, &attr);
	if (err != 0) {
		LOG_ERR("Attribute 0x%04x not found (err %d)", attr_id, err);
		return err;
	}

	err = bt_sdp_attr_read(&attr, NULL, value);
	if (err != 0) {
		LOG_ERR("Failed to read attribute 0x%04x (err %d)", attr_id, err);
		return err;
	}

	return 0;
}

static int read_uint(const struct net_buf *buf, uint16_t attr_id, uint8_t size,
		     struct bt_sdp_attr_value *value)
{
	int err;

	err = read_value(buf, attr_id, value);
	if (err != 0) {
		return err;
	}

	if ((value->type != BT_SDP_ATTR_VALUE_TYPE_UINT) || (value->uint.size != size)) {
		LOG_ERR("Attribute 0x%04x: type %d size %u, expected uint%u", attr_id, value->type,
			value->uint.size, size * 8U);
		return -EINVAL;
	}

	return 0;
}

/* Read the uint16 attribute @p attr_id if the record has it. */
static int read_u16_if(const struct test_did_record *rec, const struct net_buf *buf, uint32_t bit,
		       uint16_t attr_id, uint16_t *out)
{
	struct bt_sdp_attr_value value;
	int err;

	if ((rec->found & bit) == 0U) {
		return 0;
	}

	err = read_uint(buf, attr_id, sizeof(uint16_t), &value);
	if (err == 0) {
		*out = value.uint.u16;
	}

	return err;
}

static int read_svclass(struct test_did_record *rec, const struct net_buf *buf)
{
	struct bt_sdp_attribute attr;
	int err;

	err = bt_sdp_get_attr(buf, BT_SDP_ATTR_SVCLASS_ID_LIST, &attr);
	if (err != 0) {
		return err;
	}

	return bt_sdp_attr_value_parse(&attr, svclass_cb, rec);
}

static int read_primary(struct test_did_record *rec, const struct net_buf *buf)
{
	struct bt_sdp_attr_value value;
	int err;

	err = read_value(buf, BT_SDP_ATTR_PRIMARY_RECORD, &value);
	if (err != 0) {
		return err;
	}

	if (value.type != BT_SDP_ATTR_VALUE_TYPE_BOOL) {
		LOG_ERR("PrimaryRecord: type %d, expected bool", value.type);
		return -EINVAL;
	}

	rec->primary = value.value;
	return 0;
}

/* Runs in the SDP callback: only collect values, the test thread checks them. */
static void parse_record(struct test_did_record *rec, const struct net_buf *buf)
{
	struct bt_sdp_attr_value value;
	uint16_t version;
	uint16_t features;
	int err;

	memset(rec, 0, sizeof(*rec));

	err = bt_sdp_record_parse(buf, record_attr_cb, rec);
	if (err != 0) {
		LOG_ERR("Failed to parse record (err %d)", err);
		rec->err = err;
		return;
	}

	if ((rec->found & TEST_DID_HAS_HANDLE) != 0U) {
		err = read_uint(buf, BT_SDP_ATTR_RECORD_HANDLE, sizeof(uint32_t), &value);
		if (err == 0) {
			rec->handle = value.uint.u32;
		}
	}

	if ((err == 0) && ((rec->found & TEST_DID_HAS_SVCLASS) != 0U)) {
		err = read_svclass(rec, buf);
	}

	if (err == 0) {
		err = read_u16_if(rec, buf, TEST_DID_HAS_SPEC_ID, BT_SDP_ATTR_SPECIFICATION_ID,
				  &rec->spec_id);
	}

	if (err == 0) {
		err = read_u16_if(rec, buf, TEST_DID_HAS_VENDOR_ID, BT_SDP_ATTR_VENDOR_ID,
				  &rec->vendor_id);
	}

	if (err == 0) {
		err = read_u16_if(rec, buf, TEST_DID_HAS_PRODUCT_ID, BT_SDP_ATTR_PRODUCT_ID,
				  &rec->product_id);
	}

	if (err == 0) {
		err = read_u16_if(rec, buf, TEST_DID_HAS_VERSION, BT_SDP_ATTR_VERSION,
				  &rec->version);
	}

	if (err == 0) {
		err = read_u16_if(rec, buf, TEST_DID_HAS_SOURCE, BT_SDP_ATTR_VENDOR_ID_SOURCE,
				  &rec->source);
	}

	if ((err == 0) && ((rec->found & TEST_DID_HAS_PRIMARY) != 0U)) {
		err = read_primary(rec, buf);
	}

	rec->err = err;

	rec->vendor_id_err = bt_sdp_get_vendor_id(buf, &rec->vendor_id_val);
	rec->product_id_err = bt_sdp_get_product_id(buf, &rec->product_id_val);
	rec->profile_version_err =
		bt_sdp_get_profile_version(buf, BT_SDP_PNP_INFO_SVCLASS, &version);
	rec->features_err = bt_sdp_get_features(buf, &features);

	LOG_DBG("Record: attrs 0x%03x handle 0x%08x spec 0x%04x vid 0x%04x pid 0x%04x "
		"ver 0x%04x primary %d src 0x%04x",
		rec->found, rec->handle, rec->spec_id, rec->vendor_id, rec->product_id,
		rec->version, rec->primary, rec->source);
}

static void discover_set_err(struct test_did_discover *d, int err)
{
	if (d->err == 0) {
		d->err = err;
	}
}

static void parse_handles(struct test_did_discover *d, const struct net_buf *buf)
{
	if ((buf->len % sizeof(uint32_t)) != 0U) {
		LOG_ERR("Invalid record handle list length %u", buf->len);
		discover_set_err(d, -EINVAL);
	}

	for (uint16_t off = 0U; (off + sizeof(uint32_t)) <= buf->len; off += sizeof(uint32_t)) {
		if (d->handle_count >= ARRAY_SIZE(d->handles)) {
			LOG_ERR("Too many record handles");
			discover_set_err(d, -ENOMEM);
			break;
		}

		d->handles[d->handle_count++] = sys_get_be32(&buf->data[off]);
	}
}

static void discover_done(struct test_did_discover *d)
{
	d->done_order = atomic_inc(&done_count) + 1;
	k_sem_give(&d->done_sem);
}

static uint8_t discover_cb(struct bt_conn *conn, struct bt_sdp_client_result *result,
			   const struct bt_sdp_discover_params *params)
{
	struct test_did_discover *d = CONTAINER_OF(params, struct test_did_discover, params);

	d->cb_count++;

	if (d->cancel_in_cb) {
		d->cancel_in_cb = false;
		d->cancel_err = bt_sdp_discover_cancel(conn, &d->params);
	}

	if ((result == NULL) || (result->resp_buf == NULL)) {
		LOG_DBG("No (more) result");
		d->empty_count++;
		discover_done(d);
		return BT_SDP_DISCOVER_UUID_STOP;
	}

	LOG_HEXDUMP_DBG(result->resp_buf->data, result->resp_buf->len, "SDP result:");

	if (params->type == BT_SDP_DISCOVER_SERVICE_SEARCH) {
		parse_handles(d, result->resp_buf);
	} else if (d->record_count < ARRAY_SIZE(d->records)) {
		parse_record(&d->records[d->record_count], result->resp_buf);
		d->record_count++;
	} else {
		LOG_ERR("Too many records");
		discover_set_err(d, -ENOMEM);
	}

	if (result->next_record_hint &&
	    ((d->stop_after == 0U) || (d->record_count < d->stop_after))) {
		return BT_SDP_DISCOVER_UUID_CONTINUE;
	}

	discover_done(d);
	return BT_SDP_DISCOVER_UUID_STOP;
}

void test_did_discover_init(struct test_did_discover *d, uint8_t type, const struct bt_uuid *uuid,
			    struct bt_sdp_attribute_id_list *ids)
{
	memset(d, 0, sizeof(*d));
	k_sem_init(&d->done_sem, 0, 1);

	d->params.uuid = uuid;
	d->params.func = discover_cb;
	d->params.pool = &did_sdp_pool;
	d->params.type = type;
	d->params.ids = ids;
}

void test_did_discover_init_handle(struct test_did_discover *d, uint32_t handle,
				   struct bt_sdp_attribute_id_list *ids)
{
	test_did_discover_init(d, BT_SDP_DISCOVER_SERVICE_ATTR, NULL, ids);
	d->params.handle = handle;
}

int test_did_discover_start(struct test_did_discover *d)
{
	return bt_sdp_discover(test_br_conn(), &d->params);
}

void test_did_discover_release(struct test_did_discover *d)
{
	struct bt_conn *conn = test_br_conn();
	int err;

	if (conn == NULL) {
		return;
	}

	/* A request left queued by a failed test must not be reused while it is queued. */
	err = bt_sdp_discover_cancel(conn, &d->params);
	if (err == 0) {
		LOG_WRN("Canceled a queued SDP request");
	} else if (err == -EINPROGRESS) {
		LOG_WRN("SDP request still in progress");
	}
}

void test_did_discover_wait(struct test_did_discover *d)
{
	int err;

	err = test_br_wait(&d->done_sem, TEST_BR_STEP_TIMEOUT);
	zassert_equal(err, 0, "SDP discovery not complete (err %d)", err);
	zassert_equal(d->err, 0, "SDP result parsing failed (err %d)", d->err);
}

void test_did_discover_run(struct test_did_discover *d)
{
	int err;

	err = test_did_discover_start(d);
	zassert_equal(err, 0, "Failed to start SDP discovery (err %d)", err);

	test_did_discover_wait(d);
}

void test_did_expect_records(const struct test_did_discover *d, uint8_t count)
{
	zassert_equal(d->empty_count, 0U, "SDP discovery returned an empty result");
	zassert_equal(d->record_count, count, "Got %u records, expected %u", d->record_count,
		      count);
}

void test_did_expect_empty(const struct test_did_discover *d)
{
	zassert_equal(d->cb_count, 1U, "Got %u callbacks, expected 1", d->cb_count);
	zassert_equal(d->empty_count, 1U, "Expected an empty result");
	zassert_equal(d->record_count, 0U, "Got %u records, expected none", d->record_count);
	zassert_equal(d->handle_count, 0U, "Got %u handles, expected none", d->handle_count);
}

void test_did_check_record(const struct test_did_record *rec, const struct test_did_expect *exp,
			   uint32_t found)
{
	zassert_equal(rec->err, 0, "Record parsing failed (err %d)", rec->err);
	zassert_equal(rec->found, found, "Record has attributes 0x%03x, expected 0x%03x",
		      rec->found, found);
	zassert_equal(rec->unknown_count, 0U, "Record has %u other attributes", rec->unknown_count);

	if ((found & TEST_DID_HAS_SVCLASS) != 0U) {
		zassert_equal(rec->svclass_count, 1U, "ServiceClassIDList has %u elements",
			      rec->svclass_count);
		zassert_equal(rec->svclass_pnp_count, 1U,
			      "ServiceClassIDList has no PnP Information UUID");
	}

	if ((found & TEST_DID_HAS_SPEC_ID) != 0U) {
		zassert_equal(rec->spec_id, TEST_DID_SPEC_ID, "SpecificationID 0x%04x",
			      rec->spec_id);
	}

	if ((found & TEST_DID_HAS_VENDOR_ID) != 0U) {
		zassert_equal(rec->vendor_id, exp->vendor_id, "VendorID 0x%04x, expected 0x%04x",
			      rec->vendor_id, exp->vendor_id);
		zassert_equal(rec->vendor_id_err, 0, "bt_sdp_get_vendor_id() failed (err %d)",
			      rec->vendor_id_err);
		zassert_equal(rec->vendor_id_val, exp->vendor_id,
			      "bt_sdp_get_vendor_id() 0x%04x, expected 0x%04x", rec->vendor_id_val,
			      exp->vendor_id);
	} else {
		zassert_equal(rec->vendor_id_err, -ENODATA,
			      "bt_sdp_get_vendor_id() without VendorID returned %d",
			      rec->vendor_id_err);
	}

	if ((found & TEST_DID_HAS_PRODUCT_ID) != 0U) {
		zassert_equal(rec->product_id, exp->product_id, "ProductID 0x%04x, expected 0x%04x",
			      rec->product_id, exp->product_id);
		zassert_equal(rec->product_id_err, 0, "bt_sdp_get_product_id() failed (err %d)",
			      rec->product_id_err);
		zassert_equal(rec->product_id_val, exp->product_id,
			      "bt_sdp_get_product_id() 0x%04x, expected 0x%04x",
			      rec->product_id_val, exp->product_id);
	} else {
		zassert_equal(rec->product_id_err, -ENODATA,
			      "bt_sdp_get_product_id() without ProductID returned %d",
			      rec->product_id_err);
	}

	if ((found & TEST_DID_HAS_VERSION) != 0U) {
		zassert_equal(rec->version, exp->version, "Version 0x%04x, expected 0x%04x",
			      rec->version, exp->version);
	}

	if ((found & TEST_DID_HAS_PRIMARY) != 0U) {
		zassert_equal(rec->primary, exp->primary, "PrimaryRecord %d, expected %d",
			      rec->primary, exp->primary);
	}

	if ((found & TEST_DID_HAS_SOURCE) != 0U) {
		zassert_equal(rec->source, exp->source, "VendorIDSource 0x%04x, expected 0x%04x",
			      rec->source, exp->source);
	}

	/* The records of this test have no ProfileDescriptorList and no SupportedFeatures. */
	zassert_equal(rec->profile_version_err, -ENODATA,
		      "bt_sdp_get_profile_version() returned %d", rec->profile_version_err);
	zassert_equal(rec->features_err, -ENODATA, "bt_sdp_get_features() returned %d",
		      rec->features_err);
}
