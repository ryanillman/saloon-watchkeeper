include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
bi_component(NAME app
  SRCS src/app.c
  INCLUDE_DIRS include
  REQUIRES roles ui boatdata config source_sk platform lvgl)
