include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
bi_component(NAME history
  SRCS src/history.c
  INCLUDE_DIRS include
  REQUIRES boatdata platform)
