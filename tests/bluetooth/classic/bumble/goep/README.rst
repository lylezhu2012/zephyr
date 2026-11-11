GOEP Native Simulator Tests
###########################

Two native_sim Zephyr hosts exchange GOEP and OBEX traffic through Bumble
controllers. Device 0 (``00:00:01:00:00:01``) initiates the ACL and GOEP
connections; device 1 (``00:00:01:00:00:02``) accepts them. The suite uses
public GOEP/OBEX APIs, RFCOMM channel 10 and a test L2CAP PSM of ``0x1001``.
It does not use a mock transport, shell commands or another profile.

Configurations
**************

.. list-table:: Expected execution counts
   :header-rows: 1

   * - Configuration
     - Cases per transport
   * - ``bluetooth.classic.sim.goep.connect.{rfcomm,l2cap}``
     - 16
   * - ``bluetooth.classic.sim.goep.put_get.{rfcomm,l2cap}``
     - 20
   * - ``bluetooth.classic.sim.goep.misc.{rfcomm,l2cap}``
     - 30

The six source-mapped configurations contain 33 paired scenarios, or 132 role/transport
case executions in a complete run. Each pair repeats with fresh connections
and both cleanup initiators; repetitions are not additional ztest cases.
Explicit transport-disconnect scenarios independently exercise all four
transport/ACL cleanup-initiator combinations.
Run from the west workspace parent, selecting the intended Zephyr checkout::

   export ZEPHYR_BASE=/path/to/zephyr
   west twister -p native_sim -T "$ZEPHYR_BASE/tests/bluetooth/classic/bumble/goep" \
       -O /path/to/fresh-output --inline-logs -v

OBEX Regression Configurations
******************************

Five additional L2CAP configurations exercise the shared OBEX implementation:

* ``api_helpers.l2cap``: eight role cases covering one-byte SSN encoding and decoding
  (0, 127 and 255), adjacent headers, range/tailroom errors, empty versus missing
  End-of-Body, truncated TLVs, callback-requested early stop and UUID registration.
  The second-server registration assertion runs on the server; its client case
  only coordinates connection lifecycle.
* ``abort_empty.l2cap`` and ``abort_headers.l2cap``: two role cases each, checking
  ABORT responses without headers and with exact Description bytes after a PUT Continue.
* ``abort_matrix.l2cap``: forty-six role cases covering pending PUT/GET responses,
  a pending terminal GET response, GET/ACTION between packets, continued ACTION,
  ACTION after a completed SRM PUT, SRM PUT and a legacy single empty response.
  Additional GET vectors check an old empty error response, SRM Continue and
  terminal responses (including an empty End-of-Body), and the SRM single-response control.
  Successful cancellation pairs send a fresh PUT to check recovery. Ambiguous Success
  pairs instead require transport closure without guessed operation or ABORT callbacks.
  Final-PUT vectors verify identical request headers for single/dual empty and Description
  Success replies; a separate SRM final-PUT vector exercises the outstanding terminal boundary.
  SRMP Wait controls distinguish a received PUT/GET Continue with no next request from
  a newly sent request whose response remains pending. Only the former completes ABORT
  and then checks a fresh PUT; the latter still requires transport closure.

* ``abort_errors.l2cap``: fourteen role cases covering ambiguous SRM errors,
  bounded failure, contradictory replies, response-header provenance and callback reentry.

The full directory selects eleven configurations and 204 role/transport case executions.
The new matrix uses public L2CAP/RFCOMM send APIs on the test-owned transport to
place old responses before the ABORT response. It does not inject private OBEX
state, add fixed sleeps or increase timeouts. These are callback-routing regression
vectors, not a claim that every response sequence is standards-conformant.
An old terminal response and a legacy single ABORT response can have identical
bytes, including optional Description headers. With no proven response boundary,
both single and dual indistinguishable Success sequences now fail closed rather
than being guessed as successful cancellation. SRM GET Continue is still delivered
as GET data, but it does not prove that a later Success belongs to ABORT. An identified
GET End-of-Body and ordinary non-final SRM PUT remain separate positive controls.
The dual-Success vectors distinguish two successful local submissions from a second send
rejected with ``-ENOTCONN`` after the first response closes the transport. The latter also
requires an actual transport-disconnected event and no operation callbacks; other send errors
still fail. A separate event-driven control waits for closure before the second send.
Submission counts are logged; neither outcome proves delivery of both responses to the client.
Send-error rollback, multi-client isolation, callback reentry and sustained SRM GET
traffic remain outside exhaustive coverage of this matrix.

Assertions And Boundaries
*************************

* Connection callbacks check the session, OBEX version ``0x10`` and maximum
  OBEX packet length 255. Transport-only tests cover explicit transport
  disconnect and implicit teardown on ACL release. A separate scenario drops
  an ACL with an established OBEX connection, without OBEX DISCONNECT.
* Authentication preserves server challenges with passwords ``1234`` and
  ``0000``, and a client challenge with ``0000``. The tests parse the nonce
  and digest TLVs, retry CONNECT after Unauthorized where applicable, verify
  the digest and reject verification with a different password. These are
  fixed test credentials, not production authentication policy.
* Directed CONNECT checks the exact 16 bytes ``1234567812345678`` in Target
  and WHO, and Connection ID 1. DISCONNECT checks WHO and the same ID.
  The source-mapped OBEX DISCONNECT scenarios then release the ACL without
  an explicit transport disconnect. The basic scenario also retains fully
  ordered OBEX/transport/ACL shutdown as additional coverage.
* PUT/GET check final bits, Continue/Success/error responses and exact body
  lengths/bytes including the terminating NUL. SRM and SRMP scenarios check
  header value 1 in both directions. They do not prove sustained SRM flow
  control, SRMP wait/resume behavior or protocol conformance.
* ACTION checks Action ID and UTF-16BE Name/Destination Name bytes. The
  source permission variant deliberately retains Name/Destination Name
  without adding a Permissions header. MOVE and SET_PERM Not Found/SRM
  variants remain separate. These are source regression vectors, not a
  claim that every vector is a standards-valid file operation.
* Active ABORT follows a non-final PUT and Continue; idle ABORT checks the
  current implementation's ``-EINVAL``. SETPATH checks exact flags/name
  bytes and Success, Not Found or Forbidden responses.
* The L2CAP ``abort_errors`` configuration covers a retained SRM GET error
  followed by ABORT Success, a lone error with a five-second failure timeout,
  contradictory replies, and failure after an identified GET terminal reply.
  It also checks retained Description bytes and rejection of PUT reentry
  from the GET callback while ABORT remains pending. These policy tests do
  not establish universal legacy compatibility or exhaustive
  timeout/transport-disconnection race coverage.
* Authentication and operation scenarios release the ACL directly after
  their responses, without preceding OBEX or transport disconnect requests.
  Both transport-disconnected callbacks are checked before session reuse.
* Received operation headers are copied before callbacks return. An event
  queue carries operation/status/header data to the test thread. The acceptor
  admits one ACL only when waiting for the next connection, after previous
  cleanup has returned. An early attempt receives a temporary no-resources
  rejection; only that result is retried within the existing connection
  timeout. Each transport open adopts the current ACL generation.

Each host has one session and one peer. Concurrent connections, multiple
instances, both ACL initiation roles, physical radios and filesystem effects
are outside this suite. The source server-view cases initiate their own ACL;
this harness instead fixes ACL initiation to device 0 and retains both
cleanup directions. Source L2CAP PSM 11 is replaced by the dynamic test PSM
``0x1001``. OBEX operation coverage over L2CAP extends the source tests,
whose L2CAP cases exercise only transport connection/disconnection.

Source Traceability
*******************

There are 32 client and 31 server source IDs. Tables preserve their spelling,
including ``PUT_SUCCESS_FINAl``. A destination names the paired test suffix
in ``goep_client_<area>`` and ``goep_server_<area>``. RFCOMM/L2CAP transport
IDs select their respective configuration. All other destinations run over
both transports. Shared destinations merge viewpoints, not parameter variants.
The mapping has undergone independent review. The cleanup-path findings
have been addressed; independent re-review of these fixes is pending.

.. list-table:: Client IDs from b2b/goep_c/pytest/test_goep_c.py
   :header-rows: 1

   * - Source ID
     - Area / paired test
   * - ``test_BR_GOEP_CLIENT_TRANSPORT_RFCOMM_CONNECT_001``
     - connect / ``test_007_transport_acl_teardown``
   * - ``test_BR_GOEP_CLIENT_TRANSPORT_RFCOMM_DISCONNECT_001``
     - connect / ``test_008_transport_disconnect``
   * - ``test_BR_GOEP_CLIENT_OBEX_CONNECT_NO_AUTH_001``
     - connect / ``test_006_obex_acl_teardown``
   * - ``test_BR_GOEP_CLIENT_OBEX_CONNECT_AUTH_RESPONSE_001``
     - connect / ``test_003_server_challenge_1234``
   * - ``test_BR_GOEP_CLIENT_OBEX_CONNECT_AUTH_CHALLENGE_001``
     - connect / ``test_005_client_challenge_0000``
   * - ``test_BR_GOEP_CLIENT_PUT_SUCCESS_FINAL_001``
     - put_get / ``test_001_put_success_final``
   * - ``test_BR_GOEP_CLIENT_PUT_CONTINUE_NON_FINAL_001``
     - put_get / ``test_002_put_continue_non_final``
   * - ``test_BR_GOEP_CLIENT_PUT_ERROR_001``
     - put_get / ``test_003_put_error``
   * - ``test_BR_GOEP_CLIENT_PUT_SRM_ENABLE_001``
     - put_get / ``test_004_put_srm_enable``
   * - ``test_BR_GOEP_CLIENT_PUT_SRM_PARAM_001``
     - put_get / ``test_005_put_srm_param``
   * - ``test_BR_GOEP_CLIENT_GET_SUCCESS_FINAL_001``
     - put_get / ``test_006_get_success_final``
   * - ``test_BR_GOEP_CLIENT_GET_CONTINUE_NON_FINAL_001``
     - put_get / ``test_007_get_continue_non_final``
   * - ``test_BR_GOEP_CLIENT_GET_NOT_FOUND_001``
     - put_get / ``test_008_get_not_found``
   * - ``test_BR_GOEP_CLIENT_GET_SRM_ENABLE_001``
     - put_get / ``test_009_get_srm_enable``
   * - ``test_BR_GOEP_CLIENT_GET_SRM_PARAM_001``
     - put_get / ``test_010_get_srm_param``
   * - ``test_BR_GOEP_CLIENT_ACTION_COPY_SUCCESS_001``
     - misc / ``test_001_action_copy``
   * - ``test_BR_GOEP_CLIENT_ACTION_MOVE_SUCCESS_001``
     - misc / ``test_002_action_move``
   * - ``test_BR_GOEP_CLIENT_ACTION_PERMISSION_SUCCESS_001``
     - misc / ``test_003_action_permission_source_variant``
   * - ``test_BR_GOEP_CLIENT_ACTION_NOT_FOUND_001``
     - misc / ``test_004_action_move_not_found``
   * - ``test_BR_GOEP_CLIENT_ACTION_SRM_ENABLE_001``
     - misc / ``test_006_action_move_srm``
   * - ``test_BR_GOEP_CLIENT_ACTION_SRM_PARAM_001``
     - misc / ``test_008_action_srmp``
   * - ``test_BR_GOEP_CLIENT_ABORT_ACTIVE_REQUEST_001``
     - misc / ``test_009_abort_active``
   * - ``test_BR_GOEP_CLIENT_ABORT_NO_ACTIVE_REQUEST_001``
     - misc / ``test_010_abort_idle``
   * - ``test_BR_GOEP_CLIENT_SETPATH_ROOT_SUCCESS_001``
     - misc / ``test_011_setpath_root``
   * - ``test_BR_GOEP_CLIENT_SETPATH_DOWN_SUCCESS_001``
     - misc / ``test_012_setpath_down``
   * - ``test_BR_GOEP_CLIENT_SETPATH_UP_SUCCESS_001``
     - misc / ``test_013_setpath_up``
   * - ``test_BR_GOEP_CLIENT_SETPATH_NO_CREATE_FAIL_001``
     - misc / ``test_014_setpath_no_create``
   * - ``test_BR_GOEP_CLIENT_SETPATH_FORBIDDEN_001``
     - misc / ``test_015_setpath_forbidden``
   * - ``test_BR_GOEP_CLIENT_OBEX_DISCONNECT_SUCCESS_001``
     - connect / ``test_001_obex_connect_disconnect``
   * - ``test_BR_GOEP_CLIENT_OBEX_DISCONNECT_WITH_CONN_ID_001``
     - connect / ``test_002_connection_id``
   * - ``test_BR_GOEP_CLIENT_TRANSPORT_L2CAP_CONNECT_001``
     - connect / ``test_007_transport_acl_teardown``
   * - ``test_BR_GOEP_CLIENT_TRANSPORT_L2CAP_DISCONNECT_001``
     - connect / ``test_008_transport_disconnect``

.. list-table:: Server IDs from b2b/goep_s/pytest/test_goep_s.py
   :header-rows: 1

   * - Source ID
     - Area / paired test
   * - ``test_BR_GOEP_SERVER_TRANSPORT_RFCOMM_ACCEPT_001``
     - connect / ``test_007_transport_acl_teardown``
   * - ``test_BR_GOEP_SERVER_TRANSPORT_RFCOMM_DISCONNECT_001``
     - connect / ``test_008_transport_disconnect``
   * - ``test_BR_GOEP_SERVER_OBEX_CONNECT_NO_AUTH_001``
     - connect / ``test_006_obex_acl_teardown``
   * - ``test_BR_GOEP_SERVER_OBEX_CONNECT_AUTH_CHALLENGE_001``
     - connect / ``test_004_server_challenge_0000``
   * - ``test_BR_GOEP_SERVER_OBEX_CONNECT_AUTH_RESPONSE_001``
     - connect / ``test_005_client_challenge_0000``
   * - ``test_BR_GOEP_SERVER_PUT_SUCCESS_FINAl_001``
     - put_get / ``test_001_put_success_final``
   * - ``test_BR_GOEP_SERVER_PUT_CONTINUE_NON_FINAL_001``
     - put_get / ``test_002_put_continue_non_final``
   * - ``test_BR_GOEP_SERVER_PUT_ERROR_001``
     - put_get / ``test_003_put_error``
   * - ``test_BR_GOEP_SERVER_PUT_SRM_ENABLE_001``
     - put_get / ``test_004_put_srm_enable``
   * - ``test_BR_GOEP_SERVER_PUT_SRM_PARAM_001``
     - put_get / ``test_005_put_srm_param``
   * - ``test_BR_GOEP_SERVER_GET_SUCCESS_FINAL_001``
     - put_get / ``test_006_get_success_final``
   * - ``test_BR_GOEP_SERVER_GET_CONTINUE_NON_FINAL_001``
     - put_get / ``test_007_get_continue_non_final``
   * - ``test_BR_GOEP_SERVER_GET_NOT_FOUND_001``
     - put_get / ``test_008_get_not_found``
   * - ``test_BR_GOEP_SERVER_GET_SRM_ENABLE_001``
     - put_get / ``test_009_get_srm_enable``
   * - ``test_BR_GOEP_SERVER_GET_SRM_PARAM_001``
     - put_get / ``test_010_get_srm_param``
   * - ``test_BR_GOEP_SERVER_ACTION_COPY_SUCCESS_001``
     - misc / ``test_001_action_copy``
   * - ``test_BR_GOEP_SERVER_ACTION_MOVE_SUCCESS_001``
     - misc / ``test_002_action_move``
   * - ``test_BR_GOEP_SERVER_ACTION_PERMISSION_SUCCESS_001``
     - misc / ``test_003_action_permission_source_variant``
   * - ``test_BR_GOEP_SERVER_ACTION_NOT_FOUND_001``
     - misc / ``test_005_action_permission_not_found``
   * - ``test_BR_GOEP_SERVER_ACTION_SRM_ENABLE_001``
     - misc / ``test_007_action_permission_srm``
   * - ``test_BR_GOEP_SERVER_ACTION_SRM_PARAM_001``
     - misc / ``test_008_action_srmp``
   * - ``test_BR_GOEP_SERVER_ABORT_ACTIVE_REQUEST_001``
     - misc / ``test_009_abort_active``
   * - ``test_BR_GOEP_SERVER_SETPATH_ROOT_SUCCESS_001``
     - misc / ``test_011_setpath_root``
   * - ``test_BR_GOEP_SERVER_SETPATH_DOWN_SUCCESS_001``
     - misc / ``test_012_setpath_down``
   * - ``test_BR_GOEP_SERVER_SETPATH_UP_SUCCESS_001``
     - misc / ``test_013_setpath_up``
   * - ``test_BR_GOEP_SERVER_SETPATH_NO_CREATE_FAIL_001``
     - misc / ``test_014_setpath_no_create``
   * - ``test_BR_GOEP_SERVER_SETPATH_FORBIDDEN_001``
     - misc / ``test_015_setpath_forbidden``
   * - ``test_BR_GOEP_SERVER_OBEX_DISCONNECT_SUCCESS_001``
     - connect / ``test_001_obex_connect_disconnect``
   * - ``test_BR_GOEP_SERVER_OBEX_DISCONNECT_WITH_CONN_ID_001``
     - connect / ``test_002_connection_id``
   * - ``test_BR_GOEP_SERVER_TRANSPORT_L2CAP_ACCEPT_001``
     - connect / ``test_007_transport_acl_teardown``
   * - ``test_BR_GOEP_SERVER_TRANSPORT_L2CAP_DISCONNECT_001``
     - connect / ``test_008_transport_disconnect``

Development Evidence
********************

The isolated implementation is based on ``a31a10410d38`` (Zephyr 4.4.99).
Development evidence is under ``/home/test/bt_sim_runs/goep_20261105``;
each run has source hashes and fresh output. Selected, skipped tests are
not counted as executed passes.

* ``acl_owner1``: four earlier connect/put_get configurations, 44/44 actual
  cases passed after the test-side ACL ownership repair (102.51 seconds).
* ``misc2``: two misc configurations, 60/60 actual cases passed
  (107.87 seconds). This predates the later connect additions.
* ``connid1``: targeted connection development, 8/8 actual cases passed
  (43.80 seconds).
* ``auth1``: authentication development, 20/20 actual cases passed
  (59.52 seconds).
* ``connect_complete1`` was interrupted with zero executed cases. It is not
  a successful validation run.
* ``connect_complete3``: complete connection slice, 32/32 actual cases
  passed (68.88 seconds). Its actual Twister output includes::

     INFO    - 32 of 32 executed test cases passed (100.00%) on 1 out of total 1773 platforms (0.06%).
     INFO    - 100 selected test cases not executed: 100 skipped.

* ``author_operations1``: four put_get/misc configurations, 100/100 actual
  cases passed after the shared connection helper changes (139.12 seconds);
  164 selected cases were skipped. This and the separate connection run are
  development regressions, not a complete formal acceptance run.

Independent review completed on 2026-11-06. The three prioritized findings
were a cleanup/reconnect race, replaced source ACL-release paths and a missing
mixed transport/ACL disconnect combination. Fix-development evidence is under
``/home/test/bt_sim_runs/goep_fixes_20261106``:

* ``r1``: targeted L2CAP reconnect, 2/2 actual cases passed (55.68 seconds).
* ``r1_delay``: the previous 1000 ms acceptor-cleanup delay reproduced on
  both transports, 4/4 actual cases passed (52.04 seconds). The isolated
  probe asserts that no new ACL exists before the old unpair completes.
  This probe is not part of the delivered source.
* ``r2``: RFCOMM connect and PUT/GET cleanup, 36/36 actual cases passed
  (113.75 seconds).
* ``r3``: L2CAP connect with all disconnect combinations, 16/16 actual
  cases passed (73.57 seconds).
* ``remaining``: RFCOMM connect, L2CAP PUT/GET and both misc configurations,
  96/96 actual cases passed (126.21 seconds).

These are staged repair regressions, not one formal acceptance run or
stability rounds. Independent re-review, the complete formal six-configuration
acceptance run and three fresh stability rounds remain pending. Specification
review identified limitations of preserved source vectors; no protocol
conformance or hardware result is claimed. No protocol-stack or simulator
source is changed by this suite.
