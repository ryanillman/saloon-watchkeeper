// The emergency script page (Navigation page 1): what it says, and that every button needs a 1 s hold.
#include <string.h>
#include "alerts.h"
#include "boatdata.h"
#include "emergency.h"
#include "emergency_page.h"
#include "history.h"
#include "role.h"
#include "ui_theme.h"
#include "unity.h"

#define T 100000LL

static uint32_t now_ms;
static uint32_t fake_tick(void) { return now_ms; }
static uint8_t buf[1024 * 48 * 2];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  lv_display_flush_ready(d);
}
static lv_obj_t *scr;

static bool avail;
static int press_calls, cancel_calls;
static em_nature_t last_nature;
static char vname[32], vmmsi[16];
static bool a_avail(void) { return avail; }
static bool a_press(em_nature_t n) {
  press_calls++;
  last_nature = n;
  return true;
}
static bool a_cancel(void) {
  cancel_calls++;
  return true;
}
static const char *a_name(void) { return vname; }
static const char *a_mmsi(void) { return vmmsi; }
static const emergency_actions_t actions = {a_avail, a_press, a_cancel, a_name, a_mmsi};

void setUp(void) {
  bd_init();
  hist_init();
  alerts_init();
  em_reset();
  avail = true;
  press_calls = cancel_calls = 0;
  last_nature = EM_NONE;
  strcpy(vname, "SALOON");
  strcpy(vmmsi, "316001234");
  emergency_page_set_actions(&actions);
}
void tearDown(void) {}

static role_binding_t binding;
static void build(size_class_t sc, int32_t w, int32_t h) {
  lv_obj_clean(scr);
  lv_obj_t *area = lv_obj_create(scr);
  lv_obj_set_size(area, w, h);
  emergency_page_build(area, sc, &binding);
  emergency_page_refresh(&binding, T);
  lv_refr_now(NULL);
}
static void wide(void) { build(SIZE_WIDE, 800, 440); }

static lv_obj_t *part(const char *n) {
  lv_obj_t *o = emergency_page_part(n);
  TEST_ASSERT_NOT_NULL_MESSAGE(o, n);
  return o;
}
static bool shown(const char *n) { return !lv_obj_has_flag(part(n), LV_OBJ_FLAG_HIDDEN); }
static const char *label(const char *n) { return lv_label_get_text(part(n)); }
static void refresh(void) { emergency_page_refresh(&binding, T); }

static void run(uint32_t ms) {   // time passes in 50 ms steps so the hold timer runs repeatedly
  for (uint32_t t = 0; t < ms; t += 50) {
    now_ms += 50;
    lv_timer_handler();
  }
}
static void press(const char *n) { lv_obj_send_event(part(n), LV_EVENT_PRESSED, NULL); }
static void release(const char *n) { lv_obj_send_event(part(n), LV_EVENT_RELEASED, NULL); }

static const char *const NATURE_BTNS[] = {"btn_mob", "btn_fire", "btn_flood", "btn_sinking", "btn_other"};
static const em_nature_t NATURES[] = {EM_MOB, EM_FIRE, EM_FLOOD, EM_SINKING, EM_OTHER};

static void test_buttons_hidden_when_signalk_down(void) {
  avail = false;
  wide();
  for (size_t i = 0; i < 5; i++) TEST_ASSERT_FALSE_MESSAGE(shown(NATURE_BTNS[i]), NATURE_BTNS[i]);
  TEST_ASSERT_FALSE(shown("btn_cancel"));
  TEST_ASSERT_NOT_NULL(strstr(label("hint"), "UNAVAILABLE"));
}

static void test_buttons_shown_when_available(void) {
  wide();
  for (size_t i = 0; i < 5; i++) TEST_ASSERT_TRUE_MESSAGE(shown(NATURE_BTNS[i]), NATURE_BTNS[i]);
  TEST_ASSERT_NOT_NULL(strstr(label("hint"), "HOLD"));
}

static void test_mob_fires_after_1s_hold_and_only_once(void) {
  wide();
  press("btn_mob");
  run(900);
  TEST_ASSERT_EQUAL(0, press_calls);
  run(150);
  TEST_ASSERT_EQUAL(1, press_calls);
  TEST_ASSERT_EQUAL(EM_MOB, last_nature);
  run(3000);   // still held
  TEST_ASSERT_EQUAL(1, press_calls);
  release("btn_mob");
  run(500);
  TEST_ASSERT_EQUAL(1, press_calls);
}

static void test_release_before_1s_sends_nothing(void) {
  wide();
  press("btn_mob");
  run(500);
  release("btn_mob");
  run(2000);
  TEST_ASSERT_EQUAL(0, press_calls);
}

static void test_leaving_the_button_cancels_the_hold(void) {
  wide();
  press("btn_fire");
  run(500);
  lv_obj_send_event(part("btn_fire"), LV_EVENT_PRESS_LOST, NULL);
  run(2000);
  TEST_ASSERT_EQUAL(0, press_calls);
}

static void test_every_nature_needs_the_hold_and_a_tap_does_nothing(void) {
  wide();
  for (size_t i = 0; i < 5; i++) {
    press_calls = 0;
    press(NATURE_BTNS[i]);
    run(150);
    release(NATURE_BTNS[i]);
    lv_obj_send_event(part(NATURE_BTNS[i]), LV_EVENT_CLICKED, NULL);   // the click that follows a tap
    run(1500);
    TEST_ASSERT_EQUAL_MESSAGE(0, press_calls, NATURE_BTNS[i]);
    press(NATURE_BTNS[i]);
    run(1100);
    release(NATURE_BTNS[i]);
    TEST_ASSERT_EQUAL_MESSAGE(1, press_calls, NATURE_BTNS[i]);
    TEST_ASSERT_EQUAL_MESSAGE(NATURES[i], last_nature, NATURE_BTNS[i]);
  }
}

static void test_cancel_needs_the_hold_and_shows_only_when_active(void) {
  wide();
  TEST_ASSERT_FALSE(shown("btn_cancel"));
  em_press(EM_MOB, true, 41.5, -70.25, T, 1700000000);
  refresh();
  TEST_ASSERT_TRUE(shown("btn_cancel"));
  press("btn_cancel");
  run(300);
  release("btn_cancel");
  run(1500);
  TEST_ASSERT_EQUAL(0, cancel_calls);
  press("btn_cancel");
  run(1100);
  release("btn_cancel");
  TEST_ASSERT_EQUAL(1, cancel_calls);
  em_cancel_sent();
  refresh();
  TEST_ASSERT_FALSE(shown("btn_cancel"));
}

static void test_refresh_keeps_a_hold_in_progress(void) {
  wide();
  press("btn_mob");
  run(500);
  refresh();   // the app refreshes every 100 ms
  run(600);
  TEST_ASSERT_EQUAL(1, press_calls);
}

static void own_position(double lat, double lon, int64_t age_ms) {
  bd_set_position(bd_intern("navigation.position"), lat, lon, 1, T - age_ms);
}

static void declare(em_nature_t n) {
  em_press(n, true, 48.5402833, -123.1766667, T, 1700000000);
  refresh();
}

static void test_script_reads_the_call_for_the_declared_nature(void) {
  own_position(48.5402833, -123.1766667, 0);
  wide();
  declare(EM_MOB);
  TEST_ASSERT_EQUAL_STRING(
      "MAYDAY MAYDAY MAYDAY. This is SALOON SALOON SALOON. Position four eight degrees three two decimal four two "
      "minutes north one two three degrees one zero decimal six zero minutes west. We have a man overboard. Over.",
      label("script"));
  em_cancel_sent();
  declare(EM_SINKING);
  TEST_ASSERT_NOT_NULL(strstr(label("script"), "We are sinking. Over."));
}

static void test_steps_tell_a_beginner_how_to_use_the_radio(void) {
  own_position(48.5, -123.1, 0);
  wide();
  declare(EM_FIRE);
  TEST_ASSERT_TRUE(shown("steps"));
  const char *t = label("steps");
  TEST_ASSERT_NOT_NULL(strstr(t, "VHF"));
  TEST_ASSERT_NOT_NULL(strstr(t, "DISTRESS"));
  TEST_ASSERT_NOT_NULL(strstr(t, "3 seconds"));
  TEST_ASSERT_NOT_NULL(strstr(t, "microphone"));
}

static void test_nothing_declared_says_so_and_hides_the_steps(void) {
  wide();
  TEST_ASSERT_NOT_NULL(strstr(label("script"), "No emergency declared"));
  TEST_ASSERT_FALSE(shown("steps"));
  TEST_ASSERT_NULL(strstr(label("script"), "MAYDAY"));
}

static void test_no_fix_is_said_in_the_call_not_guessed(void) {
  wide();   // no position at all
  declare(EM_MOB);
  TEST_ASSERT_NOT_NULL(strstr(label("script"), "NO GPS FIX"));
  TEST_ASSERT_NULL(strstr(label("script"), "degrees"));
  own_position(48.5, -123.1, 11000);   // an older fix is no fix
  refresh();
  TEST_ASSERT_NOT_NULL(strstr(label("script"), "NO GPS FIX"));
}

static void test_unknown_name_is_bracketed_not_guessed(void) {
  vname[0] = 0;
  own_position(48.5, -123.1, 0);
  wide();
  declare(EM_MOB);
  TEST_ASSERT_NOT_NULL(strstr(label("script"), "(BOAT NAME"));
  TEST_ASSERT_NULL(strstr(label("script"), "SALOON"));
}

static void test_footer_keeps_the_numbers_for_the_plotter_and_the_radio(void) {
  own_position(41.5402833, -70.25, 3000);
  wide();
  TEST_ASSERT_EQUAL_STRING("41\xC2\xB0" "32.417'N 070\xC2\xB0" "15.000'W | fix 3 s old | MMSI 316001234", label("footer"));
  vmmsi[0] = 0;
  refresh();
  TEST_ASSERT_EQUAL_STRING("41\xC2\xB0" "32.417'N 070\xC2\xB0" "15.000'W | fix 3 s old", label("footer"));
  bd_set_invalid(bd_find("navigation.position"));
  refresh();
  TEST_ASSERT_EQUAL_STRING("NO POSITION", label("footer"));
}

// The longest name, in the largest text that still fits: nothing may run into the footer (pages must not scroll).
static void fits(size_class_t sc, int32_t w, int32_t h) {
  memset(vname, 'W', 31);
  vname[31] = 0;
  own_position(-123.456789, -123.456789, 0);
  build(sc, w, h);
  declare(EM_MOB);
  lv_obj_update_layout(scr);
  lv_obj_t *script = part("script"), *footer = part("footer");
  TEST_ASSERT_GREATER_THAN_INT32(0, lv_obj_get_height(script));
  TEST_ASSERT_LESS_OR_EQUAL_INT32_MESSAGE(lv_obj_get_y(footer), lv_obj_get_y(script) + lv_obj_get_height(script), "the call ends above the footer");
}

static void test_the_longest_call_fits_on_the_wide_board(void) { fits(SIZE_WIDE, 800, 440); }
static void test_the_longest_call_fits_on_the_square_board(void) { fits(SIZE_SQUARE, 480, 432); }

static void test_incoming_notification_preselects_the_nature(void) {
  wide();
  TEST_ASSERT_NOT_NULL(strstr(label("nature"), "NO EMERGENCY"));
  em_on_notification(EM_FIRE, true, false, 0, 0, NULL, T);
  refresh();
  TEST_ASSERT_NOT_NULL(strstr(label("nature"), "FIRE"));
  lv_refr_now(NULL);
  lv_color_t on = lv_obj_get_style_bg_color(part("btn_fire"), LV_PART_MAIN), off = lv_obj_get_style_bg_color(part("btn_mob"), LV_PART_MAIN);   // not CHECKED: the theme styles that state itself
  TEST_ASSERT_TRUE_MESSAGE(lv_color_eq(on, ui_theme()->alarm), "the declared nature's button is filled");
  TEST_ASSERT_FALSE(lv_color_eq(off, ui_theme()->alarm));
}

// Night is one red: the declared button's fill (alarm) and the usual text colour (fg) are the same, so its label must flip.
static void test_the_declared_buttons_label_stays_readable_in_every_palette(void) {
  for (int pal = PALETTE_DAY; pal <= PALETTE_NIGHT; pal++) {
    ui_theme_set_current((palette_t)pal);
    wide();
    declare(EM_MOB);
    lv_obj_t *btn = part("btn_mob"), *label = lv_obj_get_child(btn, 1);   // child 0 is the hold fill
    lv_color_t fill = lv_obj_get_style_bg_color(btn, LV_PART_MAIN), text = lv_obj_get_style_text_color(label, LV_PART_MAIN);
    TEST_ASSERT_FALSE_MESSAGE(lv_color_eq(fill, text), "declared button: label differs from fill");
    lv_obj_t *other = lv_obj_get_child(part("btn_fire"), 1);
    TEST_ASSERT_FALSE_MESSAGE(lv_color_eq(lv_obj_get_style_bg_color(part("btn_fire"), LV_PART_MAIN), lv_obj_get_style_text_color(other, LV_PART_MAIN)),
                              "idle button: label differs from fill");
    em_reset();
  }
  ui_theme_set_current(PALETTE_DAY);
}

static void test_send_status_text(void) {
  wide();
  em_press(EM_MOB, true, 41.5, -70.25, T, 1700000000);
  refresh();
  TEST_ASSERT_NOT_NULL(strstr(label("send"), "SENDING"));
  em_set_send(EM_SENT);
  refresh();
  TEST_ASSERT_EQUAL_STRING("SENT", label("send"));
  em_set_send(EM_NOT_SENT);
  refresh();
  TEST_ASSERT_NOT_NULL(strstr(label("send"), "NOT SENT"));
  TEST_ASSERT_NOT_NULL(strstr(label("send"), "TELL HELM"));
}

static void test_builds_on_the_square_board(void) {
  build(SIZE_SQUARE, 480, 432);
  for (size_t i = 0; i < 5; i++) TEST_ASSERT_TRUE(shown(NATURE_BTNS[i]));
  em_press(EM_FIRE, false, 0, 0, T, 0);
  refresh();
  lv_refr_now(NULL);
  TEST_ASSERT_TRUE(shown("btn_cancel"));
}

int main(void) {
  lv_init();
  lv_tick_set_cb(fake_tick);
  lv_display_t *d = lv_display_create(1024, 480);
  lv_display_set_flush_cb(d, flush);
  lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  scr = lv_screen_active();
  UNITY_BEGIN();
  RUN_TEST(test_buttons_hidden_when_signalk_down);
  RUN_TEST(test_buttons_shown_when_available);
  RUN_TEST(test_mob_fires_after_1s_hold_and_only_once);
  RUN_TEST(test_release_before_1s_sends_nothing);
  RUN_TEST(test_leaving_the_button_cancels_the_hold);
  RUN_TEST(test_every_nature_needs_the_hold_and_a_tap_does_nothing);
  RUN_TEST(test_cancel_needs_the_hold_and_shows_only_when_active);
  RUN_TEST(test_refresh_keeps_a_hold_in_progress);
  RUN_TEST(test_script_reads_the_call_for_the_declared_nature);
  RUN_TEST(test_steps_tell_a_beginner_how_to_use_the_radio);
  RUN_TEST(test_nothing_declared_says_so_and_hides_the_steps);
  RUN_TEST(test_no_fix_is_said_in_the_call_not_guessed);
  RUN_TEST(test_unknown_name_is_bracketed_not_guessed);
  RUN_TEST(test_footer_keeps_the_numbers_for_the_plotter_and_the_radio);
  RUN_TEST(test_the_longest_call_fits_on_the_wide_board);
  RUN_TEST(test_the_longest_call_fits_on_the_square_board);
  RUN_TEST(test_incoming_notification_preselects_the_nature);
  RUN_TEST(test_the_declared_buttons_label_stays_readable_in_every_palette);
  RUN_TEST(test_send_status_text);
  RUN_TEST(test_builds_on_the_square_board);
  return UNITY_END();
}
