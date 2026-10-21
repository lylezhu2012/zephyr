.. _bluetooth_classic_rfcomm_sim_test:

Bluetooth Classic RFCOMM Test
#############################

Overview
********

This test suite verifies the Zephyr Bluetooth Classic (BR/EDR) RFCOMM API
(:file:`include/zephyr/bluetooth/classic/rfcomm.h`) between two Zephyr devices
using the Bumble Bluetooth controller simulator sidecar.

The test is executed on the Zephyr ``native_sim`` platform. Two simulated
controllers are started by Twister's Bumble sidecar and connected to two
instances of the same test binary via ``--bt-dev=127.0.0.1:<tcp_port>``. Each
instance receives a ``--peer_bd_address`` argument so the two roles can locate
each other.

The test binary is started twice:

* **Client role** (``rfcomm_client`` ztest suite, device 0,
  ``00:00:01:00:00:01``): creates the ACL connection, opens RFCOMM DLCs with
  :c:func:`bt_rfcomm_dlc_connect`, exchanges data and closes the DLCs.
* **Server role** (``rfcomm_server`` ztest suite, device 1,
  ``00:00:01:00:00:02``): registers RFCOMM server channels with
  :c:func:`bt_rfcomm_server_register`, accepts the incoming DLCs, exchanges data
  and waits for the client to disconnect.

Test Structure
**************

Both suites contain the same test names and ztest runs them in name order, so
the two devices execute the same test at the same time. The common code in
:file:`src/test_br.c` does the following:

* The suite setup enables Bluetooth on both devices. The client runs one
  inquiry until it finds the server, which proves that the server host is up.
* A ztest rule creates a new ACL connection before every test (client) or waits
  for it (server), and releases it after every test, so each test starts from a
  fresh ACL link.
* Each test calls ``test_br_sync()`` once its own side is ready (for example
  after the server registered its channels). The client sends the test name in
  an L2CAP ECHO request and the server answers with its own test name. If one
  device fails a test early, the other device detects the mismatch and both
  continue in step with the next test.
* Waits for a peer event return as soon as the ACL link is lost, so a failure on
  one device ends the same test on the other device quickly.

Implementation Notes
********************

* The server channels are fixed through Kconfig
  (``CONFIG_TEST_RFCOMM_CHANNEL_A``, ``CONFIG_TEST_RFCOMM_CHANNEL_B``,
  ``CONFIG_TEST_RFCOMM_CHANNEL_CLIENT`` and
  ``CONFIG_TEST_RFCOMM_CHANNEL_UNREGISTERED``). Both roles run the same image,
  so no SDP query is needed to find them.
* Each payload is generated from a seed and a length known to both roles
  (:file:`src/test_common.h`). The receiver records every frame in the
  :c:member:`bt_rfcomm_dlc_ops.recv` callback and the test thread compares the
  length and content against the expected pattern.
* Both roles request ``CONFIG_BT_RFCOMM_L2CAP_MTU`` as DLC MTU; the frames of
  the MTU test use the MTU the stack reports in ``dlc->mtu`` after the DLC is
  connected.
* ``CONFIG_BT_BUF_ACL_RX_COUNT_EXTRA`` is raised so that the stack can grant
  more than one RX credit per DLC.

Test Cases
**********

``bluetooth.classic.sim.rfcomm.basic`` runs all tests of
``rfcomm_client::*`` on device 0 and ``rfcomm_server::*`` on device 1:

``test_001_basic``
  One DLC on channel A. The client sends a payload which the server checks,
  the server answers with a payload which the client checks, then the client
  disconnects the DLC. Also checks the local return codes of
  :c:func:`bt_rfcomm_server_register` (missing ``accept``, channel out of
  range, channel in use, dynamic channel allocation),
  :c:func:`bt_rfcomm_server_unregister`, :c:func:`bt_rfcomm_dlc_connect`
  (no ACL, ``NULL`` DLC, channel out of range, invalid MTU) and
  :c:func:`bt_rfcomm_dlc_send` on a DLC that is not connected.

``test_002_unregistered_channel``
  The client connects a DLC to a channel the server has not registered. The
  test checks that only the ``disconnected`` callback is called and the server
  ``accept`` callback is not. After the RFCOMM session has been released, a
  DLC to channel A is connected and data is exchanged in both directions.

``test_003_multiple_dlcs``
  Two DLCs (channels A and B) on the same RFCOMM session. A second DLC to an
  already connected channel returns ``-EBUSY``. Distinct payloads are sent in
  both directions on each DLC and checked for correct routing. The server then
  disconnects DLC B; the test checks that DLC A is still connected and still
  carries data before the client disconnects it.

``test_004_mtu_and_credits``
  ``CONFIG_TEST_RFCOMM_FRAME_COUNT`` frames are sent in each direction with
  lengths of 1, 127, 128 octets and the negotiated MTU, which is more frames
  than the initial credit count. Every frame is checked for length and
  content, so RX credits have to be replenished during the transfer. Sending
  ``MTU + 1`` octets returns ``-EMSGSIZE``.

``test_005_deferred_recv``
  The server sets :c:member:`bt_rfcomm_dlc.rx_credit_limit` to 3 and returns
  ``-EINPROGRESS`` from the ``recv`` callback. The client queues 8 frames. The
  server checks that exactly 3 frames arrive while the buffers are held, then
  completes them with :c:func:`bt_rfcomm_dlc_recv_complete` and checks that
  all 8 frames arrive in order with the expected content. Also checks the
  ``-EINVAL`` return code of :c:func:`bt_rfcomm_dlc_recv_complete` for
  ``NULL`` arguments.

``test_006_rpn_rls``
  The client sends :c:func:`bt_rfcomm_send_rpn_cmd` and
  :c:func:`bt_rfcomm_send_rls_cmd` commands and the server sends an RLS
  command on a connected DLC; data is then exchanged in both directions to
  check that the DLC still works. Also checks the ``-EINVAL`` return codes for
  invalid arguments and ``-ENOTCONN`` before the DLC is connected and after it
  is disconnected. The Zephyr API has no callback for received RPN/RLS
  commands, so the test only checks return codes and that the DLC keeps
  working.

``test_007_acceptor_initiated_dlc``
  The client also registers an RFCOMM server channel. After the client has
  opened DLC A, the server connects a DLC to the client channel over the same
  RFCOMM session, exchanges data in both directions on it and disconnects it.
  DLC A is then checked to still carry data.

Requirements
************

* Host environment capable of running ``native_sim`` tests.
* Python package ``bumble`` installed (used by Twister's Bumble sidecar).

Building and Running
********************

This test is intended to be run via Twister on ``native_sim``.

.. code-block:: console

   west twister -T tests/bluetooth/classic/bumble/rfcomm

Twister invokes the ``bumble`` sidecar defined in :file:`tests.yaml`. The
sidecar starts two Bumble controllers with fixed BD_ADDR values
(``00:00:01:00:00:01`` and ``00:00:01:00:00:02``) and passes the matching
``--peer_bd_address`` and ``-test`` arguments to each binary instance
automatically.

If you see "west: command not found", activate the Zephyr virtual environment:

.. code-block:: console

   source ~/zephyrproject/.venv/bin/activate

Refer to :ref:`getting_started` for details.

Sample Output
*************

.. code-block:: console

   INFO    - Adding tasks to the queue...
   INFO    - Added initial list of jobs to queue
   INFO    - 1 test scenarios (1 configurations) selected, 0 configurations filtered (0 by static filter, 0 at runtime).
   INFO    - 1 of 1 executed test configurations passed (100.00%), 0 built (not run), 0 failed, 0 errored, with no warnings in 46.43 seconds.
   INFO    - 14 of 14 executed test cases passed (100.00%) on 1 out of total 1773 platforms (0.06%).
   INFO    - 1 test configurations executed on platforms, 0 test configurations were only built.
   INFO    - Saving reports...

Test Coverage
*************

This test covers:

* RFCOMM server channel registration and unregistration
* DLC connection, data transfer and disconnection from the initiator and the
  acceptor side
* Refused DLC connection to an unregistered channel
* Multiple DLCs on one RFCOMM session
* Frames from 1 octet up to the negotiated MTU and RX credit replenishment
* Deferred RX completion with ``-EINPROGRESS`` and
  :c:func:`bt_rfcomm_dlc_recv_complete`
* RPN and RLS command transmission
* Return codes of the RFCOMM API for invalid arguments and states

Configuration Options
*********************

See :file:`prj.conf` for the default configuration.

Additional configuration options can be found in :file:`Kconfig`.
