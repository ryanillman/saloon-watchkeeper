// The auto palette holds its last mode; config saves go through a worker after a debounce.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "app.h"
#include "boatdata.h"
#include "config.h"
#include "platform.h"
#include "ui_quick.h"
#include "ui_shell.h"
#include "unity.h"

static uint8_t buf[800 * 48 * 2];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  lv_display_flush_ready(d);
}
static uint32_t tick_ms(void) { return (uint32_t)platform_millis(); }
static lv_display_t *disp;
static char dir[64];

static void start(const char *d, const char *palette) {
  cfg_report_t rep;
  cfg_t *cfg = (cfg_t *)app_init(d, &rep);
  snprintf(cfg->palette, sizeof cfg->palette, "%s", palette);
  app_bind();
  app_start_ui(disp, SIZE_WIDE);
}

void setUp(void) {}
void tearDown(void) {}

static bool is_night(void) { return lv_color_eq(ui_theme()->fg, ui_theme_colors(PALETTE_NIGHT)->fg); }

static void test_auto_holds_last_mode(void) {
  start(NULL, "auto");
  TEST_ASSERT_FALSE(is_night());   // no mode received since boot: day
  bd_id_t id = bd_intern("environment.mode");
  bd_set_text(id, "night", 1, platform_millis());
  app_tick();
  TEST_ASSERT_TRUE(is_night());
  bd_invalidate_source(1);
  app_tick();
  TEST_ASSERT_TRUE(is_night());   // held
  bd_set_invalid(id);
  app_tick();
  TEST_ASSERT_TRUE(is_night());
  start(NULL, "auto");   // a fresh boot has nothing to hold
  TEST_ASSERT_FALSE(is_night());
}

static char *slurp(const char *name) {
  char path[128];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  FILE *f = fopen(path, "r");
  if (!f) return NULL;
  static char text[8192];
  size_t n = fread(text, 1, sizeof text - 1, f);
  text[n] = 0;
  fclose(f);
  return text;
}

static bool wait_for(const char *needle, int ms) {
  for (int t = 0; t < ms; t += 50) {
    char *s = slurp("config.json");
    if (s && strstr(s, needle)) return true;
    usleep(50 * 1000);
  }
  return false;
}

static void test_save_is_debounced_and_off_the_ui_task(void) {
  snprintf(dir, sizeof dir, "/tmp/bi-test-app-%d", (int)getpid());
  mkdir(dir, 0755);
  start(dir, "day");
  lv_obj_send_event(ui_shell_strip_tap(), LV_EVENT_CLICKED, NULL);
  lv_obj_send_event(ui_quick_part(UI_QUICK_NIGHT), LV_EVENT_CLICKED, NULL);
  lv_obj_send_event(ui_quick_part(UI_QUICK_DUSK), LV_EVENT_CLICKED, NULL);   // the last change wins
  lv_obj_send_event(ui_quick_part(UI_QUICK_SLIDER), LV_EVENT_VALUE_CHANGED, NULL);
  app_tick();
  TEST_ASSERT_NULL(slurp("config.json"));   // not yet: 2 s debounce
  usleep(2100 * 1000);
  app_tick();   // hands the copy over and returns
  TEST_ASSERT_NULL(slurp("config.json"));   // the write happens on the worker, not here
  TEST_ASSERT_TRUE(wait_for("\"dusk\"", 3000));
  TEST_ASSERT_NULL(strstr(slurp("config.json"), "\"night\""));
  char cmd[128];
  snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
  (void)!system(cmd);
}

int main(void) {
  lv_init();
  lv_tick_set_cb(tick_ms);
  disp = lv_display_create(800, 480);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  UNITY_BEGIN();
  RUN_TEST(test_auto_holds_last_mode);
  RUN_TEST(test_save_is_debounced_and_off_the_ui_task);
  return UNITY_END();
}
