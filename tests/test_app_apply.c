// app_apply_config does only what changed; app_post runs work on the UI task.
#include <string.h>
#include "alerts.h"
#include "app.h"
#include "boatdata.h"
#include "config.h"
#include "glance_page.h"
#include "history.h"
#include "platform.h"
#include "ui_alerts.h"
#include "ui_shell.h"
#include "app_can.h"
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

// Bytes after the string's NUL are not part of the setting: a config parsed from the file and
// one typed in setup differ there, and that must not drop and rejoin the network.
static void test_apply_wifi_ignores_bytes_after_the_nul(void) {
  strcpy(next.wifi.ssid, "Net");
  strcpy(next.wifi.password, "secret");
  TEST_ASSERT_EQUAL_UINT(APPLY_WIFI, app_apply_config(&next));
  strcpy(next.wifi.ssid + 4, "junk");
  strcpy(next.wifi.password + 7, "junk");
  TEST_ASSERT_EQUAL_UINT(APPLY_NONE, app_apply_config(&next));
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

static void test_glance_is_the_first_page_with_a_tile_per_role_that_has_one(void) {
  TEST_ASSERT_EQUAL_UINT8(0, ui_shell_nav()->role);
  TEST_ASSERT_EQUAL_UINT8(4, glance_tile_count());   // engine, power, tanks and anchor
}

static void test_tapping_a_glance_tile_opens_that_role(void) {
  TEST_ASSERT_NOT_NULL(glance_part(1, GP_TILE));
  lv_obj_send_event(glance_part(1, GP_TILE), LV_EVENT_CLICKED, NULL);   // the second tile is Power
  lv_timer_handler();
  TEST_ASSERT_EQUAL_UINT8(2, ui_shell_nav()->role);   // glance, engine, power, tanks, anchor
}

static void test_history_tracks_the_trend_paths_and_samples_each_tick(void) {
  bd_id_t soc = bd_intern("electrical.batteries.279.capacity.stateOfCharge");
  app_bind();   // a battery appeared
  bd_id_t ids[HIST_MAX_PATHS];
  size_t n = hist_tracked(ids, HIST_MAX_PATHS);
  bool found = false;
  for (size_t i = 0; i < n; i++) found = found || ids[i] == soc;
  TEST_ASSERT_TRUE(found);
  bd_set_number(soc, 0.8, 1, platform_millis());
  app_tick();
  float v[1];
  TEST_ASSERT_EQUAL_UINT(1, hist_read(soc, 1, v));
  TEST_ASSERT_EQUAL_FLOAT(0.8f, v[0]);
}

static void test_a_path_made_by_any_source_gets_its_instance_bound(void) {
  // The NMEA 2000 bus creates paths with no Signal K discovery behind them; the roles must still pick them up.
  bd_id_t soc = bd_intern("electrical.batteries.1.capacity.stateOfCharge");
  app_tick();   // no app_bind call and no network: the tick has to notice the new path
  bd_id_t ids[HIST_MAX_PATHS];
  size_t n = hist_tracked(ids, HIST_MAX_PATHS);
  bool found = false;
  for (size_t i = 0; i < n; i++) found = found || ids[i] == soc;
  TEST_ASSERT_TRUE(found);
}

static bool dot_shown(lv_obj_t *dot) { return !lv_obj_has_flag(dot, LV_OBJ_FLAG_HIDDEN); }

static void test_strip_shows_signal_k_alone_until_the_bus_is_enabled(void) {
  app_tick();
  TEST_ASSERT_TRUE(dot_shown(ui_shell_source_dot()));
  TEST_ASSERT_FALSE(dot_shown(ui_shell_can_dot()));
}

static void test_strip_shows_both_sources_in_fallback_mode(void) {
  next.n2k.enabled = true;
  next.n2k.only = false;
  app_apply_config(&next);
  app_tick();
  TEST_ASSERT_TRUE(dot_shown(ui_shell_source_dot()));
  TEST_ASSERT_TRUE(dot_shown(ui_shell_can_dot()));
}

static void test_strip_hides_signal_k_when_the_bus_is_the_only_source(void) {
  next.n2k.enabled = true;
  next.n2k.only = true;
  app_apply_config(&next);
  app_tick();
  TEST_ASSERT_FALSE(dot_shown(ui_shell_source_dot()));
  TEST_ASSERT_TRUE(dot_shown(ui_shell_can_dot()));
  // The bus has not been started in this test: the dot says offline, with the text that names it.
  TEST_ASSERT_EQUAL_STRING("CAN OFFLINE", lv_label_get_text(ui_shell_status()));
}

static bool clock_hidden(void) { return lv_obj_has_flag(ui_shell_clock(), LV_OBJ_FLAG_HIDDEN); }

static void test_clock_hidden_until_a_time_arrives(void) {
  app_tick();
  TEST_ASSERT_TRUE(clock_hidden());
}

// Two readings one second apart, as a running GPS sends them.
static void gps_time(const char *first, const char *second) {
  bd_id_t id = bd_intern("navigation.datetime");
  int64_t t = platform_millis();
  bd_set_text(id, first, 1, t);
  app_tick();
  bd_set_text(id, second, 1, t + 1000);
  app_tick();
}

static void test_clock_shows_signalk_time_in_the_configured_zone(void) {
  gps_time("2026-11-04T19:08:24.174Z", "2026-11-04T19:08:25.174Z");
  TEST_ASSERT_FALSE(clock_hidden());
  TEST_ASSERT_EQUAL_STRING("19:08", lv_label_get_text(ui_shell_clock()));
  next.utc_offset_min = -330;
  app_apply_config(&next);
  app_tick();
  TEST_ASSERT_EQUAL_STRING("13:38", lv_label_get_text(ui_shell_clock()));
}

static void test_clock_hides_a_single_stale_value(void) {   // what Signal K holds for a GPS that is off
  bd_set_text(bd_intern("navigation.datetime"), "2026-11-03T18:03:00Z", 1, platform_millis());
  app_tick();
  TEST_ASSERT_TRUE(clock_hidden());
  bd_set_text(bd_intern("navigation.datetime"), "2026-11-03T18:03:00Z", 1, platform_millis() + 15000);   // resent
  app_tick();
  TEST_ASSERT_TRUE(clock_hidden());
}

static void test_clock_keeps_running_when_the_time_goes_invalid(void) {
  gps_time("2026-11-04T19:08:24Z", "2026-11-04T19:08:25Z");
  bd_invalidate_source(1);   // Signal K dropped
  app_tick();
  TEST_ASSERT_FALSE(clock_hidden());
  TEST_ASSERT_EQUAL_STRING("19:08", lv_label_get_text(ui_shell_clock()));
}

static void test_clock_ignores_a_malformed_time(void) {
  bd_id_t id = bd_intern("navigation.datetime");
  bd_set_text(id, "not a time", 1, platform_millis());
  app_tick();
  TEST_ASSERT_TRUE(clock_hidden());
}

static int8_t role_index(const char *name) {
  for (uint8_t i = 0; i < app_config()->role_count; i++)
    if (!strcmp(app_config()->roles[i].role, name)) return (int8_t)i;
  return -1;
}

static void test_alert_role_routes_anchor(void) {
  int8_t anchor = role_index("anchor"), engine = role_index("engine");
  TEST_ASSERT_GREATER_OR_EQUAL_INT8(0, anchor);
  TEST_ASSERT_GREATER_OR_EQUAL_INT8(0, engine);
  // The notification's own id has no trailing segment: it is exactly "navigation.anchor".
  TEST_ASSERT_EQUAL_INT8(anchor, app_alert_role("navigation.anchor"));
  TEST_ASSERT_EQUAL_INT8(anchor, app_alert_role("navigation.anchor.watch"));
  TEST_ASSERT_EQUAL_INT8(-1, app_alert_role("navigation.anchorage"));
  TEST_ASSERT_EQUAL_INT8(engine, app_alert_role("propulsion.port.temperature"));
}

static bool subscribes(const sk_sub_t *s, size_t n, const char *path) {
  bd_id_t id = bd_find(path);
  for (size_t i = 0; i < n; i++)
    if (id != BD_NONE && s[i].id == id) return true;
  return false;
}

static void test_navigation_role_subscribes_to_its_ids(void) {
  sk_sub_t s[128];
  size_t n = app_bound_ids(s, 128);
  TEST_ASSERT_FALSE(subscribes(s, n, "environment.wind.angleApparent"));   // not configured: nothing to read
  strcpy(next.roles[next.role_count++].role, "navigation");
  TEST_ASSERT_EQUAL_UINT(APPLY_ROLES, app_apply_config(&next));
  n = app_bound_ids(s, 128);
  TEST_ASSERT_TRUE(subscribes(s, n, "environment.wind.angleApparent"));
  TEST_ASSERT_TRUE(subscribes(s, n, "navigation.course.calcValues.bearingTrackTrue"));
  TEST_ASSERT_TRUE(subscribes(s, n, "steering.autopilot.state"));
  TEST_ASSERT_TRUE(subscribes(s, n, "environment.current"));
}

int main(void) {
  lv_init();
  lv_tick_set_cb(tick_ms);
  disp = lv_display_create(800, 480);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  UNITY_BEGIN();
  RUN_TEST(test_glance_is_the_first_page_with_a_tile_per_role_that_has_one);
  RUN_TEST(test_tapping_a_glance_tile_opens_that_role);
  RUN_TEST(test_history_tracks_the_trend_paths_and_samples_each_tick);
  RUN_TEST(test_a_path_made_by_any_source_gets_its_instance_bound);
  RUN_TEST(test_strip_shows_signal_k_alone_until_the_bus_is_enabled);
  RUN_TEST(test_strip_shows_both_sources_in_fallback_mode);
  RUN_TEST(test_strip_hides_signal_k_when_the_bus_is_the_only_source);
  RUN_TEST(test_clock_hidden_until_a_time_arrives);
  RUN_TEST(test_clock_shows_signalk_time_in_the_configured_zone);
  RUN_TEST(test_clock_hides_a_single_stale_value);
  RUN_TEST(test_clock_keeps_running_when_the_time_goes_invalid);
  RUN_TEST(test_clock_ignores_a_malformed_time);
  RUN_TEST(test_apply_identical_is_none);
  RUN_TEST(test_apply_palette_only_display);
  RUN_TEST(test_apply_token_reconnects_signalk);
  RUN_TEST(test_apply_exclude_rebinds);
  RUN_TEST(test_apply_fewer_roles_resizes_navigation);
  RUN_TEST(test_apply_wifi);
  RUN_TEST(test_apply_wifi_ignores_bytes_after_the_nul);
  RUN_TEST(test_post_runs_on_tick);
  RUN_TEST(test_apply_keeps_alarm_banner);
  RUN_TEST(test_alert_role_routes_anchor);
  RUN_TEST(test_navigation_role_subscribes_to_its_ids);
  return UNITY_END();
}
