#include <stdio.h>
#include <string.h>
#include "alerts.h"
#include "ui_alerts.h"
#include "ui_quick.h"
#include "ui_shell.h"
#include "unity.h"

static uint8_t buf[800 * 48 * 2];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  lv_display_flush_ready(d);
}

static char last_palette[8];
static int palette_calls, bright_calls;
static uint8_t last_bright;
static void on_palette(const char *c) { snprintf(last_palette, sizeof last_palette, "%s", c); palette_calls++; }
static void on_bright(uint8_t p) { last_bright = p; bright_calls++; }
static const ui_quick_cb_t cb = {on_palette, on_bright, NULL, NULL, NULL};
static bool buzz_on;
static int buzz_calls;
static void on_buzz(bool on) { buzz_on = on; buzz_calls++; }
static bool buzz_state(void) { return buzz_on; }

static lv_obj_t *scr;
static void page(lv_obj_t *area, uint8_t p, size_class_t sc, void *ctx) { (void)area; (void)p; (void)sc; (void)ctx; }
static ui_role_view_t roles[] = {{"ENGINE", 1, page, NULL, NULL}};
static int taps;
static void tapped(void) { taps++; }

static void settle(void) {
  lv_timer_handler();   // runs the deferred delete
  lv_refr_now(NULL);
}

void setUp(void) {
  ui_quick_close();
  settle();
  last_palette[0] = 0;
  palette_calls = bright_calls = taps = 0;
  last_bright = 0;
  ui_shell_create(lv_display_get_default(), SIZE_WIDE, PALETTE_DAY, roles, 1, 0);
}
void tearDown(void) { ui_quick_close(); settle(); }

static void test_env_mode_mapping(void) {
  TEST_ASSERT_EQUAL(PALETTE_NIGHT, palette_from_env_mode("night"));
  TEST_ASSERT_EQUAL(PALETTE_DUSK, palette_from_env_mode("dawn"));
  TEST_ASSERT_EQUAL(PALETTE_DUSK, palette_from_env_mode("dusk"));
  TEST_ASSERT_EQUAL(PALETTE_DAY, palette_from_env_mode("day"));
  TEST_ASSERT_EQUAL(PALETTE_DAY, palette_from_env_mode("sunrise"));
  TEST_ASSERT_EQUAL(PALETTE_DAY, palette_from_env_mode("sunset"));
  TEST_ASSERT_EQUAL(PALETTE_DAY, palette_from_env_mode(""));
  TEST_ASSERT_EQUAL(PALETTE_DAY, palette_from_env_mode(NULL));
}

static void test_quick_palette_callback(void) {
  ui_quick_open(lv_screen_active(), "day", 80, true, &cb);
  TEST_ASSERT_TRUE(ui_quick_is_open());
  lv_obj_send_event(ui_quick_part(UI_QUICK_NIGHT), LV_EVENT_CLICKED, NULL);
  TEST_ASSERT_EQUAL(1, palette_calls);
  TEST_ASSERT_EQUAL_STRING("night", last_palette);
  lv_obj_send_event(ui_quick_part(UI_QUICK_AUTO), LV_EVENT_CLICKED, NULL);
  TEST_ASSERT_EQUAL_STRING("auto", last_palette);
}

static void test_quick_no_slider_when_not_dimmable(void) {
  ui_quick_open(lv_screen_active(), "day", 100, false, &cb);
  TEST_ASSERT_NULL(ui_quick_part(UI_QUICK_SLIDER));
  TEST_ASSERT_NOT_NULL(ui_quick_part(UI_QUICK_ON_LABEL));
  TEST_ASSERT_EQUAL_STRING("ON", lv_label_get_text(ui_quick_part(UI_QUICK_ON_LABEL)));
}

static void test_quick_slider_reports_percent(void) {
  ui_quick_open(lv_screen_active(), "day", 80, true, &cb);
  lv_obj_t *sl = ui_quick_part(UI_QUICK_SLIDER);
  TEST_ASSERT_NOT_NULL(sl);
  TEST_ASSERT_NULL(ui_quick_part(UI_QUICK_ON_LABEL));
  TEST_ASSERT_EQUAL(80, lv_slider_get_value(sl));
  lv_slider_set_value(sl, 40, LV_ANIM_OFF);
  lv_obj_send_event(sl, LV_EVENT_VALUE_CHANGED, NULL);
  TEST_ASSERT_EQUAL(40, last_bright);
}

static void test_close_button_and_reopen(void) {
  ui_quick_open(lv_screen_active(), "dusk", 80, true, &cb);
  lv_obj_send_event(ui_quick_part(UI_QUICK_CLOSE), LV_EVENT_CLICKED, NULL);
  TEST_ASSERT_FALSE(ui_quick_is_open());
  settle();
  ui_quick_open(lv_screen_active(), "dusk", 80, true, &cb);
  TEST_ASSERT_TRUE(ui_quick_is_open());
}

static void test_strip_tap_calls_handler(void) {
  ui_shell_set_strip_tap(tapped);
  lv_obj_send_event(ui_shell_strip_tap(), LV_EVENT_CLICKED, NULL);
  TEST_ASSERT_EQUAL(1, taps);
  ui_shell_set_strip_tap(NULL);
}

static lv_obj_t *root_of(void) {   // button, row, card, root
  return lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(ui_quick_part(UI_QUICK_DAY))));
}

static int8_t role_of(const char *id) { (void)id; return 0; }

static void test_panel_sits_above_banner_and_alarm_closes_it(void) {
  alerts_init();
  alerts_source_up(true);
  ui_alerts_attach(lv_screen_active(), ui_shell_strip(), SIZE_WIDE, role_of);
  ui_quick_open(lv_screen_active(), "day", 80, true, &cb);
  lv_obj_t *scrn = lv_screen_active();
  TEST_ASSERT_EQUAL(lv_obj_get_child_count(scrn) - 1, lv_obj_get_index(root_of()));   // last child: drawn over the banner
  TEST_ASSERT_EQUAL_PTR(scrn, lv_obj_get_parent(root_of()));
  alert_t al = {0};
  snprintf(al.id, sizeof al.id, "propulsion.port.temperature");
  al.state = AL_ALARM;
  snprintf(al.message, sizeof al.message, "Port coolant high");
  alerts_upsert(&al, 1000);
  alerts_snapshot_t snap;
  alerts_snapshot(&snap);
  ui_alerts_update(&snap, 0);
  TEST_ASSERT_TRUE(ui_alerts_banner_visible());
  TEST_ASSERT_FALSE(ui_quick_is_open());
}

static void raise_alarm(const char *id, const char *msg) {
  alert_t al = {0};
  snprintf(al.id, sizeof al.id, "%s", id);
  al.state = AL_ALARM;
  snprintf(al.message, sizeof al.message, "%s", msg);
  alerts_upsert(&al, 1000);
}

static void update(int64_t now) {
  alerts_snapshot_t snap;
  alerts_snapshot(&snap);
  ui_alerts_update(&snap, now);
}

// The banner already shows an acked alarm; the user opens the panel; a second alarm must close it.
static void test_second_alarm_closes_panel(void) {
  alerts_init();
  alerts_source_up(true);
  ui_alerts_attach(lv_screen_active(), ui_shell_strip(), SIZE_WIDE, role_of);
  raise_alarm("a.one", "One");
  update(0);
  alerts_request("a.one", AL_REQ_ACK);
  update(2000);
  TEST_ASSERT_TRUE(ui_alerts_banner_visible());
  ui_quick_open(lv_screen_active(), "day", 80, true, &cb);
  update(3000);
  TEST_ASSERT_TRUE(ui_quick_is_open());   // nothing new: stays open
  raise_alarm("a.two", "Two");
  update(4000);
  TEST_ASSERT_FALSE(ui_quick_is_open());
}

// The banner shows an un-acked alarm and a second un-acked one arrives behind it.
static void test_new_alarm_behind_banner_closes_panel(void) {
  alerts_init();
  alerts_source_up(true);
  ui_alerts_attach(lv_screen_active(), ui_shell_strip(), SIZE_WIDE, role_of);
  raise_alarm("a.one", "One");
  update(0);
  ui_quick_open(lv_screen_active(), "day", 80, true, &cb);
  update(1000);
  TEST_ASSERT_TRUE(ui_quick_is_open());
  raise_alarm("a.two", "Two");
  update(2000);
  TEST_ASSERT_FALSE(ui_quick_is_open());
}

static void test_buzzer_switch_only_with_a_buzzer(void) {
  ui_quick_open(lv_screen_active(), "day", 80, true, &cb);
  TEST_ASSERT_NULL(ui_quick_part(UI_QUICK_BUZZER));   // this board has none
  ui_quick_close();
  lv_timer_handler();
  static const ui_quick_cb_t with = {on_palette, on_bright, NULL, on_buzz, buzz_state};
  buzz_on = false;
  buzz_calls = 0;
  ui_quick_open(lv_screen_active(), "day", 80, true, &with);
  lv_obj_t *sw = ui_quick_part(UI_QUICK_BUZZER);
  TEST_ASSERT_NOT_NULL(sw);
  TEST_ASSERT_FALSE(lv_obj_has_state(sw, LV_STATE_CHECKED));
  lv_obj_add_state(sw, LV_STATE_CHECKED);   // what a touch on the switch does
  lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, NULL);
  TEST_ASSERT_EQUAL_INT(1, buzz_calls);
  TEST_ASSERT_TRUE(buzz_on);
  ui_quick_close();
  lv_timer_handler();
}

int main(void) {
  lv_init();
  lv_display_t *disp = lv_display_create(800, 480);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  (void)scr;
  UNITY_BEGIN();
  RUN_TEST(test_env_mode_mapping);
  RUN_TEST(test_quick_palette_callback);
  RUN_TEST(test_quick_no_slider_when_not_dimmable);
  RUN_TEST(test_quick_slider_reports_percent);
  RUN_TEST(test_close_button_and_reopen);
  RUN_TEST(test_strip_tap_calls_handler);
  RUN_TEST(test_panel_sits_above_banner_and_alarm_closes_it);
  RUN_TEST(test_second_alarm_closes_panel);
  RUN_TEST(test_new_alarm_behind_banner_closes_panel);
  RUN_TEST(test_buzzer_switch_only_with_a_buzzer);
  return UNITY_END();
}
