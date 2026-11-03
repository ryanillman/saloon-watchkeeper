include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
if(ESP_PLATFORM)
  bi_component(NAME board
    SRCS src/board_probe.c
    INCLUDE_DIRS include
    ESP_REQUIRES esp_driver_i2c esp_psram spi_flash)
endif()
