.. _bluetooth_classic_did_sim_test:

Bluetooth Classic Device ID (DID) Test
######################################

Overview
********

This test suite verifies the Device ID (DID) record of the Zephyr Bluetooth Classic (BR/EDR)
host and the SDP client API used to read it (:file:`include/zephyr/bluetooth/classic/sdp.h`),
between two Zephyr devices using the Bumble Bluetooth controller simulator sidecar.

With :kconfig:option:`CONFIG_BT_DID` enabled, :c:func:`bt_enable` registers a PnP Information
record (:file:`subsys/bluetooth/host/classic/did.c`). VendorID, ProductID and Version come from
:kconfig:option:`CONFIG_BT_DEVICE_VENDOR_ID`, :kconfig:option:`CONFIG_BT_DEVICE_PRODUCT_ID` and
:kconfig:option:`CONFIG_BT_DEVICE_VERSION`. SpecificationID (``0x0103``), PrimaryRecord
(``TRUE``) and VendorIDSource (``0x0001``, Bluetooth SIG) are fixed in ``did.c``.

The test is executed on the Zephyr ``native_sim`` platform. Two simulated controllers are started
by Twister's Bumble sidecar and connected to two instances of the same test binary via
``--bt-dev=127.0.0.1:<tcp_port>``. Each instance receives a ``--peer_bd_address`` argument so the
two roles can locate each other.

* **Client role** (``did_client_<area>`` ztest suites, device 0, ``00:00:01:00:00:01``): creates
  the ACL connection and reads the PnP Information records of the server with
  :c:func:`bt_sdp_discover`.
* **Server role** (``did_server_<area>`` ztest suites, device 1, ``00:00:01:00:00:02``): exposes
  the record of the DID module and, depending on the test, a second, non-primary PnP Information
  record registered with :c:func:`bt_sdp_register_service`.

Test Structure
**************

Both suites of a scenario contain the same test names and ztest runs them in name order, so the
two devices execute the same test at the same time. :file:`src/test_br.c` creates a new ACL
connection before every test and releases it after the test. Each test calls ``test_br_sync()``
when its side is ready; the client sends the test name in an L2CAP ECHO request and the server
answers with its own.

The server registers or unregisters its second record before ``test_br_sync()``. At that point
no SDP channel is open: the previous ACL link is gone and the client starts SDP only after the
sync, so :c:func:`bt_sdp_unregister_service` does not return ``-EBUSY``.

Implementation Notes
********************

* Both roles run the same image, so both have the record of the DID module. The second record
  exists on the server only (VendorIDSource ``0x0002``, PrimaryRecord ``FALSE``); finding it
  proves that the client read the SDP database of the peer.
* The record values are set with ``configdefault`` in :file:`Kconfig`, so that the disabled
  scenario can turn :kconfig:option:`CONFIG_BT_DID` off without assigning values to options that
  depend on it. VendorID ``0x0025`` is the Bluetooth SIG Company Identifier of NXP B.V., which
  matches the VendorIDSource ``0x0001`` set by ``did.c``.
* Each attribute is read with :c:func:`bt_sdp_get_attr` and :c:func:`bt_sdp_attr_read`. VendorID
  and ProductID are also read with :c:func:`bt_sdp_get_vendor_id` and
  :c:func:`bt_sdp_get_product_id`. :c:func:`bt_sdp_get_profile_version` and
  :c:func:`bt_sdp_get_features` must return ``-ENODATA``, since the records have neither
  attribute. The attribute set of a record is checked exactly, including the attributes that
  ``BT_SDP_NEW_SERVICE`` adds (ServiceRecordState, BrowseGroupList and
  LanguageBaseAttributeIDList).
* The SDP callback only records values and return codes; all assertions run in the test thread.
  The attribute ID lists are static, since the SDP client reads them again for continuation
  requests.

Test Cases
**********

``bluetooth.classic.sim.did.sdp`` (``did_client_sdp`` / ``did_server_sdp``):

``test_001_ssa_single_record``
  The server has the record of the DID module only. A Service Search Attribute request with the
  default range returns one record with all attributes and the configured values, PrimaryRecord
  ``TRUE`` and a ServiceClassIDList that holds the PnP Information UUID only.

``test_002_ss_sa_single_record``
  A Service Search request returns one record handle; a Service Attribute request for that
  handle returns the same record, with the requested handle in attribute ``0x0000``.

``test_003_ssa_two_records``
  The server also registers the second record (a second registration returns ``-EEXIST``). A
  Service Search Attribute request returns both records, with different handles, one with
  PrimaryRecord ``TRUE`` and one with ``FALSE``, each with its own values.

``test_004_ss_sa_two_records``
  A Service Search request returns two different handles. One Service Attribute request per
  handle for the record handle and the range ``0x0200`` to ``0x0205`` returns that record, without
  ServiceClassIDList.

``test_005_ssa_attr_subset``
  A Service Search Attribute request with the attribute ID list VendorID and the range Version to
  VendorIDSource returns only these four attributes of both records;
  :c:func:`bt_sdp_get_product_id` returns ``-ENODATA``.

``test_006_ssa_reserved_range``
  The range VendorIDSource to ``0x02ff`` returns only VendorIDSource of both records. The
  reserved range ``0x0206`` to ``0x02ff`` alone returns one empty result, since the Zephyr SDP
  server leaves out records without a matching attribute.

``test_007_record_removed``
  The server unregisters the second record (a second unregistration returns ``-ENOENT``, a
  ``NULL`` record ``-EINVAL``). The client finds the record of the DID module only, with
  PrimaryRecord ``TRUE``.

``test_008_discover_queue``
  Three requests (Service Search, and two Service Search Attribute requests) are queued before
  the first one is resolved. They complete in order. The second one returns
  ``BT_SDP_DISCOVER_UUID_STOP`` after the first record and gets no further callback; the third
  one gets both records.

``test_009_discover_cancel``
  :c:func:`bt_sdp_discover_cancel` returns ``-EINVAL`` for ``NULL`` arguments and ``-ESRCH`` for
  a request that is not queued. A request queued behind another one is canceled (``0``, then
  ``-ESRCH``), queued again and gets one callback per record. A request that cancels itself in
  its callback gets ``-EINPROGRESS`` and completes normally.

``test_010_sa_reserved_range``
  A Service Attribute request for the reserved range ``0x0206`` to ``0x02ff`` of each record:
  the server answers with an empty attribute list and the client reports one empty result.

``bluetooth.classic.sim.did.disabled`` (``did_client_disabled`` / ``did_server_disabled``), built
with :kconfig:option:`CONFIG_BT_DID` disabled:

``test_001_no_record``
  The server registers a Serial Port record instead. The client finds that record, which shows
  that the SDP server of the peer answers, and then gets one empty result for both a Service
  Search and a Service Search Attribute request for the PnP Information UUID.

Specification Mapping
*********************

There is no board-to-board DID test suite. The tests refer to the Device ID Profile
Specification v1.3 (DID) and to the SDP chapter of the Core Specification 6.0, Vol 3, Part B.

=============================== ============================================================
Test                            Reference
=============================== ============================================================
``sdp::test_001``               DID 5 (table 5.1), 5.1, 5.5 (single record), 5.6, 8.1
``sdp::test_002``               DID appendix D 12.1 and 12.3; Core 4.5, 4.6
``sdp::test_003``               DID 4 (more than one record), 5.5, 5.6
``sdp::test_004``               DID appendix D 12.3; Core 4.6.2
``sdp::test_005``               Core 4.7.2 (only requested attributes)
``sdp::test_006``               DID 5.7 (reserved IDs); empty result is Zephyr behaviour
``sdp::test_007``               DID 5.5 (single record)
``sdp::test_008``               Zephyr API: queued requests, ``BT_SDP_DISCOVER_UUID_STOP``
``sdp::test_009``               Zephyr API: :c:func:`bt_sdp_discover_cancel`
``sdp::test_010``               Core 4.6.2 (only requested attributes); empty result is Zephyr
                                behaviour
``disabled::test_001``          DID appendix D 12.2
=============================== ============================================================

Not covered:

* Device ID in the Extended Inquiry Response (DID 7 and 8.2, optional): the host does not build
  it, an application can only write its own EIR with :c:func:`bt_br_write_eir`.
* The optional attributes ClientExecutableURL, DocumentationURL and ServiceDescription: the
  record of the DID module has none.
* VendorIDSource ``0x0002`` on the record of the DID module: ``did.c`` sets ``0x0001``, so it is
  covered with the second record only.
* ``-EBUSY`` of :c:func:`bt_sdp_unregister_service` while an SDP channel is open: the test has
  no way to keep the SDP channel of the peer open at a known time.

Observed but not checked: the Zephyr SDP server sends the attributes in the order of the record
array, so a record declared with ``BT_SDP_NEW_SERVICE`` is not in ascending order of attribute ID
(``0x0000``, ``0x0002``, ``0x0006``, ``0x0005``, ``0x0001``, ...). The parameter descriptions of
Core 4.6.2 and 4.7.2 say "The attributes are listed in ascending order of attribute ID value."

Requirements
************

* Host environment capable of running ``native_sim`` tests.
* Python package ``bumble`` installed (used by Twister's Bumble sidecar).

Building and Running
********************

This test is intended to be run via Twister on ``native_sim``.

.. code-block:: console

   west twister -T tests/bluetooth/classic/bumble/did

A single scenario can be selected with ``-s``, for example
``-s bluetooth.classic.sim.did.disabled``.

If you see "west: command not found", activate the Zephyr virtual environment:

.. code-block:: console

   source ~/zephyrproject/.venv/bin/activate

Refer to :ref:`getting_started` for details.

Sample Output
*************

.. code-block:: console

   INFO    - Adding tasks to the queue...
   INFO    - Added initial list of jobs to queue
   INFO    - 1/2 native_sim/native         bluetooth.classic.sim.did.disabled                 PASSED (native 2.646s <host/gnu>)
   INFO    - 2/2 native_sim/native         bluetooth.classic.sim.did.sdp                      PASSED (native 15.561s <host/gnu>)
   INFO    - 2 test scenarios (2 configurations) selected, 0 configurations filtered (0 by static filter, 0 at runtime).
   INFO    - 2 of 2 executed test configurations passed (100.00%), 0 built (not run), 0 failed, 0 errored, with no warnings in 69.78 seconds.
   INFO    - 22 of 22 executed test cases passed (100.00%) on 1 out of total 1773 platforms (0.06%).
   INFO    - 2 test configurations executed on platforms, 0 test configurations were only built.
   INFO    - Saving reports...

Configuration Options
*********************

See :file:`prj.conf` for the default configuration and :file:`Kconfig` for the record values.
