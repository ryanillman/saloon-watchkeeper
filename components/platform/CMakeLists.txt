include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
bi_component(NAME platform
  SRCS src/platform_time.c src/platform_file.c
  INCLUDE_DIRS include
  LINUX_SRCS src/platform_linux.c
  ESP_SRCS src/platform_esp.c
  ESP_REQUIRES esp_timer
  LINUX_LIBS pthread)
