include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
bi_component(NAME ui
  SRCS src/ui_nav.c src/ui_theme.c src/ui_shell.c src/ui_placeholder.c src/ui_scale.c src/ui_clock.c src/ui_gauge.c src/ui_alerts.c src/ui_quick.c src/ui_flow.c src/ui_setup.c src/ui_swing.c
  INCLUDE_DIRS include
  REQUIRES lvgl alerts platform config
  LINUX_LIBS m)
