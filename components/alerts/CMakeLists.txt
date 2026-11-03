include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
bi_component(NAME alerts
  SRCS src/alerts.c
  INCLUDE_DIRS include
  REQUIRES platform)
