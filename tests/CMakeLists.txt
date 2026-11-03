# One executable per test_*.c, linked to Unity and the components it covers.
function(bi_test name)
  add_executable(${name} ${name}.c)
  target_link_libraries(${name} PRIVATE unity ${ARGN})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

bi_test(test_platform platform)
bi_test(test_ui_nav ui)
bi_test(test_ui_scale ui)
bi_test(test_ui_widgets ui)
bi_test(test_sim_boards sim_boards)
bi_test(test_boatdata boatdata)
bi_test(test_config config json)
bi_test(test_units units)
bi_test(test_sk_parse source_sk_core)
bi_test(test_sk_client source_sk_core)
bi_test(test_role_bind roles)
bi_test(test_engine_state roles)
bi_test(test_power_calc roles)
bi_test(test_tanks roles ui)
bi_test(test_alerts alerts platform)
bi_test(test_ui_alerts ui alerts)
bi_test(test_ui_quick ui alerts)
bi_test(test_app_palette app boatdata config platform)

add_test(NAME snapshot_smoke
  COMMAND ${CMAKE_COMMAND} -DSIM=$<TARGET_FILE:sim> -DOUT=${CMAKE_BINARY_DIR}/snap
          -P ${CMAKE_CURRENT_SOURCE_DIR}/check_snapshots.cmake)
bi_test(test_ui_shell_gesture ui)

add_test(NAME bi_component_idf
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_bi_component_idf.cmake)
