#pragma once
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

// LVGL display drawing straight into an esp_lcd RGB panel's two PSRAM framebuffers
// (direct mode). Each flush swaps buffers on the next vsync, so frames never tear.
lv_display_t *rgb_lvgl_display(esp_lcd_panel_handle_t panel, uint16_t w, uint16_t h);
