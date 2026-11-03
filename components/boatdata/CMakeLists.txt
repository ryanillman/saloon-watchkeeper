include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
bi_component(NAME boatdata
  SRCS src/boatdata.c
  INCLUDE_DIRS include
  REQUIRES platform)
