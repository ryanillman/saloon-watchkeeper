# One executable per test_*.c, linked to Unity and the components it covers.
function(bi_test name)
  add_executable(${name} ${name}.c)
  target_link_libraries(${name} PRIVATE unity ${ARGN})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

bi_test(test_platform platform)
bi_test(test_ui_nav ui)
bi_test(test_sim_boards sim_boards)
bi_test(test_boatdata boatdata)

add_test(NAME snapshot_smoke
  COMMAND ${CMAKE_COMMAND} -DSIM=$<TARGET_FILE:sim> -DOUT=${CMAKE_BINARY_DIR}/snap
          -P ${CMAKE_CURRENT_SOURCE_DIR}/check_snapshots.cmake)
bi_test(test_ui_shell_gesture ui)

add_test(NAME bi_component_idf
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_bi_component_idf.cmake)
