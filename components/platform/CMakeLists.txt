include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
bi_component(NAME platform
  INCLUDE_DIRS include
  LINUX_SRCS src/platform_linux.c
  ESP_SRCS src/platform_esp.c
  LINUX_LIBS pthread)
