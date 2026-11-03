include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
bi_component(NAME config
  SRCS src/config.c
  INCLUDE_DIRS include
  REQUIRES json platform)
