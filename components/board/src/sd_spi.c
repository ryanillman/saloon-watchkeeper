#include "sd_spi.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"

esp_err_t sd_spi_mount(int mosi, int sck, int miso) {
  spi_bus_config_t bus = {
    .mosi_io_num = mosi, .miso_io_num = miso, .sclk_io_num = sck,
    .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 4000,
  };
  esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SDSPI_DEFAULT_DMA);
  if (err) return err;
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI2_HOST;
  sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev.gpio_cs = GPIO_NUM_NC;
  dev.host_id = SPI2_HOST;
  esp_vfs_fat_sdmmc_mount_config_t mc = {.format_if_mount_failed = false, .max_files = 4};
  sdmmc_card_t *card;
  return esp_vfs_fat_sdspi_mount("/sdcard", &host, &dev, &mc, &card);
}
