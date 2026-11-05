#include <math.h>
#include <string.h>
#include "unity.h"
#include "alerts.h"
#include "anchor_ctl.h"
#include "anchor_page.h"
#include "boatdata.h"
#include "config.h"
#include "history.h"
#include "role.h"

#define T 100000LL
#define CAP 720

static cfg_t cfg;
static anchor_watch_t watch;
static anchor_pt_t track[CAP];
static anchor_cmd_t sent[8];
static bool sent_replaces[8];
static int nsent;
static int results[16];   // by handle; 0 = PENDING
static bool refuse;

static uint16_t fake_submit(const anchor_cmd_t *c, bool replaces) {
  if (refuse) return 0;
  sent[nsent] = *c;
  sent_replaces[nsent] = replaces;
  return (uint16_t)++nsent;
}
static int fake_result(uint16_t h) { return results[h]; }
static const anchor_ops_t ops = {fake_submit, fake_result};

void setUp(void) {
  bd_init();
  hist_init();
  alerts_init();
  alerts_source_up(true);
  cfg_defaults(&cfg);
  strcpy(cfg.units.distance, "m");
  memset(results, 0, sizeof results);
  memset(sent, 0, sizeof sent);
  nsent = 0;
  refuse = false;
  anchor_watch_init(&watch, NULL, 0, 60000);
  anchor_ctl_setup(&ops, ANCHOR_PLUGIN_HOEKEN, &watch, NULL, &cfg.units);
}
void tearDown(void) {}

static void holding(int64_t t, double limit) {
  bd_set_position(bd_intern("navigation.anchor.position"), 48.5, -123.0, 1, t);
  bd_set_number(bd_intern("navigation.anchor.maxRadius"), limit, 1, t);
  bd_set_number(bd_intern("navigation.anchor.currentRadius"), 20, 1, t);
  bd_set_number(bd_intern("navigation.anchor.bearingTrue"), 1.0, 1, t);
}
static const anchor_ctl_state_t *update(int64_t t) {
  anchor_ctl_update(t, track, CAP);
  return anchor_ctl_state();
}

static void test_drop_tap_sends_drop_with_staged_radius(void) {
  const anchor_ctl_state_t *s = update(T);
  TEST_ASSERT_EQUAL(AM_NOT_SET, s->view.mode);
  TEST_ASSERT_EQUAL(ANCHOR_BTN_DROP, s->button);
  TEST_ASSERT_EQUAL_DOUBLE(30, s->shown_limit_m);   // the default before anything is set
  anchor_ctl_step(+1, T);
  s = update(T);
  TEST_ASSERT_TRUE(s->limit_staged);
  TEST_ASSERT_EQUAL_INT(0, nsent);   // adjusting before the drop sends nothing
  anchor_ctl_tap_main(T);
  TEST_ASSERT_EQUAL_INT(1, nsent);
  TEST_ASSERT_EQUAL_STRING("/plugins/hoekens-anchor-alarm/dropAnchor", sent[0].path);
  TEST_ASSERT_NOT_NULL(strstr(sent[0].body, "\"radius\":35"));
  TEST_ASSERT_FALSE(sent_replaces[0]);
}

static void test_raise_hold_sends_only_after_1500ms(void) {
  holding(T, 50);
  TEST_ASSERT_EQUAL(ANCHOR_BTN_RAISE, update(T)->button);
  anchor_ctl_hold_begin(T);
  TEST_ASSERT_EQUAL_INT(93, update(T + 1400)->hold_pct);
  TEST_ASSERT_EQUAL_INT(0, nsent);
  update(T + 1500);
  TEST_ASSERT_EQUAL_INT(1, nsent);
  TEST_ASSERT_EQUAL_STRING("/plugins/hoekens-anchor-alarm/raiseAnchor", sent[0].path);
  anchor_ctl_hold_end(T + 1600);
  update(T + 3000);
  TEST_ASSERT_EQUAL_INT(1, nsent);   // once
}

static void test_raise_early_release_cancels(void) {
  holding(T, 50);
  update(T);
  anchor_ctl_hold_begin(T);
  update(T + 900);
  anchor_ctl_hold_end(T + 1000);
  TEST_ASSERT_EQUAL_INT(0, update(T + 2000)->hold_pct);
  TEST_ASSERT_EQUAL_INT(0, nsent);
}

static void test_tap_on_raise_button_does_nothing(void) {
  holding(T, 50);
  update(T);
  anchor_ctl_tap_main(T);   // a tap is a Drop; the button is Raise
  TEST_ASSERT_EQUAL_INT(0, nsent);
}

static void test_radius_step_5m_clamped_5_to_300(void) {
  holding(T, 50);
  update(T);
  anchor_ctl_step(+1, T);
  TEST_ASSERT_EQUAL_DOUBLE(55, update(T)->shown_limit_m);
  for (int i = 0; i < 20; i++) anchor_ctl_step(-1, T);
  TEST_ASSERT_EQUAL_DOUBLE(5, update(T)->shown_limit_m);
  for (int i = 0; i < 100; i++) anchor_ctl_step(+1, T);
  TEST_ASSERT_EQUAL_DOUBLE(300, update(T)->shown_limit_m);
}

static void test_radius_step_in_feet_display_units(void) {
  strcpy(cfg.units.distance, "ft");
  holding(T, 45.72);   // 150 ft
  update(T);
  anchor_ctl_step(+1, T);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 50.292, update(T)->shown_limit_m);   // 165 ft: one 15 ft step
  update(T + 1500);
  TEST_ASSERT_EQUAL_INT(1, nsent);
  TEST_ASSERT_NOT_NULL(strstr(sent[0].body, "50.292"));   // the command is in metres
}

static void test_staged_radius_sent_1500ms_after_last_tap_once(void) {
  holding(T, 50);
  update(T);
  anchor_ctl_step(+1, T);
  anchor_ctl_step(+1, T + 500);
  update(T + 1999);
  TEST_ASSERT_EQUAL_INT(0, nsent);
  update(T + 2000);
  TEST_ASSERT_EQUAL_INT(1, nsent);
  TEST_ASSERT_EQUAL_STRING("/plugins/hoekens-anchor-alarm/setZone", sent[0].path);
  TEST_ASSERT_NOT_NULL(strstr(sent[0].body, "\"radius\":60"));
  TEST_ASSERT_TRUE(sent_replaces[0]);
  update(T + 9000);
  TEST_ASSERT_EQUAL_INT(1, nsent);
}

static void test_radius_disabled_for_polygon_zone(void) {
  holding(T, 50);
  bd_set_text(bd_intern("navigation.anchor.watchZone"), "polygon", 1, T);
  const anchor_ctl_state_t *s = update(T);
  TEST_ASSERT_FALSE(s->radius_enabled);
  TEST_ASSERT_TRUE(s->main_enabled);   // drop and raise still work
  anchor_ctl_step(+1, T);
  TEST_ASSERT_EQUAL_DOUBLE(50, update(T + 3000)->shown_limit_m);
  TEST_ASSERT_EQUAL_INT(0, nsent);
}

static void test_controls_disabled_in_no_data(void) {
  alerts_source_up(false);
  const anchor_ctl_state_t *s = update(T);
  TEST_ASSERT_EQUAL(AM_NO_DATA, s->view.mode);
  TEST_ASSERT_FALSE(s->main_enabled);
  TEST_ASSERT_FALSE(s->radius_enabled);
  anchor_ctl_tap_main(T);
  anchor_ctl_hold_begin(T);
  anchor_ctl_step(+1, T);
  update(T + 3000);
  TEST_ASSERT_EQUAL_INT(0, nsent);
  TEST_ASSERT_FALSE(anchor_ctl_state()->limit_staged);
}

static void test_failed_result_shows_label_and_reverts_staged_radius(void) {
  holding(T, 50);
  update(T);
  anchor_ctl_step(+1, T);
  update(T + 1500);
  TEST_ASSERT_EQUAL_INT(1, nsent);
  results[1] = ANCHOR_RES_REJECTED;
  const anchor_ctl_state_t *s = update(T + 1600);
  TEST_ASSERT_EQUAL_STRING("REJECTED", s->message);
  TEST_ASSERT_FALSE(s->limit_staged);
  TEST_ASSERT_EQUAL_DOUBLE(50, s->shown_limit_m);
  TEST_ASSERT_EQUAL_STRING("REJECTED", update(T + 6599)->message);
  TEST_ASSERT_EQUAL_STRING("", update(T + 6600)->message);   // shown for 5 s
}

static void test_each_failure_kind_has_its_label(void) {
  const struct { int res; const char *label; } k[] = {{ANCHOR_RES_OFFLINE, "OFFLINE"}, {ANCHOR_RES_NO_ACCESS, "NO ACCESS"},
                                                      {ANCHOR_RES_NO_PLUGIN, "NO PLUGIN"}};
  for (size_t i = 0; i < 3; i++) {
    setUp();
    holding(T, 50);
    update(T);
    anchor_ctl_hold_begin(T);
    update(T + 1500);
    results[1] = k[i].res;
    TEST_ASSERT_EQUAL_STRING(k[i].label, update(T + 1600)->message);
  }
}

static void test_ok_but_no_republish_shows_not_confirmed_after_10s(void) {
  holding(T, 50);
  update(T);
  anchor_ctl_step(+1, T);
  update(T + 1500);
  results[1] = ANCHOR_RES_OK;
  TEST_ASSERT_EQUAL_STRING("", update(T + 1600)->message);
  TEST_ASSERT_EQUAL_STRING("", update(T + 11599)->message);
  const anchor_ctl_state_t *s = update(T + 11600);
  TEST_ASSERT_EQUAL_STRING("NOT CONFIRMED", s->message);
  TEST_ASSERT_FALSE(s->limit_staged);
  TEST_ASSERT_EQUAL_DOUBLE(50, s->shown_limit_m);   // still what the server last published
}

static void test_ok_and_republished_limit_is_confirmed(void) {
  holding(T, 50);
  update(T);
  anchor_ctl_step(+1, T);
  update(T + 1500);
  results[1] = ANCHOR_RES_OK;
  update(T + 1600);
  holding(T + 5000, 55);   // the plugin republished the new limit
  const anchor_ctl_state_t *s = update(T + 5100);
  TEST_ASSERT_FALSE(s->limit_staged);
  TEST_ASSERT_EQUAL_DOUBLE(55, s->shown_limit_m);
  TEST_ASSERT_EQUAL_STRING("", update(T + 20000)->message);   // and it is never reported as unconfirmed
}

static void test_duplicate_drop_tap_while_pending_sends_once(void) {
  update(T);
  anchor_ctl_tap_main(T);
  anchor_ctl_tap_main(T + 100);
  TEST_ASSERT_EQUAL_INT(1, nsent);
  results[1] = ANCHOR_RES_OK;
  update(T + 500);
  anchor_ctl_tap_main(T + 600);   // accepted but not yet seen on the server: still ignored
  TEST_ASSERT_EQUAL_INT(1, nsent);
}

static void test_refused_submit_shows_offline(void) {
  refuse = true;
  update(T);
  anchor_ctl_tap_main(T);
  TEST_ASSERT_EQUAL_STRING("OFFLINE", anchor_ctl_state()->message);
}

static void test_restoring_offers_raise_and_hold_clears_the_record(void) {
  watch.restoring = true;
  watch.rec.armed = true;
  const anchor_ctl_state_t *s = update(T);
  TEST_ASSERT_EQUAL(AM_RESTORING, s->view.mode);
  TEST_ASSERT_EQUAL(ANCHOR_BTN_RAISE, s->button);
  TEST_ASSERT_TRUE(s->main_enabled);
  anchor_ctl_hold_begin(T);
  update(T + 1500);
  TEST_ASSERT_EQUAL_INT(1, nsent);
  TEST_ASSERT_FALSE(watch.restoring);
  TEST_ASSERT_FALSE(watch.rec.armed);
}

static void test_drop_clears_a_stuck_record_too(void) {
  watch.rec.armed = true;
  update(T);
  anchor_ctl_tap_main(T);
  TEST_ASSERT_FALSE(watch.rec.armed);
}

static void test_no_sd_flag_follows_the_watch(void) {
  watch.have_sd = false;
  TEST_ASSERT_TRUE(update(T)->no_sd);
  watch.have_sd = true;
  TEST_ASSERT_FALSE(update(T)->no_sd);
}

// ---- the widgets ----

static uint8_t buf[1024 * 48 * 2];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  lv_display_flush_ready(d);
}
static lv_obj_t *scr;

static void build(size_class_t sc, int32_t w, int32_t h) {
  lv_obj_clean(scr);
  lv_obj_t *area = lv_obj_create(scr);
  lv_obj_set_size(area, w, h);
  role_binding_t b = {0};
  b.units = &cfg.units;
  anchor_build_page(area, 0, sc, &b);
  anchor_refresh(&b, T);
  lv_refr_now(NULL);
}

static void test_page_builds_in_every_size_and_state(void) {
  const struct { size_class_t sc; int32_t w, h; } sizes[] = {{SIZE_SQUARE, 480, 444}, {SIZE_WIDE, 800, 440}, {SIZE_WIDE, 1024, 560}};
  for (size_t i = 0; i < 3; i++) {
    setUp();
    build(sizes[i].sc, sizes[i].w, sizes[i].h);                 // not set
    TEST_ASSERT_EQUAL_STRING("DROP", anchor_page_button_text());
    holding(T, 50);
    anchor_refresh(&(role_binding_t){.units = &cfg.units}, T);
    lv_refr_now(NULL);
    TEST_ASSERT_EQUAL_STRING("RAISE", anchor_page_button_text());
    for (int m = 0; m < 30; m++) {   // a track
      bd_set_number(bd_intern("navigation.anchor.currentRadius"), 20 + m / 3.0, 1, T + m * 60000LL);
      bd_set_number(bd_intern("navigation.anchor.bearingTrue"), 1.0 + m / 10.0, 1, T + m * 60000LL);
      hist_tick(T + m * 60000LL);
    }
    anchor_refresh(&(role_binding_t){.units = &cfg.units}, T + 29 * 60000LL);
    lv_refr_now(NULL);
    alerts_source_up(false);
    anchor_refresh(&(role_binding_t){.units = &cfg.units}, T + 29 * 60000LL);   // no data
    lv_refr_now(NULL);
  }
}

static void test_page_shows_the_result_on_the_button(void) {
  build(SIZE_SQUARE, 480, 444);
  holding(T, 50);
  anchor_refresh(&(role_binding_t){.units = &cfg.units}, T);   // the page refreshes every 100 ms: the button is RAISE now
  anchor_ctl_hold_begin(T);
  anchor_refresh(&(role_binding_t){.units = &cfg.units}, T + 1500);
  results[1] = ANCHOR_RES_NO_PLUGIN;
  anchor_refresh(&(role_binding_t){.units = &cfg.units}, T + 1600);
  TEST_ASSERT_EQUAL_STRING("NO PLUGIN", anchor_page_button_text());
}

int main(void) {
  lv_init();
  lv_display_t *disp = lv_display_create(1024, 600);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  scr = lv_screen_active();
  UNITY_BEGIN();
  RUN_TEST(test_drop_tap_sends_drop_with_staged_radius);
  RUN_TEST(test_raise_hold_sends_only_after_1500ms);
  RUN_TEST(test_raise_early_release_cancels);
  RUN_TEST(test_tap_on_raise_button_does_nothing);
  RUN_TEST(test_radius_step_5m_clamped_5_to_300);
  RUN_TEST(test_radius_step_in_feet_display_units);
  RUN_TEST(test_staged_radius_sent_1500ms_after_last_tap_once);
  RUN_TEST(test_radius_disabled_for_polygon_zone);
  RUN_TEST(test_controls_disabled_in_no_data);
  RUN_TEST(test_failed_result_shows_label_and_reverts_staged_radius);
  RUN_TEST(test_each_failure_kind_has_its_label);
  RUN_TEST(test_ok_but_no_republish_shows_not_confirmed_after_10s);
  RUN_TEST(test_ok_and_republished_limit_is_confirmed);
  RUN_TEST(test_duplicate_drop_tap_while_pending_sends_once);
  RUN_TEST(test_refused_submit_shows_offline);
  RUN_TEST(test_restoring_offers_raise_and_hold_clears_the_record);
  RUN_TEST(test_drop_clears_a_stuck_record_too);
  RUN_TEST(test_no_sd_flag_follows_the_watch);
  RUN_TEST(test_page_builds_in_every_size_and_state);
  RUN_TEST(test_page_shows_the_result_on_the_button);
  return UNITY_END();
}
