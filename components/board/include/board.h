#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"
#include "ui_theme.h"

typedef struct {
  const char *id;               // "square4" | "lcd7" | "lcd7b"
  uint16_t width, height;
  size_class_t size_class;
  bool has_buzzer;
  bool backlight_dims;                         // false: backlight() is on/off only (any percent above 0 is on)
  esp_err_t (*init)(void);                     // panel, touch, expander, SD at /sdcard
  lv_display_t *(*lvgl_display)(void);         // after init and lv_init
  esp_err_t (*backlight)(uint8_t percent);     // 0 = off
  void (*buzzer)(bool on);                     // NULL when has_buzzer is false
  void (*reset_display)(void);                 // restarts the RGB scan from the top of the frame; call from the LVGL task
} board_t;

// CAN (NMEA 2000) wiring of this board. board_can_pins returns ESP_ERR_NOT_SUPPORTED when the
// board has no transceiver. board_can_enable switches the transceiver in: on the 7" boards it
// drives EXIO5 high, which DISABLES NATIVE USB (logs and flashing then go through the UART port).
// Call it after board->init(); the default after init is USB.
esp_err_t board_can_pins(int *tx_gpio, int *rx_gpio);
esp_err_t board_can_enable(void);

// The board this image was built for (CONFIG_BI_BOARD_*).
const board_t *board_get(void);
