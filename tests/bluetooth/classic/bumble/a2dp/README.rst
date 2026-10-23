.. _bluetooth_classic_a2dp_sim_test:

Bluetooth Classic A2DP Test
###########################

Overview
********

This test suite verifies the Zephyr Bluetooth Classic Advanced Audio Distribution Profile
(:file:`include/zephyr/bluetooth/classic/a2dp.h`) and the AVDTP signaling below it, between two
Zephyr devices using the Bumble Bluetooth controller simulator sidecar.

The test is executed on the Zephyr ``native_sim`` platform. Two simulated controllers are started
by Twister's Bumble sidecar and connected to two instances of the same test binary via
``--bt-dev=127.0.0.1:<tcp_port>``. Each instance receives a ``--peer_bd_address`` argument so the
two devices can locate each other.

* **Initiator** (``a2dp_int_<area>`` ztest suites, device 0, ``00:00:01:00:00:01``): creates the
  ACL and A2DP connections, discovers the endpoints of the peer and sends the stream commands.
* **Acceptor** (``a2dp_acp_<area>`` ztest suites, device 1, ``00:00:01:00:00:02``): accepts the
  connections and the requests (one configuration is rejected on purpose), and checks what its
  stack reports.

Both devices register the same endpoints, in the same order: an SBC sink with delay reporting
(SEID 1), an SBC source without delay reporting (SEID 2) and an SBC source with delay reporting
(SEID 3), and A2DP sink and source SDP records with AVDTP version 1.3. A test selects the local
and the remote endpoint, so the initiator can be the sink or the source.

Test Structure
**************

Both suites of a scenario contain the same test names and ztest runs them in name order, so the
two devices execute the same test at the same time. :file:`src/test_br.c` creates a new ACL
connection before every test and releases it after the test. ``test_br_sync()`` is a barrier: the
initiator sends the test name in an L2CAP ECHO request and the acceptor answers with its own. Both
sides of a test call it the same number of times: at the start, where one side has to wait for
the other, and at the end, so that the ACL connection is not released while the peer is still
checking results.

The A2DP and stream callbacks post events (:file:`src/test_common.c`); the tests wait for a
specific event and check its error code or value, and fail as soon as the ACL link is lost. When
an A2DP connection is released inside a test, both sides also wait for the release of a configured
stream, which the stack reports with the disconnection, before the events are dropped.

Implementation Notes
********************

* The commands to an unknown endpoint use the SEID 5, set in the stream as the ``modify_acp_seid``
  command of the board-to-board tests does.
* The fragmentation tests set the transmit MTU of the AVDTP L2CAP channels to 4 on both sides
  (:file:`src/test_mtu.c`, the only file using host internal headers), as the ``modify_mtu``
  command of the board-to-board tests.
* The random procedure uses a fixed seed, so a failure can be reproduced, and waits for the
  response of each command before it sends the next one.

Test Cases
**********

``bluetooth.classic.sim.a2dp.signaling_snk_int`` (``a2dp_int_signaling`` / ``a2dp_acp_signaling``)
  The board-to-board ``a2dp_sink`` signaling tests, with the initiator on the sink side. Each test
  body runs twice, with the A2DP connection released in between: set configuration (invalid
  minimum bitpool rejected), establish, release, start, suspend, reconfigure (minimum bitpool
  below the capability rejected), abort, get configuration, media MTU and delay report. The stream
  commands are first sent to an unknown endpoint; an abort of an unknown endpoint is not answered
  (AVDTP v1.3, 8.16.2) and completes with ``BT_AVDTP_TIME_OUT`` after the response timeout of the
  stack.

``bluetooth.classic.sim.a2dp.signaling_src_int``
  The same tests with ``CONFIG_TEST_A2DP_INT_SINK=n``: the initiator is on the source side, and
  the acceptor (sink) sends the delay reports.

``bluetooth.classic.sim.a2dp.robustness`` (``a2dp_int_robustness`` / ``a2dp_acp_robustness``)
  Discovery and configuration with fragmented AVDTP messages, with the initiator on the sink side
  and on the source side, and 50 random stream commands followed by a complete stream procedure on
  a new A2DP connection.

``bluetooth.classic.sim.a2dp.stream`` (``a2dp_int_stream`` / ``a2dp_acp_stream``)
  Media packets from the initiator (source) with a suspend and a restart in between, and from the
  acceptor (source) on a stream started by the sink; delay reporting configured with a first delay
  report; reconfiguration from 44.1 kHz to 48 kHz read back with get configuration; configurations
  rejected by the stack and by the application; abort before the media channel exists; discovery
  with AVDTP version 1.2, without the delay reporting capability; stream commands on a stream that
  is not configured, on a released stream and without a stream.

Board-to-board Test Mapping
***************************

========================================= ===================================================
Board-to-board test                       Test
========================================= ===================================================
``a2dp_sink`` set_configuration_cmd       ``signaling::test_001_set_configuration``
``a2dp_sink`` establish_cmd               ``signaling::test_002_establish``
``a2dp_sink`` release_cmd                 ``signaling::test_003_release``
``a2dp_sink`` start_cmd                   ``signaling::test_004_start``
``a2dp_sink`` suspend_cmd                 ``signaling::test_005_suspend``
``a2dp_sink`` reconfigure_cmd             ``signaling::test_006_reconfigure``
``a2dp_sink`` abort_cmd                   ``signaling::test_007_abort``
``a2dp_sink`` get_configuration_cmd       ``signaling::test_008_get_configuration``
``a2dp_sink`` get_mtu                     ``signaling::test_009_get_mtu``
``a2dp_sink`` delay_report_cmd            ``signaling::test_010_delay_report``
``a2dp_sink`` fragmentation               ``robustness::test_001_fragmentation_sink_int``
``a2dp_sink`` random_procedure            ``robustness::test_003_random_procedure``
``a2dp_source`` fragmentation             ``robustness::test_002_fragmentation_source_int``
========================================= ===================================================

The other ``a2dp_source`` board-to-board tests are disabled there; ``signaling_src_int`` runs the
signaling tests with the initiator on the source side instead. The ``stream`` tests have no
board-to-board counterpart.

Requirements
************

* Host environment capable of running ``native_sim`` tests.
* Python package ``bumble`` installed (used by Twister's Bumble sidecar).

Building and Running
********************

This test is intended to be run via Twister on ``native_sim``.

.. code-block:: console

   west twister -T tests/bluetooth/classic/bumble/a2dp

A single scenario can be selected with ``-s``, for example
``-s bluetooth.classic.sim.a2dp.stream``.

If you see "west: command not found", activate the Zephyr virtual environment:

.. code-block:: console

   source ~/zephyrproject/.venv/bin/activate

Refer to :ref:`getting_started` for details.

Sample Output
*************

.. code-block:: console

   INFO    - Adding tasks to the queue...
   INFO    - Added initial list of jobs to queue
   INFO    - 1/4 native_sim/native         bluetooth.classic.sim.a2dp.robustness              PASSED (native 11.392s <host/gnu>)
   INFO    - 2/4 native_sim/native         bluetooth.classic.sim.a2dp.stream                  PASSED (native 25.746s <host/gnu>)
   INFO    - 3/4 native_sim/native         bluetooth.classic.sim.a2dp.signaling_src_int       PASSED (native 63.734s <host/gnu>)
   INFO    - 4/4 native_sim/native         bluetooth.classic.sim.a2dp.signaling_snk_int       PASSED (native 63.830s <host/gnu>)
   INFO    - 4 test scenarios (4 configurations) selected, 0 configurations filtered (0 by static filter, 0 at runtime).
   INFO    - 4 of 4 executed test configurations passed (100.00%), 0 built (not run), 0 failed, 0 errored, with no warnings in 117.45 seconds.
   INFO    - 66 of 66 executed test cases passed (100.00%) on 1 out of total 1773 platforms (0.06%).
   INFO    - 118 selected test cases not executed: 118 skipped.
   INFO    - 4 test configurations executed on platforms, 0 test configurations were only built.
   INFO    - Saving reports...

Every scenario builds all suites and runs the suites of one area, so the test cases of the other
areas are reported as skipped.

Configuration Options
*********************

See :file:`prj.conf` for the default configuration; it enables the A2DP sink and source roles.

Additional configuration options can be found in :file:`Kconfig`.
