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
  esp_err_t (*init)(void);                     // panel, touch, expander, SD at /sdcard
  lv_display_t *(*lvgl_display)(void);         // after init and lv_init
  esp_err_t (*backlight)(uint8_t percent);     // 0 = off
  void (*buzzer)(bool on);                     // NULL when has_buzzer is false
} board_t;

// The board this image was built for (CONFIG_BI_BOARD_*).
const board_t *board_get(void);
