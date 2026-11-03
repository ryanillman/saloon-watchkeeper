#pragma once
#include "esp_err.h"

// Mounts a FAT SD card at /sdcard over SPI2 with chip select held low by the
// board (the card is the only device on the bus).
esp_err_t sd_spi_mount(int mosi, int sck, int miso);
