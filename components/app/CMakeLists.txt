include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
bi_component(NAME app
  SRCS src/app.c src/app_cmd.c src/app_can.c src/app_electrical.c
  INCLUDE_DIRS include
  REQUIRES roles ui alerts boatdata config source_sk source_can electrical n2k_bus n2k platform net lvgl)
