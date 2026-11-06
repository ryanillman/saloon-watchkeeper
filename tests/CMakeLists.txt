# One executable per test_*.c, linked to Unity and the components it covers.
function(bi_test name)
  add_executable(${name} ${name}.c)
  target_link_libraries(${name} PRIVATE unity ${ARGN})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

bi_test(test_platform platform)
bi_test(test_ui_nav ui)
bi_test(test_ui_scale ui)
bi_test(test_ui_clock ui)
bi_test(test_ui_shell_strip ui alerts)
bi_test(test_ui_widgets ui)
bi_test(test_ui_swing ui)
bi_test(test_ui_theme ui)
bi_test(test_ui_rose ui)
bi_test(test_sim_boards sim_boards)
bi_test(test_boatdata boatdata)
bi_test(test_history history boatdata platform)
bi_test(test_config config json)
bi_test(test_units units)
bi_test(test_sk_parse source_sk_core)
bi_test(test_sk_client source_sk_core)
bi_test(test_sk_cmd source_sk_core platform)
bi_test(test_sk_access source_sk_core)
bi_test(test_sk_history source_sk_core history boatdata platform)
bi_test(test_role_bind roles)
bi_test(test_anchor_profile roles)
bi_test(test_anchor_state roles alerts boatdata history platform)
bi_test(test_anchor_record roles platform)
bi_test(test_anchor_watch roles alerts boatdata history platform)
bi_test(test_anchor_page roles ui alerts boatdata history platform)
bi_test(test_nav_state roles alerts boatdata history platform)
bi_test(test_nav_page roles ui alerts boatdata history platform)
bi_test(test_emergency roles platform)
bi_test(test_engine_state roles)
bi_test(test_depth_state roles)
bi_test(test_power_calc roles)
bi_test(test_power_topo roles)
bi_test(test_tanks roles ui)
bi_test(test_glance_state roles alerts boatdata)
bi_test(test_glance_tiles roles alerts boatdata history platform)
bi_test(test_ui_glance roles ui alerts history)
bi_test(test_alerts alerts platform)
bi_test(test_ui_alerts ui alerts)
bi_test(test_ui_setup ui alerts config)
bi_test(test_ui_quick ui alerts)
bi_test(test_app_palette app boatdata config platform)
bi_test(test_app_apply app boatdata config platform alerts ui)
bi_test(test_app_anchor app boatdata config platform alerts ui)
bi_test(test_app_emergency app boatdata config platform alerts ui roles source_sk_core)
bi_test(test_app_cmd app boatdata config platform alerts ui)

add_test(NAME snapshot_smoke
  COMMAND ${CMAKE_COMMAND} -DSIM=$<TARGET_FILE:sim> -DOUT=${CMAKE_BINARY_DIR}/snap
          -P ${CMAKE_CURRENT_SOURCE_DIR}/check_snapshots.cmake)
bi_test(test_ui_shell_gesture ui)

add_test(NAME bi_component_idf
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_bi_component_idf.cmake)

# The sk-inject boat (tools/sk-inject): pure-Python tests, no Signal K server needed. The system
# Python, as tools/scenario.sh uses: the IDF virtualenv may be first on PATH.
add_test(NAME sk_inject_py
  COMMAND /usr/bin/python3 -W error -I -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/sk_inject)

# The JSON Schema lists the same roles the firmware accepts.
add_test(NAME config_schema_roles
  COMMAND /usr/bin/python3 -I ${CMAKE_CURRENT_SOURCE_DIR}/check_schema_roles.py ${CMAKE_CURRENT_SOURCE_DIR}/..)
