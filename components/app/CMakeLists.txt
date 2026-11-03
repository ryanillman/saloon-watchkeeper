include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
bi_component(NAME app
  SRCS src/app.c
  INCLUDE_DIRS include
  REQUIRES roles ui alerts boatdata config source_sk platform net lvgl)
