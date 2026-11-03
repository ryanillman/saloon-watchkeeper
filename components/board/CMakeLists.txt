include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
if(ESP_PLATFORM)
  bi_component(NAME board
    SRCS src/board_probe.c src/board_lcd7.c src/board_square4.c src/board_lcd7b.c src/rgb_lvgl.c src/gt911_lvgl.c src/sd_spi.c
         drivers/ch422g.c drivers/ch32v003_exio.c
    INCLUDE_DIRS include drivers
    REQUIRES ui
    ESP_REQUIRES esp_driver_i2c esp_driver_gpio esp_driver_spi esp_driver_sdspi esp_driver_sdmmc esp_lcd
                 esp_psram spi_flash fatfs)
endif()
