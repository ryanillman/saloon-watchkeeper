#include <stdio.h>
#include <string.h>
#include "alerts.h"
#include "ui_alerts.h"
#include "ui_holdbtn.h"
#include "ui_quick.h"
#include "ui_shell.h"
#include "unity.h"

static uint32_t now_ms;
static uint32_t fake_tick(void) { return now_ms; }
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
static const ui_quick_cb_t cb = {on_palette, on_bright, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
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

static int reset_calls;
static void on_reset(void) { reset_calls++; }
static const ui_quick_cb_t cb_reset = {on_palette, on_bright, NULL, NULL, NULL, on_reset, NULL, NULL, NULL};

static void test_reset_button_hidden_without_callback(void) {
  ui_quick_open(lv_screen_active(), "day", 80, true, &cb);
  TEST_ASSERT_NULL(ui_quick_part(UI_QUICK_RESET));
}

static void test_reset_button_calls_back_and_closes(void) {
  reset_calls = 0;
  ui_quick_open(lv_screen_active(), "day", 80, true, &cb_reset);
  TEST_ASSERT_NOT_NULL(ui_quick_part(UI_QUICK_RESET));
  lv_obj_send_event(ui_quick_part(UI_QUICK_RESET), LV_EVENT_CLICKED, NULL);
  TEST_ASSERT_EQUAL(1, reset_calls);
  TEST_ASSERT_FALSE(ui_quick_is_open());
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
  ui_alerts_attach(lv_screen_active(), ui_shell_strip(), SIZE_WIDE, role_of, NULL);
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
  snprintf(al.ref, sizeof al.ref, "ref-%s", id);
  snprintf(al.message, sizeof al.message, "%s", msg);
  alerts_upsert(&al, 1000);
}

static void update(int64_t now) {
  alerts_snapshot_t snap;
  alerts_snapshot(&snap);
  ui_alerts_update(&snap, now);
}

// An acked alarm has left the banner for the bell; the user opens the panel; a new alarm must close it.
static void test_second_alarm_closes_panel(void) {
  alerts_init();
  alerts_source_up(true);
  ui_alerts_attach(lv_screen_active(), ui_shell_strip(), SIZE_WIDE, role_of, NULL);
  raise_alarm("a.one", "One");
  update(0);
  alerts_request("a.one", AL_REQ_ACK);
  update(2000);
  TEST_ASSERT_FALSE(ui_alerts_banner_visible());
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
  ui_alerts_attach(lv_screen_active(), ui_shell_strip(), SIZE_WIDE, role_of, NULL);
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
  static const ui_quick_cb_t with = {on_palette, on_bright, NULL, on_buzz, buzz_state, NULL, NULL, NULL, NULL};
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

// ---- the emergency row: at the bottom, below a gap and a divider, hold to confirm ----

static bool em_up = true;
static int em_declared = UI_QUICK_EM_NONE, em_calls, em_last;
static bool em_available(void) { return em_up; }
static int em_get_declared(void) { return em_declared; }
static void on_em(int which) {
  em_calls++;
  em_last = which;
}
static const ui_quick_cb_t cb_em = {.on_palette = on_palette, .on_brightness = on_bright, .on_setup = NULL, .emergency_available = em_available,
                                    .emergency_declared = em_get_declared, .on_emergency = on_em};

static void em_reset_state(void) {
  em_up = true;
  em_declared = UI_QUICK_EM_NONE;
  em_calls = 0;
  em_last = -1;
}
static void run(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 50) {
    now_ms += 50;
    lv_timer_handler();
  }
}
static void hold_btn(lv_obj_t *b, uint32_t ms) {
  lv_obj_send_event(b, LV_EVENT_PRESSED, NULL);
  run(ms);
  lv_obj_send_event(b, LV_EVENT_RELEASED, NULL);
}
static int32_t top_of(lv_obj_t *o) {
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  return a.y1;
}
static int32_t bottom_of(lv_obj_t *o) {
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  return a.y2;
}

static void test_emergency_row_is_hidden_when_it_is_not_available(void) {
  em_reset_state();
  ui_quick_open(lv_screen_active(), "day", 80, true, &cb);   // no emergency callbacks at all
  TEST_ASSERT_NULL(ui_quick_part(UI_QUICK_BTN_MOB));
  TEST_ASSERT_NULL(ui_quick_part(UI_QUICK_SEPARATOR));
  ui_quick_close();
  settle();
  em_up = false;   // Signal K down, or no Navigation role
  ui_quick_open(lv_screen_active(), "day", 80, true, &cb_em);
  TEST_ASSERT_NULL(ui_quick_part(UI_QUICK_BTN_MOB));
  TEST_ASSERT_NULL(ui_quick_part(UI_QUICK_BTN_FIRE));
  TEST_ASSERT_NULL(ui_quick_part(UI_QUICK_BTN_SINKING));
  TEST_ASSERT_NULL(ui_quick_part(UI_QUICK_BTN_CANCEL));
}

static void test_emergency_row_sits_at_the_bottom_below_a_gap_and_a_divider(void) {
  em_reset_state();
  ui_quick_open(lv_screen_active(), "day", 80, true, &cb_em);
  lv_refr_now(NULL);
  lv_obj_t *close = ui_quick_part(UI_QUICK_CLOSE), *sep = ui_quick_part(UI_QUICK_SEPARATOR), *label = ui_quick_part(UI_QUICK_EM_LABEL),
           *mob = ui_quick_part(UI_QUICK_BTN_MOB), *fire = ui_quick_part(UI_QUICK_BTN_FIRE), *sink = ui_quick_part(UI_QUICK_BTN_SINKING);
  TEST_ASSERT_NOT_NULL(sep);
  TEST_ASSERT_NOT_NULL(label);
  TEST_ASSERT_NOT_NULL(mob);
  TEST_ASSERT_NOT_NULL(fire);
  TEST_ASSERT_NOT_NULL(sink);
  // Order, top to bottom: the everyday options, a gap, the divider, the label, the emergency buttons.
  TEST_ASSERT_GREATER_OR_EQUAL_INT32_MESSAGE(bottom_of(close) + 24, top_of(sep), "a clear gap above the divider");
  TEST_ASSERT_GREATER_THAN_INT32(bottom_of(sep), top_of(label));
  TEST_ASSERT_GREATER_THAN_INT32(bottom_of(label) - 1, top_of(mob));
  TEST_ASSERT_GREATER_THAN_INT32(bottom_of(close), top_of(mob));
  TEST_ASSERT_EQUAL_INT32(top_of(mob), top_of(fire));   // one row, in order
  TEST_ASSERT_EQUAL_INT32(top_of(mob), top_of(sink));
  lv_area_t am, af, as;
  lv_obj_get_coords(mob, &am);
  lv_obj_get_coords(fire, &af);
  lv_obj_get_coords(sink, &as);
  TEST_ASSERT_TRUE(am.x2 < af.x1 && af.x2 < as.x1);
  TEST_ASSERT_GREATER_OR_EQUAL_INT32_MESSAGE(64, bottom_of(mob) - top_of(mob) + 1, "big enough to hit in a hurry");
}

static void test_a_hold_declares_and_closes_the_panel(void) {
  const struct { ui_quick_part_t part; int which; } cases[] = {
    {UI_QUICK_BTN_MOB, UI_QUICK_EM_MOB}, {UI_QUICK_BTN_FIRE, UI_QUICK_EM_FIRE}, {UI_QUICK_BTN_SINKING, UI_QUICK_EM_SINKING}};
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    em_reset_state();
    ui_quick_open(lv_screen_active(), "day", 80, true, &cb_em);
    hold_btn(ui_quick_part(cases[i].part), 1100);
    TEST_ASSERT_EQUAL_MESSAGE(1, em_calls, "one call per hold");
    TEST_ASSERT_EQUAL(cases[i].which, em_last);
    TEST_ASSERT_FALSE(ui_quick_is_open());
    settle();
  }
}

static void test_a_tap_or_a_short_hold_does_nothing_and_leaves_the_panel_open(void) {
  em_reset_state();
  ui_quick_open(lv_screen_active(), "day", 80, true, &cb_em);
  lv_obj_t *mob = ui_quick_part(UI_QUICK_BTN_MOB);
  hold_btn(mob, 100);
  lv_obj_send_event(mob, LV_EVENT_CLICKED, NULL);
  hold_btn(mob, 800);
  run(1500);
  TEST_ASSERT_EQUAL(0, em_calls);
  TEST_ASSERT_TRUE(ui_quick_is_open());
}

static void test_cancel_shows_only_when_something_is_declared_and_needs_a_hold(void) {
  em_reset_state();
  ui_quick_open(lv_screen_active(), "day", 80, true, &cb_em);
  TEST_ASSERT_TRUE(lv_obj_has_flag(ui_quick_part(UI_QUICK_BTN_CANCEL), LV_OBJ_FLAG_HIDDEN));
  ui_quick_close();
  settle();
  em_declared = UI_QUICK_EM_FIRE;
  ui_quick_open(lv_screen_active(), "day", 80, true, &cb_em);
  lv_obj_t *cancel = ui_quick_part(UI_QUICK_BTN_CANCEL);
  TEST_ASSERT_FALSE(lv_obj_has_flag(cancel, LV_OBJ_FLAG_HIDDEN));
  hold_btn(cancel, 300);
  TEST_ASSERT_EQUAL(0, em_calls);
  hold_btn(cancel, 1100);
  TEST_ASSERT_EQUAL(1, em_calls);
  TEST_ASSERT_EQUAL(UI_QUICK_EM_CANCEL, em_last);
  TEST_ASSERT_FALSE(ui_quick_is_open());
  settle();
}

static void test_the_declared_nature_is_filled_and_one_declared_elsewhere_shows_cancel_only(void) {
  em_reset_state();
  em_declared = UI_QUICK_EM_MOB;
  ui_quick_open(lv_screen_active(), "day", 80, true, &cb_em);
  lv_color_t alarm = ui_theme()->alarm;
  TEST_ASSERT_TRUE(lv_color_eq(lv_obj_get_style_bg_color(ui_quick_part(UI_QUICK_BTN_MOB), LV_PART_MAIN), alarm));
  TEST_ASSERT_FALSE(lv_color_eq(lv_obj_get_style_bg_color(ui_quick_part(UI_QUICK_BTN_FIRE), LV_PART_MAIN), alarm));
  ui_quick_close();
  settle();
  em_declared = UI_QUICK_EM_OTHER;   // flooding or other, raised by another display: no button for it here
  ui_quick_open(lv_screen_active(), "day", 80, true, &cb_em);
  TEST_ASSERT_FALSE(lv_color_eq(lv_obj_get_style_bg_color(ui_quick_part(UI_QUICK_BTN_MOB), LV_PART_MAIN), alarm));
  TEST_ASSERT_FALSE(lv_obj_has_flag(ui_quick_part(UI_QUICK_BTN_CANCEL), LV_OBJ_FLAG_HIDDEN));
}

static void fits_on(int32_t w, int32_t h, size_class_t sc) {
  lv_display_t *old = lv_display_get_default();
  lv_display_t *d = lv_display_create(w, h);
  static uint8_t b2[800 * 48 * 2];
  lv_display_set_flush_cb(d, flush);
  lv_display_set_buffers(d, b2, NULL, sizeof b2, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_default(d);
  ui_shell_create(d, sc, PALETTE_DAY, roles, 1, 0);
  em_reset_state();
  em_declared = UI_QUICK_EM_MOB;   // the tallest case: CANCEL showing too
  static const ui_quick_cb_t full = {.on_palette = on_palette, .on_brightness = on_bright, .on_setup = tapped, .on_buzzer = on_buzz,
                                     .buzzer_state = buzz_state, .on_reset_screen = on_reset, .emergency_available = em_available,
                                     .emergency_declared = em_get_declared, .on_emergency = on_em};
  ui_quick_open(lv_display_get_screen_active(d), "day", 80, true, &full);
  lv_refr_now(d);
  lv_obj_t *card = lv_obj_get_parent(lv_obj_get_parent(ui_quick_part(UI_QUICK_DAY)));
  TEST_ASSERT_LESS_OR_EQUAL_INT32_MESSAGE(h, bottom_of(card) + 1, "the whole panel, emergency row included, is on the screen");
  TEST_ASSERT_LESS_OR_EQUAL_INT32(h, bottom_of(ui_quick_part(UI_QUICK_BTN_MOB)) + 1);
  ui_quick_close();
  lv_timer_handler();
  lv_display_set_default(old);
  lv_display_delete(d);
}

static void test_the_panel_with_the_emergency_row_fits_the_wide_board(void) { fits_on(800, 480, SIZE_WIDE); }
static void test_the_panel_with_the_emergency_row_fits_the_square_board(void) { fits_on(480, 480, SIZE_SQUARE); }

int main(void) {
  lv_init();
  lv_tick_set_cb(fake_tick);
  lv_display_t *disp = lv_display_create(800, 480);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  (void)scr;
  UNITY_BEGIN();
  RUN_TEST(test_env_mode_mapping);
  RUN_TEST(test_quick_palette_callback);
  RUN_TEST(test_quick_no_slider_when_not_dimmable);
  RUN_TEST(test_quick_slider_reports_percent);
  RUN_TEST(test_reset_button_hidden_without_callback);
  RUN_TEST(test_reset_button_calls_back_and_closes);
  RUN_TEST(test_close_button_and_reopen);
  RUN_TEST(test_strip_tap_calls_handler);
  RUN_TEST(test_panel_sits_above_banner_and_alarm_closes_it);
  RUN_TEST(test_second_alarm_closes_panel);
  RUN_TEST(test_new_alarm_behind_banner_closes_panel);
  RUN_TEST(test_buzzer_switch_only_with_a_buzzer);
  RUN_TEST(test_emergency_row_is_hidden_when_it_is_not_available);
  RUN_TEST(test_emergency_row_sits_at_the_bottom_below_a_gap_and_a_divider);
  RUN_TEST(test_a_hold_declares_and_closes_the_panel);
  RUN_TEST(test_a_tap_or_a_short_hold_does_nothing_and_leaves_the_panel_open);
  RUN_TEST(test_cancel_shows_only_when_something_is_declared_and_needs_a_hold);
  RUN_TEST(test_the_declared_nature_is_filled_and_one_declared_elsewhere_shows_cancel_only);
  RUN_TEST(test_the_panel_with_the_emergency_row_fits_the_wide_board);
  RUN_TEST(test_the_panel_with_the_emergency_row_fits_the_square_board);
  return UNITY_END();
}
