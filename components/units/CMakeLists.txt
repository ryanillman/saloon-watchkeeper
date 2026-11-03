include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
bi_component(NAME units
  SRCS src/units.c
  INCLUDE_DIRS include
  REQUIRES config)
