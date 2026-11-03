include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
if(ESP_PLATFORM)
  bi_component(NAME net
    SRCS src/net_esp.c
    INCLUDE_DIRS include
    REQUIRES config
    ESP_REQUIRES esp_wifi esp_netif esp_event nvs_flash)
endif()
