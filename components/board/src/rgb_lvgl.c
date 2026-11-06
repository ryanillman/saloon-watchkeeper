#include "rgb_lvgl.h"
#include "esp_lcd_panel_rgb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Given when the driver starts reading the buffer passed to draw_bitmap. With bounce
// buffers that happens before VSYNC, so waiting for VSYNC would let LVGL draw into
// a buffer the driver is still copying out.
static SemaphoreHandle_t swap_sem;

static bool IRAM_ATTR on_frame_buf_complete(esp_lcd_panel_handle_t p, const esp_lcd_rgb_panel_event_data_t *e, void *ctx) {
  (void)p; (void)e; (void)ctx;
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(swap_sem, &woken);
  return woken == pdTRUE;
}

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a;
  if (lv_display_flush_is_last(d)) {
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(d);
    // px is one of the panel's own framebuffers, so this only swaps the scan-out buffer.
    esp_lcd_panel_draw_bitmap(panel, 0, 0, lv_display_get_horizontal_resolution(d),
                              lv_display_get_vertical_resolution(d), px);
    xSemaphoreTake(swap_sem, 0);               // drop an event from before this swap
    xSemaphoreTake(swap_sem, pdMS_TO_TICKS(100));
  }
  lv_display_flush_ready(d);
}

lv_display_t *rgb_lvgl_display(esp_lcd_panel_handle_t panel, uint16_t w, uint16_t h) {
  swap_sem = xSemaphoreCreateBinary();
  esp_lcd_rgb_panel_event_callbacks_t cbs = {.on_frame_buf_complete = on_frame_buf_complete};
  esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, NULL);

  void *fb0, *fb1;
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &fb0, &fb1));
  lv_display_t *d = lv_display_create(w, h);
  lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(d, fb0, fb1, (uint32_t)w * h * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_user_data(d, panel);
  lv_display_set_flush_cb(d, flush);
  return d;
}

void rgb_lvgl_reset(esp_lcd_panel_handle_t panel) {
  esp_lcd_rgb_panel_restart(panel);
  xSemaphoreTake(swap_sem, 0);   // an event from before the restart must not release the next flush early
  lv_obj_invalidate(lv_screen_active());
}
