// Waveshare ESP32-S3-Touch-LCD-7B: 1024x600 RGB (ST7262, no init), GT911,
// CH32V003-style I/O helper at 0x24. Pins and timings: docs/hardware/boards.md.
// Not yet run on hardware.
#include "sdkconfig.h"
#if CONFIG_BI_BOARD_LCD7B
#include "board.h"
#include "ch32v003_exio.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gt911_lvgl.h"
#include "rgb_lvgl.h"
#include "sd_spi.h"

#define W 1024
#define H 600

// I/O helper pins
#define EXIO_TP_RST  1
#define EXIO_BL      2   // backlight enable
#define EXIO_LCD_RST 3
#define EXIO_SD_CS   4
#define EXIO_USB_SEL 5   // low = native USB, high = CAN
#define EXIO_VDD_EN  6   // panel VCOM supply

#define PIN_TP_INT 4

static const char *TAG = "lcd7b";
static i2c_master_bus_handle_t i2c;
static esp_lcd_panel_handle_t panel;

static esp_err_t init(void) {
  i2c_master_bus_config_t bc = {
    .i2c_port = I2C_NUM_0, .sda_io_num = 8, .scl_io_num = 9,
    .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &i2c));

  // All helper pins outputs (as Waveshare's demo does). Panel supply on, SD
  // deselected, USB selected, backlight off, panel and touch in reset.
  ESP_ERROR_CHECK(ch32v003_init(i2c, 0xFF, 1u << EXIO_VDD_EN | 1u << EXIO_SD_CS));

  // GT911 picks 0x5D when INT is low as reset is released.
  gpio_set_direction(PIN_TP_INT, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_TP_INT, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  ch32v003_set(EXIO_LCD_RST, 1);
  ch32v003_set(EXIO_TP_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(200));
  gpio_reset_pin(PIN_TP_INT);

  esp_lcd_rgb_panel_config_t pc = {
    .clk_src = LCD_CLK_SRC_DEFAULT,
    .timings = {
      .pclk_hz = 30 * 1000 * 1000, .h_res = W, .v_res = H,
      .hsync_pulse_width = 162, .hsync_back_porch = 152, .hsync_front_porch = 48,
      .vsync_pulse_width = 45, .vsync_back_porch = 13, .vsync_front_porch = 3,
      .flags.pclk_active_neg = 1,
    },
    .data_width = 16,
    .bits_per_pixel = 16,
    .num_fbs = 2,
    .bounce_buffer_size_px = W * 10,
    .hsync_gpio_num = 46, .vsync_gpio_num = 3, .de_gpio_num = 5, .pclk_gpio_num = 7,
    .disp_gpio_num = GPIO_NUM_NC,
    // B3-B7, G2-G7, R3-R7 (same wiring as the 800x480 board)
    .data_gpio_nums = {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40},
    .flags.fb_in_psram = 1,
  };
  ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&pc, &panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

  ch32v003_set(EXIO_SD_CS, 0);   // the card is the only SPI device; keep it selected
  esp_err_t sd = sd_spi_mount(11, 12, 13);
  if (sd != ESP_OK) ESP_LOGW(TAG, "SD card not mounted: %s", esp_err_to_name(sd));
  else ESP_LOGI(TAG, "SD card mounted at /sdcard");
  return ESP_OK;
}

static lv_display_t *display(void) {
  lv_display_t *d = rgb_lvgl_display(panel, W, H);
  if (!gt911_lvgl_indev(i2c, W, H, PIN_TP_INT)) ESP_LOGE(TAG, "touch unavailable");
  return d;
}

// Enable pin for on/off; the helper's PWM is inverted (0 = full) and Waveshare
// caps it at 97% so the panel never goes fully dark while enabled.
static esp_err_t backlight(uint8_t percent) {
  if (percent == 0) return ch32v003_set(EXIO_BL, 0);
  if (percent > 100) percent = 100;
  uint8_t off_pct = (uint8_t)(100 - percent);
  if (off_pct > 97) off_pct = 97;
  esp_err_t err = ch32v003_pwm((uint8_t)(off_pct * 255 / 100));
  if (err) return err;
  return ch32v003_set(EXIO_BL, 1);
}

static const board_t lcd7b = {
  .id = "lcd7b", .width = W, .height = H, .size_class = SIZE_WIDE, .has_buzzer = false,
  .init = init, .lvgl_display = display, .backlight = backlight, .buzzer = NULL,
};

const board_t *board_get(void) { return &lcd7b; }
#endif
