// Waveshare ESP32-S3-Touch-LCD-4 (v4): 480x480 ST7701, GT911, CH32V003 I/O helper at 0x24.
// Pins, timings, and the panel init sequence: docs/hardware/boards.md.
#include "sdkconfig.h"
#if CONFIG_BI_BOARD_SQUARE4
#include "board.h"
#include "ch32v003_exio.h"
#include "driver/i2c_master.h"
#include "driver/sdmmc_host.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gt911_lvgl.h"
#include "rgb_lvgl.h"

#define W 480
#define H 480

// CH32V003 pins
#define EXIO_TP_RST  1
#define EXIO_TP_INT  2   // GT911 INT runs through the helper (V4.0 schematic)
#define EXIO_SD_CS   4   // SD D3 (V4.0 schematic)
#define EXIO_LCD_RST 3
#define EXIO_SYS_EN  5   // panel and peripheral power
#define EXIO_BEE_EN  6   // buzzer

static const char *TAG = "square4";
static i2c_master_bus_handle_t i2c;
static esp_lcd_panel_handle_t panel;

// ST7701 vendor init for this panel, from Waveshare's esp32_s3_touch_lcd_4 BSP v3.0.0 (Apache-2.0).
static const st7701_lcd_init_cmd_t init_cmds[] = {
  {0x11, (uint8_t[]){0x00}, 0, 120},
  {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
  {0xC0, (uint8_t[]){0x3B, 0x00}, 2, 0},
  {0xC1, (uint8_t[]){0x0D, 0x02}, 2, 0},
  {0xC2, (uint8_t[]){0x21, 0x08}, 2, 0},
  {0xCD, (uint8_t[]){0x08}, 1, 0},
  {0xB0, (uint8_t[]){0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18}, 16, 0},
  {0xB1, (uint8_t[]){0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18}, 16, 0},
  {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
  {0xB0, (uint8_t[]){0x60}, 1, 0},
  {0xB1, (uint8_t[]){0x30}, 1, 0},
  {0xB2, (uint8_t[]){0x87}, 1, 0},
  {0xB3, (uint8_t[]){0x80}, 1, 0},
  {0xB5, (uint8_t[]){0x49}, 1, 0},
  {0xB7, (uint8_t[]){0x85}, 1, 0},
  {0xB8, (uint8_t[]){0x21}, 1, 0},
  {0xC1, (uint8_t[]){0x78}, 1, 0},
  {0xC2, (uint8_t[]){0x78}, 1, 20},
  {0xE0, (uint8_t[]){0x00, 0x1B, 0x02}, 3, 0},
  {0xE1, (uint8_t[]){0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44}, 11, 0},
  {0xE2, (uint8_t[]){0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00}, 12, 0},
  {0xE3, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
  {0xE4, (uint8_t[]){0x44, 0x44}, 2, 0},
  {0xE5, (uint8_t[]){0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0, 0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0}, 16, 0},
  {0xE6, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
  {0xE7, (uint8_t[]){0x44, 0x44}, 2, 0},
  {0xE8, (uint8_t[]){0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0, 0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0}, 16, 0},
  {0xEB, (uint8_t[]){0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40}, 7, 0},
  {0xEC, (uint8_t[]){0x3C, 0x00}, 2, 0},
  {0xED, (uint8_t[]){0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA}, 16, 0},
  {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
  {0x36, (uint8_t[]){0x00}, 1, 0},
  {0x3A, (uint8_t[]){0x66}, 1, 0},
  {0x21, (uint8_t[]){0x00}, 0, 120},
  {0x29, (uint8_t[]){0x00}, 0, 0},
};

static esp_err_t mount_sd(void) {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.clk = 2;
  slot.cmd = 1;
  slot.d0 = 4;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  esp_vfs_fat_sdmmc_mount_config_t mc = {.format_if_mount_failed = false, .max_files = 4};
  sdmmc_card_t *card;
  return esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mc, &card);
}

static esp_err_t init(void) {
  i2c_master_bus_config_t bc = {
    .i2c_port = I2C_NUM_0, .sda_io_num = 15, .scl_io_num = 7,
    .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &i2c));

  // Same power-up as Waveshare's WS_CH32_IO library: every helper pin an output,
  // all low, then power, panel reset, and touch reset high. EXIO2 (GT911 INT)
  // stays low, so the GT911 starts in its normal mode at 0x5D.
  // EXIO4 (SD D3/CS) is kept high so the card stays in SD mode.
  ESP_ERROR_CHECK(ch32v003_init(i2c, 0xFF, 0));
  vTaskDelay(pdMS_TO_TICKS(200));
  ch32v003_set(EXIO_SD_CS, 1);
  ch32v003_set(EXIO_SYS_EN, 1);
  ch32v003_set(EXIO_LCD_RST, 1);
  ch32v003_set(EXIO_TP_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(200));

  // ST7701 vendor init over 3-wire SPI; the lines are released afterwards for the SD card.
  spi_line_config_t lines = {
    .cs_io_type = IO_TYPE_GPIO, .cs_gpio_num = 42,
    .scl_io_type = IO_TYPE_GPIO, .scl_gpio_num = 2,
    .sda_io_type = IO_TYPE_GPIO, .sda_gpio_num = 1,
  };
  esp_lcd_panel_io_3wire_spi_config_t io_cfg = ST7701_PANEL_IO_3WIRE_SPI_CONFIG(lines, 0);
  esp_lcd_panel_io_handle_t io;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_3wire_spi(&io_cfg, &io));

  esp_lcd_rgb_panel_config_t rgb = {
    .clk_src = LCD_CLK_SRC_DEFAULT,
    .timings = ST7701_480_480_PANEL_60HZ_RGB_TIMING(),
    .data_width = 16,
    .bits_per_pixel = 16,
    .num_fbs = 2,
    .bounce_buffer_size_px = W * 10,
    .psram_trans_align = 64,
    .hsync_gpio_num = 38, .vsync_gpio_num = 39, .de_gpio_num = 40, .pclk_gpio_num = 41,
    .disp_gpio_num = GPIO_NUM_NC,
    // B1-B5, G0-G5, R1-R5
    .data_gpio_nums = {5, 45, 48, 47, 21, 14, 13, 12, 11, 10, 9, 46, 3, 8, 18, 17},
    .flags.fb_in_psram = 1,
  };
  rgb.timings.h_res = W;
  rgb.timings.v_res = H;
  st7701_vendor_config_t vendor = {
    .rgb_config = &rgb,
    .init_cmds = init_cmds,
    .init_cmds_size = sizeof init_cmds / sizeof init_cmds[0],
    .flags = {.auto_del_panel_io = 1},   // frees GPIO1/2 for SD; cannot be combined with mirror_by_cmd
  };
  esp_lcd_panel_dev_config_t dev = {
    .reset_gpio_num = GPIO_NUM_NC,   // reset is on the CH32V003, done above
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
    .bits_per_pixel = 18,
    .vendor_config = &vendor,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(io, &dev, &panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
  // Display-on (0x29) is the last command of init_cmds; the SPI io is gone by now.

  esp_err_t sd = mount_sd();
  if (sd != ESP_OK) ESP_LOGW(TAG, "SD card not mounted: %s", esp_err_to_name(sd));
  else ESP_LOGI(TAG, "SD card mounted at /sdcard");
  return ESP_OK;
}

static lv_display_t *display(void) {
  lv_display_t *d = rgb_lvgl_display(panel, W, H);
  if (!gt911_lvgl_indev(i2c, W, H, GPIO_NUM_NC)) ESP_LOGE(TAG, "touch unavailable");
  return d;
}

// The helper's PWM is inverted: 0 is full brightness, 255 is off.
static esp_err_t backlight(uint8_t percent) {
  if (percent > 100) percent = 100;
  return ch32v003_pwm((uint8_t)(255 - percent * 255 / 100));
}

static void buzzer(bool on) { ch32v003_set(EXIO_BEE_EN, on); }

static const board_t square4 = {
  .id = "square4", .width = W, .height = H, .size_class = SIZE_SQUARE, .has_buzzer = true,
  .init = init, .lvgl_display = display, .backlight = backlight, .buzzer = buzzer,
};

const board_t *board_get(void) { return &square4; }
#endif
