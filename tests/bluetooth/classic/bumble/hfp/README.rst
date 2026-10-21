.. _bluetooth_classic_hfp_sim_test:

Bluetooth Classic HFP Test
##########################

Overview
********

This test suite verifies the Zephyr Bluetooth Classic Hands-Free Profile, Audio Gateway
(:file:`include/zephyr/bluetooth/classic/hfp_ag.h`) and Hands-Free unit
(:file:`include/zephyr/bluetooth/classic/hfp_hf.h`) roles, between two Zephyr devices using the
Bumble Bluetooth controller simulator sidecar.

The test is executed on the Zephyr ``native_sim`` platform. Two simulated controllers are started
by Twister's Bumble sidecar and connected to two instances of the same test binary via
``--bt-dev=127.0.0.1:<tcp_port>``. Each instance receives a ``--peer_bd_address`` argument so the
two roles can locate each other.

* **AG role** (``hfp_ag_<area>`` ztest suites, device 0, ``00:00:01:00:00:01``): registers the AG
  with :c:func:`bt_hfp_ag_register` and creates the ACL connection.
* **HF role** (``hfp_hf_<area>`` ztest suites, device 1, ``00:00:01:00:00:02``): registers the HF
  with :c:func:`bt_hfp_hf_register` and accepts the ACL connection.

Each process registers only its own role, so that the AG and the HF do not compete for the SCO
server.

The simulated controller supports SCO/eSCO link setup and release with the CVSD and transparent
air modes; SCO data is not transferred. The audio tests therefore check the air mode reported for
the SCO connection, not audio samples.

Test Structure
**************

Both suites of a scenario contain the same test names and ztest runs them in name order, so the
two devices execute the same test at the same time. :file:`src/test_br.c` creates a new ACL
connection before every test and releases it after the test. Each test calls ``test_br_sync()``
when its side is ready; the AG sends the test name in an L2CAP ECHO request and the HF answers with
its own. A test calls it again where one side has to wait for the other (for example before the AG
acts on a call that the HF has seen ringing) and once more at the end, so that the ACL connection
is not released while the peer is still checking results.

The AG and HF callbacks post events (:file:`src/test_common.c`); the tests wait for a specific
event, possibly of a given call object or with a given value, and fail as soon as the ACL link is
lost.

Implementation Notes
********************

* The SLC helpers wait ``TEST_SLC_SETTLE_TIME`` after the ``connected`` callback, so that the AT
  commands the HF stack sends right after the SLC is established (gains, CMEE, COPS, CLIP, ...)
  are completed before a test sends its own commands.
* AT commands and result codes are sent from a work queue, so the L2CAP ECHO of
  ``test_br_sync()`` can reach the peer first. A side that sends something the peer can only
  observe through the state of its stack (``+BIND``, ``AT+BIA``, ``AT+CLIP``, ``AT+CCWA``) waits
  ``TEST_AT_DELIVERY_TIME`` before ``test_br_sync()``.
* The AG callbacks answer requests of the HF like the shell of the board-to-board tests: number
  ``123456789`` and memory location ``0`` are accepted for dialing, every second voice tag request
  is refused, and the subscriber number is ``87654321``.

Test Cases
**********

``bluetooth.classic.sim.hfp.slc`` (``hfp_ag_slc`` / ``hfp_hf_slc``)
  Service level connection setup and release by either role, indicators (service, signal,
  roaming, battery, ``AT+BIA`` activation), operator, gains in both directions, ECNR, voice
  recognition activated by either role (engine state, textual representation, ready to accept
  audio), voice tag phone number, subscriber number and HF indicators (enhanced safety, battery)
  with activation by the AG.

``bluetooth.classic.sim.hfp.audio`` (``hfp_ag_audio`` / ``hfp_hf_audio``)
  Audio connection setup by the AG and on request of the HF, release by either role, codec
  negotiation answered by the stack or by the HF application (CVSD, mSBC, LC3-SWB), codec IDs
  update, renegotiation from mSBC to CVSD and an unsupported codec selection.

``bluetooth.classic.sim.hfp.call`` (``hfp_ag_call`` / ``hfp_hf_call``)
  Incoming calls accepted, rejected (also twice by the AG), held (response and hold, including
  status queries with and without a held call, and a held call rejected followed by a query of
  the current calls) and terminated by either role (also followed by a query of the current
  calls),
  CLIP, in-band ring tone on and off, audio release during a call, dialing by number, memory
  location and redial, DTMF, ongoing call reported during the SLC setup and outgoing calls of the
  AG accepted or rejected by the remote party.

``bluetooth.classic.sim.hfp.multicall`` (``hfp_ag_multicall`` / ``hfp_hf_multicall``)
  Call waiting notification, three-way calling (``AT+CHLD`` 0, 1, 2, 3, 4 and ``1<idx>``),
  UDUB (also with a held call, followed by another waiting call, also right after the reject, and
  followed by a query of the current calls), a waiting call released by index followed by another
  waiting call, a waiting call rejected by the AG followed by another waiting call, list of
  current calls, hold and retrieve by the AG and explicit call transfer by either role.

Board-to-board Test Mapping
***************************

The board-to-board tests ``hfp_ag`` (``test_BR_HFP_AG_<ID>``) and ``hfp_hf``
(``test_BR_HFP_HF_<ID>``) use the same IDs. When the two variants of an ID only differ by the role
that starts the SLC, both map to one test.

======== =================================== ===================================
ID       ``hfp_ag``                          ``hfp_hf``
======== =================================== ===================================
CM_001   ``slc::test_001_cm_001_ag``         ``slc::test_002_cm_001_hf``
CM_002   ``slc::test_002_cm_001_hf``         ``slc::test_001_cm_001_ag``
CM_003   ``slc::test_003_cm_003_ag``         ``slc::test_004_cm_003_hf``
CM_004   ``slc::test_005_cm_004_ag``         ``slc::test_006_cm_004_hf``
PSI_001  ``slc::test_007_psi_001``           ``slc::test_007_psi_001``
PSI_002  ``slc::test_008_psi_002``           ``slc::test_008_psi_002``
PSI_003  ``slc::test_009_psi_003``           ``slc::test_009_psi_003``
PSI_004  ``slc::test_010_psi_004``           ``slc::test_010_psi_004``
PSI_005  ``slc::test_011_psi_005``           ``slc::test_011_psi_005``
PSI_007  ``call::test_001_psi_007_ag``       ``call::test_002_psi_007_hf``
ACH_001  ``audio::test_001_ach_001_ag``      ``audio::test_002_ach_001_hf``
ACH_002  ``audio::test_002_ach_001_hf``      ``audio::test_001_ach_001_ag``
ACH_003  ``audio::test_003_ach_003_ag``      ``audio::test_004_ach_003_hf``
ACH_004  ``audio::test_005_ach_004_ag``      ``audio::test_006_ach_004_hf``
ACH_005  ``audio::test_007_ach_005_ag``      ``audio::test_008_ach_005_hf``
ACH_006  ``audio::test_010_ach_006_ag``      ``audio::test_009_ach_006_hf``
AIC_001  ``call::test_003_aic_001``          ``call::test_003_aic_001``
AIC_002  ``call::test_004_aic_002``          ``call::test_004_aic_002``
RIC_001  ``call::test_005_ric_001``          ``call::test_005_ric_001``
RIC_002  ``call::test_006_ric_002``          ``call::test_006_ric_002``
TC_001   ``call::test_007_tc_001``           ``call::test_007_tc_001``
TC_002   ``call::test_008_tc_002``           ``call::test_008_tc_002``
ACT_001  ``call::test_009_act_001``          ``call::test_009_act_001``
ACT_002  ``call::test_010_act_002_ag``       ``call::test_011_act_002_hf``
PCN_001  ``call::test_012_pcn_001``          ``call::test_012_pcn_001``
PMD_001  ``call::test_013_pmd_001``          ``call::test_013_pmd_001``
PLN_001  ``call::test_014_pln_001``          ``call::test_014_pln_001``
CWN_001  ``multicall::test_001_cwn_001``     ``multicall::test_001_cwn_001``
3WC_001  ``multicall::test_002_3wc_001``     ``multicall::test_002_3wc_001``
3WC_002  ``multicall::test_003_3wc_002``     ``multicall::test_003_3wc_002``
3WC_003  ``multicall::test_004_3wc_003``     ``multicall::test_004_3wc_003``
CLI_001  ``call::test_015_cli_001``          ``call::test_015_cli_001``
ECNR_001 ``slc::test_014_ecnr_001``          ``slc::test_014_ecnr_001``
VRA_001  ``slc::test_015_vra_001``           ``slc::test_015_vra_001``
VT_001   ``slc::test_017_vt_001``            ``slc::test_017_vt_001``
DTMF_001 ``call::test_016_dtmf_001_ag``      ``call::test_017_dtmf_001_hf``
RAVC_001 ``slc::test_012_ravc_001``          ``slc::test_012_ravc_001``
RAVC_002 ``slc::test_013_ravc_002``          ``slc::test_013_ravc_002``
RAH_001  ``call::test_018_rah_001``          ``call::test_018_rah_001``
RAH_002  ``call::test_019_rah_002``          ``call::test_019_rah_002``
RAH_003  ``call::test_020_rah_003``          ``call::test_020_rah_003``
RAH_004  ``call::test_021_rah_004``          ``call::test_021_rah_004``
RAH_005  ``call::test_022_rah_005``          ``call::test_022_rah_005``
RAH_006  ``call::test_023_rah_006``          ``call::test_023_rah_006``
RAH_007  ``call::test_024_rah_007``          ``call::test_024_rah_007``
RAH_008  ``call::test_025_rah_008``          ``call::test_025_rah_008``
SNI_001  ``slc::test_018_sni_001``           ``slc::test_018_sni_001``
ECS_001  ``call::test_026_ecs_001_ag``       ``multicall::test_005_ecs_001_hf``
ECS_002  ``multicall::test_006_ecs_002``     ``multicall::test_006_ecs_002``
IIA_001  ``slc::test_019_iia_001``           ``slc::test_019_iia_001``
WBS_001  ``audio::test_009_ach_006_hf``      ``audio::test_008_ach_005_hf``
CN_001   ``audio::test_009_ach_006_hf``      ``audio::test_008_ach_005_hf``
CN_002   ``audio::test_008_ach_005_hf``      ``audio::test_009_ach_006_hf``
CN_003   ``audio::test_011_cn_003_ag``       ``audio::test_012_cn_003_hf``
CN_004   ``audio::test_013_cn_004``          ``audio::test_013_cn_004``
HFI_001  ``slc::test_020_hfi_001``           ``slc::test_020_hfi_001``
HFI_002  ``slc::test_021_hfi_002``           ``slc::test_021_hfi_002``
SWBS_001 ``audio::test_014_swbs_001_ag``     ``audio::test_015_swbs_001_hf``
======== =================================== ===================================

Tests without a board-to-board counterpart: ``slc::test_016_vra_ag``,
``call::test_027_inband_ringtone``, ``call::test_028_ag_outgoing``,
``call::test_029_ag_outgoing_rejected``, ``call::test_030_rah_query_no_held_call``,
``call::test_031_rah_query_then_hold``, ``call::test_032_ag_reject_twice``,
``call::test_033_terminate_then_query``, ``call::test_034_rah_reject_then_query``,
``multicall::test_007_release_all_held``,
``multicall::test_008_udub``, ``multicall::test_009_release_active_accept_other``,
``multicall::test_010_ag_hold_retrieve``, ``multicall::test_011_ag_explicit_call_transfer``,
``multicall::test_012_udub_with_held_call``, ``multicall::test_013_udub_then_waiting``,
``multicall::test_014_udub_then_waiting_at_once``,
``multicall::test_015_release_waiting_by_index``, ``multicall::test_016_udub_then_query`` and
``multicall::test_017_ag_reject_waiting``.

Requirements
************

* Host environment capable of running ``native_sim`` tests.
* Python package ``bumble`` installed (used by Twister's Bumble sidecar).

Building and Running
********************

This test is intended to be run via Twister on ``native_sim``.

.. code-block:: console

   west twister -T tests/bluetooth/classic/bumble/hfp

A single scenario can be selected with ``-s``, for example
``-s bluetooth.classic.sim.hfp.audio``.

If you see "west: command not found", activate the Zephyr virtual environment:

.. code-block:: console

   source ~/zephyrproject/.venv/bin/activate

Refer to :ref:`getting_started` for details.

Sample Output
*************

.. code-block:: console

   INFO    - Adding tasks to the queue...
   INFO    - Added initial list of jobs to queue
   INFO    - 1/4 native_sim/native         bluetooth.classic.sim.hfp.audio                    PASSED (native 97.616s <host/gnu>)
   INFO    - 2/4 native_sim/native         bluetooth.classic.sim.hfp.slc                      PASSED (native 131.740s <host/gnu>)
   INFO    - 3/4 native_sim/native         bluetooth.classic.sim.hfp.multicall                PASSED (native 142.658s <host/gnu>)
   INFO    - 4/4 native_sim/native         bluetooth.classic.sim.hfp.call                     PASSED (native 240.520s <host/gnu>)
   INFO    - 4 test scenarios (4 configurations) selected, 0 configurations filtered (0 by static filter, 0 at runtime).
   INFO    - 4 of 4 executed test configurations passed (100.00%), 0 built (not run), 0 failed, 0 errored, with no warnings in 314.47 seconds.
   INFO    - 174 of 174 executed test cases passed (100.00%) on 1 out of total 1773 platforms (0.06%).
   INFO    - 522 selected test cases not executed: 522 skipped.
   INFO    - 4 test configurations executed on platforms, 0 test configurations were only built.
   INFO    - Saving reports...

Every scenario builds all suites and runs the suites of one area, so the test cases of the other
areas are reported as skipped.

Configuration Options
*********************

See :file:`prj.conf` for the default configuration; it enables the HFP features used by the
board-to-board tests.

Additional configuration options can be found in :file:`Kconfig`.
