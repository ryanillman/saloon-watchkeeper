// app_apply_config does only what changed; app_post runs work on the UI task.
#include <string.h>
#include "alerts.h"
#include "app.h"
#include "config.h"
#include "platform.h"
#include "ui_alerts.h"
#include "ui_shell.h"
#include "unity.h"

static uint8_t buf[800 * 48 * 2];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  lv_display_flush_ready(d);
}
static uint32_t tick_ms(void) { return (uint32_t)platform_millis(); }
static lv_display_t *disp;
static cfg_t next;

void setUp(void) {
  cfg_report_t rep;
  app_init(NULL, &rep);
  app_bind();
  app_start_ui(disp, SIZE_WIDE);
  next = *app_config();
}
void tearDown(void) {}

static void test_apply_identical_is_none(void) {
  TEST_ASSERT_EQUAL_UINT(APPLY_NONE, app_apply_config(&next));
}

static void test_apply_palette_only_display(void) {
  strcpy(next.palette, "night");
  TEST_ASSERT_EQUAL_UINT(APPLY_DISPLAY, app_apply_config(&next));
  TEST_ASSERT_EQUAL_STRING("night", app_config()->palette);
  TEST_ASSERT_TRUE(lv_color_eq(ui_theme()->fg, ui_theme_colors(PALETTE_NIGHT)->fg));
}

static void test_apply_token_reconnects_signalk(void) {
  strcpy(next.sk.token, "abc");
  TEST_ASSERT_EQUAL_UINT(APPLY_SIGNALK, app_apply_config(&next));
  TEST_ASSERT_EQUAL_STRING("abc", app_config()->sk.token);
}

static void test_apply_exclude_rebinds(void) {
  strcpy(next.roles[0].exclude[0], "port");
  next.roles[0].exclude_count = 1;
  TEST_ASSERT_EQUAL_UINT(APPLY_ROLES, app_apply_config(&next));
  TEST_ASSERT_EQUAL_UINT8(1, app_config()->roles[0].exclude_count);
}

static void test_apply_fewer_roles_resizes_navigation(void) {
  next.role_count = 2;
  TEST_ASSERT_EQUAL_UINT(APPLY_ROLES, app_apply_config(&next));
  TEST_ASSERT_EQUAL_UINT8(2, ui_shell_nav()->role_count);
}

static void test_apply_wifi(void) {
  strcpy(next.wifi.ssid, "Other");
  TEST_ASSERT_EQUAL_UINT(APPLY_WIFI, app_apply_config(&next));
}

static int ran;
static void bump(void *arg) { ran += *(int *)arg; }

static void test_post_runs_on_tick(void) {
  static int three = 3;
  ran = 0;
  TEST_ASSERT_TRUE(app_post(bump, &three, 0));
  TEST_ASSERT_EQUAL_INT(0, ran);   // not yet: only the UI task runs it
  app_tick();
  TEST_ASSERT_EQUAL_INT(3, ran);
  app_tick();
  TEST_ASSERT_EQUAL_INT(3, ran);   // once
}

// Review Focus 1: an alarm banner survives a config change that rebuilds the pages.
static void test_apply_keeps_alarm_banner(void) {
  alerts_source_up(true);
  alert_t al = {.state = AL_ALARM, .audible = true};
  strcpy(al.id, "propulsion.port.temperature");
  strcpy(al.message, "Port coolant high");
  alerts_upsert(&al, platform_millis());
  app_tick();
  TEST_ASSERT_TRUE(ui_alerts_banner_visible());
  next.role_count = 3;
  app_apply_config(&next);
  app_tick();
  TEST_ASSERT_TRUE(ui_alerts_banner_visible());
  alerts_source_up(false);
}

int main(void) {
  lv_init();
  lv_tick_set_cb(tick_ms);
  disp = lv_display_create(800, 480);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  UNITY_BEGIN();
  RUN_TEST(test_apply_identical_is_none);
  RUN_TEST(test_apply_palette_only_display);
  RUN_TEST(test_apply_token_reconnects_signalk);
  RUN_TEST(test_apply_exclude_rebinds);
  RUN_TEST(test_apply_fewer_roles_resizes_navigation);
  RUN_TEST(test_apply_wifi);
  RUN_TEST(test_post_runs_on_tick);
  RUN_TEST(test_apply_keeps_alarm_banner);
  return UNITY_END();
}
