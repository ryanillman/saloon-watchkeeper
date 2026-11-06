#pragma once
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

// LVGL display drawing straight into an esp_lcd RGB panel's two PSRAM framebuffers
// (direct mode). Each flush swaps buffers on the next vsync, so frames never tear.
lv_display_t *rgb_lvgl_display(esp_lcd_panel_handle_t panel, uint16_t w, uint16_t h);

// Restarts the panel's DMA at the top of the frame (recovers a scan that lost sync, e.g. the status bar drawn at the
// bottom) and redraws the whole screen. Call from the LVGL task.
void rgb_lvgl_reset(esp_lcd_panel_handle_t panel);
