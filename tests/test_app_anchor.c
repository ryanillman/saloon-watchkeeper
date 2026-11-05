// The Anchor role as the app wires it: subscriptions, the configured plugin's commands, and the local watch alert.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "alerts.h"
#include "anchor_ctl.h"
#include "anchor_record.h"
#include "anchor_watch.h"
#include "app.h"
#include <math.h>
#include "boatdata.h"
#include "config.h"
#include "platform.h"
#include "sk_cmd.h"

static uint8_t buf[800 * 48 * 2];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  lv_display_flush_ready(d);
}
static lv_display_t *disp;
static cfg_t next;
static anchor_pt_t tr[720];

void setUp(void) {
  cfg_report_t rep;
  app_init(NULL, &rep);
  app_bind();
  app_start_ui(disp, SIZE_WIDE);
  next = *app_config();
}
void tearDown(void) {}

static bool subscribed(const sk_sub_t *s, size_t n, const char *path) {
  bd_id_t id = bd_find(path);
  for (size_t i = 0; i < n; i++)
    if (id != BD_NONE && s[i].id == id) return true;
  return false;
}

static void test_anchor_paths_subscribed_when_role_configured(void) {
  sk_sub_t s[64];
  size_t n = app_bound_ids(s, 64);
  TEST_ASSERT_TRUE(subscribed(s, n, "navigation.anchor.position"));
  TEST_ASSERT_TRUE(subscribed(s, n, "navigation.anchor.maxRadius"));
  TEST_ASSERT_TRUE(subscribed(s, n, "navigation.anchor.currentRadius"));
  TEST_ASSERT_TRUE(subscribed(s, n, "navigation.anchor.bearingTrue"));
  // Without the role there is nothing to watch.
  uint8_t kept = 0;
  for (uint8_t i = 0; i < next.role_count; i++)
    if (strcmp(next.roles[i].role, "anchor")) next.roles[kept++] = next.roles[i];
  next.role_count = kept;
  strcpy(next.start_role, next.roles[0].role);
  app_apply_config(&next);
  n = app_bound_ids(s, 64);
  TEST_ASSERT_FALSE(subscribed(s, n, "navigation.anchor.currentRadius"));
}

static const char *drop_path(void) {
  static sk_cmd_t cmd;
  sk_cmd_handle_t h;
  alerts_source_up(true);
  sk_cmd_set_connected(true);
  anchor_ctl_update(1000, tr, 720);   // no anchor on a live server: NOT SET, so the button drops
  anchor_ctl_tap_main(1000);
  return sk_cmd_next(&cmd, &h, 1000) ? cmd.path : "(nothing sent)";
}

static void test_drop_button_submits_profile_command_for_configured_plugin(void) {
  TEST_ASSERT_EQUAL_STRING("/plugins/hoekens-anchor-alarm/dropAnchor", drop_path());   // the default
  strcpy(next.anchor_plugin, "anchoralarm");
  app_apply_config(&next);
  TEST_ASSERT_EQUAL_STRING("/plugins/anchoralarm/dropAnchor", drop_path());
}

static void test_unknown_plugin_name_falls_back_to_hoeken(void) {
  strcpy(next.anchor_plugin, "bogus");   // load reports and replaces it; a hand-built config must not crash the page
  app_apply_config(&next);
  TEST_ASSERT_EQUAL_STRING("/plugins/hoekens-anchor-alarm/dropAnchor", drop_path());
}

static void test_command_results_reach_the_page(void) {
  static sk_cmd_t cmd;
  sk_cmd_handle_t h;
  TEST_ASSERT_EQUAL_STRING("/plugins/hoekens-anchor-alarm/dropAnchor", drop_path());
  alerts_source_up(true);
  anchor_ctl_update(1100, tr, 720);
  TEST_ASSERT_EQUAL_STRING("", anchor_ctl_state()->message);   // sent, nothing back yet
  anchor_ctl_tap_main(1200);   // a second tap while the first is pending sends nothing
  TEST_ASSERT_FALSE(sk_cmd_next(&cmd, &h, 1200));
}

static void test_local_watch_alert_survives_source_loss_in_app_tick(void) {
  char dir[64] = "/tmp/bi_appanchor_XXXXXX";
  TEST_ASSERT_NOT_NULL(mkdtemp(dir));
  anchor_record_t armed = {true, 30, 0};
  TEST_ASSERT_TRUE(anchor_record_save(dir, &armed));   // the display went down with an anchor set
  cfg_report_t rep;
  app_init(dir, &rep);
  app_bind();
  app_start_ui(disp, SIZE_WIDE);
  alerts_source_up(true);   // the server is up and says it has no anchor
  bd_set_number(bd_intern("navigation.anchor.position"), NAN, 1, platform_millis());
  app_tick();
  alert_t a;
  TEST_ASSERT_TRUE(alerts_get(ANCHOR_WATCH_ALERT_ID, &a));
  TEST_ASSERT_NOT_NULL(strstr(a.message, "NOT RESTORED"));
  alerts_source_up(false);
  TEST_ASSERT_TRUE(alerts_get(ANCHOR_WATCH_ALERT_ID, &a));   // the alarm outlives the connection
  char cmd[96];
  snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
  system(cmd);
}

int main(void) {
  lv_init();
  disp = lv_display_create(800, 480);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  UNITY_BEGIN();
  RUN_TEST(test_anchor_paths_subscribed_when_role_configured);
  RUN_TEST(test_drop_button_submits_profile_command_for_configured_plugin);
  RUN_TEST(test_unknown_plugin_name_falls_back_to_hoeken);
  RUN_TEST(test_command_results_reach_the_page);
  RUN_TEST(test_local_watch_alert_survives_source_loss_in_app_tick);
  return UNITY_END();
}
