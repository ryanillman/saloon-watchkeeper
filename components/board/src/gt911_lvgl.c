#include "gt911_lvgl.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_timer.h"

static esp_lcd_touch_handle_t tp;
static esp_lcd_panel_io_handle_t tp_io;   // TEMP diagnostics

static void read_cb(lv_indev_t *i, lv_indev_data_t *data) {
  (void)i;
  esp_lcd_touch_point_data_t pt;
  uint8_t n = 0;
  static int last_status = -1;   // TEMP diagnostics: raw GT911 status register 0x814E
  uint8_t st = 0;
  esp_err_t se = esp_lcd_panel_io_rx_param(tp_io, 0x814E, &st, 1);
  if (se || st != last_status) {
    ESP_LOGI("gt911", "status 0x814E = 0x%02X (%s)", st, esp_err_to_name(se));
    last_status = st;
  }
  esp_err_t rd = esp_lcd_touch_read_data(tp);
  esp_err_t gd = esp_lcd_touch_get_data(tp, &pt, &n, 1);
  static int64_t last;   // TEMP diagnostics
  if ((rd || n) && esp_timer_get_time() - last > 100000) {
    last = esp_timer_get_time();
    ESP_LOGI("gt911", "read=%s get=%s n=%u x=%u y=%u", esp_err_to_name(rd), esp_err_to_name(gd), n, pt.x, pt.y);
  }
  if (gd == ESP_OK && n > 0) {
    data->point.x = pt.x;
    data->point.y = pt.y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

lv_indev_t *gt911_lvgl_indev(i2c_master_bus_handle_t bus, uint16_t w, uint16_t h, int int_gpio) {
  esp_lcd_panel_io_handle_t io;
  // The GT911 latches 0x5D or 0x14 at reset depending on its INT line; use whichever answers.
  esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
  io_cfg.scl_speed_hz = 400000;
  if (i2c_master_probe(bus, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, 50) != ESP_OK) {
    if (i2c_master_probe(bus, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP, 50) != ESP_OK) {
      ESP_LOGE("gt911", "no GT911 at 0x5D or 0x14");
      return NULL;
    }
    io_cfg.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
  }
  ESP_LOGI("gt911", "GT911 at 0x%02X", (unsigned)io_cfg.dev_addr);
  if (esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io) != ESP_OK) return NULL;
  tp_io = io;
  esp_lcd_touch_config_t cfg = {
    .x_max = w, .y_max = h, .rst_gpio_num = GPIO_NUM_NC, .int_gpio_num = int_gpio,
  };
  if (esp_lcd_touch_new_i2c_gt911(io, &cfg, &tp) != ESP_OK) {
    ESP_LOGE("gt911", "init failed");
    return NULL;
  }
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, read_cb);
  return indev;
}
