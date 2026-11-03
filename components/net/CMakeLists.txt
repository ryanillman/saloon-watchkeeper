include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
bi_component(NAME net
  INCLUDE_DIRS include
  REQUIRES config
  ESP_SRCS src/net_esp.c
  LINUX_SRCS src/net_linux.c
  ESP_REQUIRES esp_wifi esp_netif esp_event nvs_flash)
