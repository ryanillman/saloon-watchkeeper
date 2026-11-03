include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
bi_component(NAME ui
  SRCS src/ui_nav.c src/ui_theme.c src/ui_shell.c src/ui_placeholder.c src/ui_scale.c src/ui_gauge.c
  INCLUDE_DIRS include
  REQUIRES lvgl
  LINUX_LIBS m)
