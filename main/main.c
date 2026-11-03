#include <stdio.h>
#include <string.h>
#include "app.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "board.h"
#include "board_probe.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "sdkconfig.h"

static const char *TAG = "main";
static const board_t *board;

// Signal K snapshots can be large; keep JSON parsing out of scarce internal RAM.
static void *json_alloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

static uint32_t tick_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

#if CONFIG_BI_DIAG_COLORBARS
static void color_bars(lv_display_t *d) {
  static const uint32_t c[] = {0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000};
  lv_obj_t *scr = lv_display_get_screen_active(d);
  int32_t w = lv_display_get_horizontal_resolution(d) / 8;
  for (int i = 0; i < 8; i++) {
    lv_obj_t *b = lv_obj_create(scr);
    lv_obj_remove_style_all(b);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(c[i]), 0);
    lv_obj_set_size(b, w, LV_PCT(100));
    lv_obj_set_pos(b, i * w, 0);
  }
  for (int i = 0; i < 500; i++) {
    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// Writes, reads back, and removes one file of our own. Touches nothing else on the card.
static void sd_selftest(void) {
  const char *path = "/sdcard/.bi_probe", *msg = "boat-instruments sd check";
  FILE *f = fopen(path, "w");
  if (!f) {
    ESP_LOGW(TAG, "SD check: cannot write (no card?)");
    return;
  }
  fputs(msg, f);
  fclose(f);
  char buf[64] = "";
  if ((f = fopen(path, "r"))) {
    fgets(buf, sizeof buf, f);
    fclose(f);
  }
  remove(path);
  ESP_LOGI(TAG, "SD check: %s", strcmp(buf, msg) == 0 ? "OK" : "read back mismatch");
}
#endif

static void tick_cb(lv_timer_t *t) {
  (void)t;
  app_tick();
}

// The only task that calls LVGL.
static void ui_task(void *arg) {
  (void)arg;
  lv_init();
  lv_tick_set_cb(tick_ms);
  lv_display_t *d = board->lvgl_display();
  const cfg_t *cfg = arg;
  board->backlight(cfg->brightness ? cfg->brightness : 100);
#if CONFIG_BI_DIAG_COLORBARS
  color_bars(d);
#endif
  app_set_buzzer(board->buzzer, board->has_buzzer && board->buzzer);
  app_bind();
  app_start_ui(d, board->size_class);
  app_start_network();
  lv_timer_create(tick_cb, 100, NULL);
  for (;;) {
    uint32_t wait = lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(wait < 5 ? 5 : wait > 30 ? 30 : wait));
  }
}

void app_main(void) {
#if CONFIG_BI_DIAG_PROBE
  for (;;) {
    board_probe_run();
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
#endif
  cJSON_InitHooks(&(cJSON_Hooks){.malloc_fn = json_alloc, .free_fn = heap_caps_free});
  board = board_get();
  ESP_LOGI(TAG, "board %s %ux%u", board->id, board->width, board->height);
  ESP_ERROR_CHECK(board->init());
#if CONFIG_BI_DIAG_COLORBARS
  sd_selftest();
#endif
  cfg_report_t rep;
  const cfg_t *cfg = app_init("/sdcard", &rep);
  for (uint8_t i = 0; i < rep.count; i++)
    ESP_LOGW(TAG, "config: %s (line %d): %s", rep.issues[i].key_path, rep.issues[i].line, rep.issues[i].message);
  net_wifi_start(&cfg->wifi);
  xTaskCreatePinnedToCore(ui_task, "ui", 16 * 1024, (void *)cfg, 5, NULL, 1);
}
