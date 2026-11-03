AVRCP Simulator Tests
#####################

These native_sim tests run two Zephyr Bluetooth hosts with Bumble controllers.
Device 0 initiates the ACL and AVRCP connections and exercises the controller
role. Device 1 exercises the target role. Both hosts register both AVRCP roles.

Scenarios
*********

The ``control`` suites contain 18 CT and 18 TG cases, or 36 total.
The ``browsing`` suites contain 9 CT and 9 TG cases, or 18 total.
The ``robustness`` suites contain 13 CT and 13 TG cases, or 26 total.
Together they contain 40 pairs, or 80 individual test cases.

Each suffix follows ``bluetooth.classic.sim.avrcp.`` in ``tests.yaml``. The
matching suites are ``avrcp_ct_<suffix>`` and ``avrcp_tg_<suffix>``.

The regular request/response helpers execute their body three times: before
a CT-initiated profile disconnect, before a TG-initiated profile disconnect,
and after reconnecting again. Browsing tests also cover explicit browsing
disconnect and closing browsing through a control disconnect. These helpers
retain their original ``test_ct_run_twice`` and ``test_tg_run_twice`` names.
PlayItem and AddToNowPlaying run both without and with a browsing connection.
Both peers explicitly disconnect browsing during the lifecycle sequence.

The robustness scenario additionally checks API arguments, connection state,
caller buffer ownership on rejected sends, notification data, ACL loss with
browsing connected, and reconnecting without a browsing-disconnected callback.
After the lifecycle checks, an actual request/response confirms that the
reconnected channel works. Cleanup errors fail the test rather than only
writing a log message. Test 013 queues three vendor responses and checks every
transaction ID, status, length, capability, and company ID at the controller.

Running
*******

From a Linux west workspace with the test dependencies installed::

   export ZEPHYR_BASE=/path/to/zephyr
   python3 "$ZEPHYR_BASE/scripts/twister" -p native_sim \
       -T "$ZEPHYR_BASE/tests/bluetooth/classic/bumble/avrcp" \
       -O /tmp/avrcp-results --inline-logs -v -j 1

Use a new output directory for each complete repeat. A zero exit status alone
is not sufficient: verify that all three configurations and 80 cases executed.
An invocation that executes zero cases does not count as a successful repeat.

Board-to-Board Traceability
***************************

The source inventories are ``b2b/avrcp_ct/pytest/test_avrcp.py`` (26 IDs) and
``b2b/avrcp_tg/pytest/test_avrcp.py`` (28 IDs). Every original top-level test ID
is listed below. Each destination test exists in both the CT and TG suites
for the stated scenario. Play and pause share one paired test; the TG invalid
status/buffer case is split into two paired tests.

.. list-table:: Original IDs and simulator destinations
   :header-rows: 1
   :widths: 25 25 12 38

   * - Original CT ID
     - Original TG ID
     - Scenario
     - Destination test
   * - ``test_avrcp_ct_unit_info_cmd``
     - ``test_avrcp_tg_unit_info_rsp``
     - control
     - ``test_001_unit_info``
   * - ``test_avrcp_ct_subunit_info_cmd``
     - ``test_avrcp_tg_subunit_info_rsp``
     - control
     - ``test_002_subunit_info``
   * - ``test_avrcp_ct_play``
     - ``test_avrcp_tg_play_rsp``
     - control
     - ``test_003_passthrough``
   * - ``test_avrcp_ct_pause``
     - ``test_avrcp_tg_pause_rsp``
     - control
     - ``test_003_passthrough``
   * - ``test_avrcp_ct_get_caps``
     - ``test_avrcp_tg_get_caps_rsp``
     - control
     - ``test_004_get_caps``
   * - ``test_avrcp_ct_register_notification``
     - ``test_avrcp_tg_register_notification_rsp``
     - control
     - ``test_005_register_notification``
   * - ``test_avrcp_ct_set_absolute_volume``
     - ``test_avrcp_tg_set_absolute_volume_rsp``
     - control
     - ``test_007_set_absolute_volume``
   * - ``test_avrcp_ct_get_element_attrs``
     - ``test_avrcp_tg_get_element_attrs_rsp``
     - control
     - ``test_008_get_element_attrs``
   * - ``test_avrcp_ct_get_play_status``
     - ``test_avrcp_tg_get_play_status_rsp``
     - control
     - ``test_009_get_play_status``
   * - ``test_avrcp_ct_list_app_attrs``
     - ``test_avrcp_tg_list_player_app_setting_attrs_rsp``
     - control
     - ``test_010_list_player_app_setting_attrs``
   * - ``test_avrcp_ct_list_app_vals``
     - ``test_avrcp_tg_list_player_app_setting_vals_rsp``
     - control
     - ``test_011_list_player_app_setting_vals``
   * - ``test_avrcp_ct_get_app_curr``
     - ``test_avrcp_tg_get_curr_player_app_setting_val_rsp``
     - control
     - ``test_012_get_curr_player_app_setting_val``
   * - ``test_avrcp_ct_set_app_val``
     - ``test_avrcp_tg_set_player_app_setting_val_rsp``
     - control
     - ``test_013_set_player_app_setting_val``
   * - ``test_avrcp_ct_get_app_attr_text``
     - ``test_avrcp_tg_get_player_app_setting_attr_text_rsp``
     - control
     - ``test_014_get_player_app_setting_attr_text``
   * - ``test_avrcp_ct_get_app_val_text``
     - ``test_avrcp_tg_get_player_app_setting_val_text_rsp``
     - control
     - ``test_015_get_player_app_setting_val_text``
   * - ``test_avrcp_ct_inform_displayable_char``
     - ``test_avrcp_tg_inform_displayable_char_rsp``
     - control
     - ``test_016_inform_displayable_char_set``
   * - ``test_avrcp_ct_inform_batt``
     - ``test_avrcp_tg_inform_batt_rsp``
     - control
     - ``test_017_inform_batt_status_of_ct``
   * - ``test_avrcp_ct_set_addressed_player``
     - ``test_avrcp_tg_set_addressed_player_rsp``
     - control
     - ``test_018_set_addressed_player``
   * - ``test_avrcp_set_browsed_player``
     - ``test_avrcp_tg_set_browsed_player_rsp``
     - browsing
     - ``test_001_set_browsed_player``
   * - ``test_avrcp_ct_get_folder_items``
     - ``test_avrcp_tg_get_folder_items_rsp``
     - browsing
     - ``test_002_get_folder_items``
   * - ``test_avrcp_ct_change_path``
     - ``test_avrcp_tg_change_path_rsp``
     - browsing
     - ``test_003_change_path``
   * - ``test_avrcp_ct_get_item_attrs``
     - ``test_avrcp_tg_get_item_attributes_rsp``
     - browsing
     - ``test_004_get_item_attrs``
   * - ``test_avrcp_ct_get_total_number_of_items``
     - ``test_avrcp_tg_get_total_num_of_items_rsp``
     - browsing
     - ``test_005_get_total_number_of_items``
   * - ``test_avrcp_ct_search``
     - ``test_avrcp_tg_search_rsp``
     - browsing
     - ``test_006_search``
   * - ``test_avrcp_ct_play_item``
     - ``test_avrcp_tg_play_item_rsp``
     - browsing
     - ``test_007_play_item``
   * - ``test_avrcp_ct_add_to_now_playing``
     - ``test_avrcp_tg_add_to_now_playing_rsp``
     - browsing
     - ``test_008_add_to_now_playing``
   * - Not present
     - ``test_avrcp_tg_send_browsing_general_reject``
     - browsing
     - ``test_009_general_reject``
   * - Not present
     - ``test_avrcp_tg_test_invalid_status_buf``
     - robustness
     - ``test_001_invalid_status`` and ``test_002_empty_rsp``

Control test 006 and robustness tests 003 through 013 provide additional
coverage beyond these source IDs. This traceability records represented
scenarios, not assertion-for-assertion equivalence: shell commands and log
matching are replaced by API/callback assertions. It does not establish
equivalent RF behavior, shell output, timing, or physical-board behavior.
AVRCP Cover Art is outside this suite.
