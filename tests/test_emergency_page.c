// The emergency script page (Navigation page 2): the VHF distress call, one thing to say per line, in the largest text that fits.
// There are no buttons here: an emergency is declared from the quick panel.
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

static uint8_t buf[1024 * 48 * 2];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  lv_display_flush_ready(d);
}
static lv_obj_t *scr;

static char vname[32];
static const char *a_name(void) { return vname; }

void setUp(void) {
  bd_init();
  hist_init();
  alerts_init();
  em_reset();
  strcpy(vname, "SALOON");
  emergency_page_set_vessel_name(a_name);
  ui_theme_set_current(PALETTE_DAY);
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
static void square(void) { build(SIZE_SQUARE, 480, 432); }

static lv_obj_t *part(const char *n) {
  lv_obj_t *o = emergency_page_part(n);
  TEST_ASSERT_NOT_NULL_MESSAGE(o, n);
  return o;
}
static bool shown(const char *n) { return !lv_obj_has_flag(part(n), LV_OBJ_FLAG_HIDDEN); }
static const char *label(const char *n) { return lv_label_get_text(part(n)); }
static void refresh(void) { emergency_page_refresh(&binding, T); }
static int32_t line_height(const char *n) { return lv_font_get_line_height(lv_obj_get_style_text_font(part(n), LV_PART_MAIN)); }

static void own_position(double lat, double lon, int64_t age_ms) { bd_set_position(bd_intern("navigation.position"), lat, lon, 1, T - age_ms); }

static void declare(em_nature_t n) {
  em_press(n, true, 48.5402833, -123.1766667, T, 1700000000);
  refresh();
}

static void test_the_page_has_no_buttons(void) {
  wide();
  for (const char *const *n = (const char *const[]){"btn_mob", "btn_fire", "btn_flood", "btn_sinking", "btn_other", "btn_cancel", "hint", "footer", NULL}; *n; n++)
    TEST_ASSERT_NULL_MESSAGE(emergency_page_part(*n), *n);
  uint32_t buttons = 0;
  lv_obj_t *area = lv_obj_get_child(scr, 0);
  for (uint32_t i = 0; i < lv_obj_get_child_count(area); i++)
    if (lv_obj_check_type(lv_obj_get_child(area, i), &lv_button_class)) buttons++;
  TEST_ASSERT_EQUAL_UINT32(0, buttons);
}

static void test_the_call_is_one_thing_to_say_per_line(void) {
  own_position(48.5402833, -123.1766667, 0);
  wide();
  declare(EM_MOB);
  TEST_ASSERT_EQUAL_STRING(
      "MAYDAY MAYDAY MAYDAY\n"
      "This is SALOON SALOON SALOON\n"
      "Position four eight degrees three two decimal four two minutes north\n"
      "one two three degrees one zero decimal six zero minutes west\n"
      "We have a man overboard. Over.",
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

static void test_nothing_declared_points_at_the_quick_panel(void) {
  wide();
  TEST_ASSERT_NOT_NULL(strstr(label("script"), "No emergency declared"));
  TEST_ASSERT_NOT_NULL(strstr(label("script"), "top strip"));
  TEST_ASSERT_NOT_NULL(strstr(label("script"), "MOB, FIRE or SINKING"));
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

static void test_the_send_status_is_for_this_displays_own_press_only(void) {
  wide();
  TEST_ASSERT_EQUAL_STRING("", label("send"));
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
  em_reset();
  em_on_notification(EM_FIRE, true, false, 0, 0, NULL, T);   // raised elsewhere: nothing to say about sending
  refresh();
  TEST_ASSERT_EQUAL_STRING("", label("send"));
  TEST_ASSERT_NULL(strstr(label("send"), "RECEIVED"));
}

// Nothing on the page is smaller than 20 pt, and the call is as big as it can be.
static void test_the_type_is_big(void) {
  own_position(48.5402833, -123.1766667, 0);
  wide();
  declare(EM_MOB);
  TEST_ASSERT_GREATER_OR_EQUAL_INT32(lv_font_get_line_height(&lv_font_montserrat_20), line_height("steps"));   // steps are at least 20 pt
  TEST_ASSERT_GREATER_OR_EQUAL_INT32(lv_font_get_line_height(&lv_font_montserrat_20), line_height("send"));
  TEST_ASSERT_GREATER_OR_EQUAL_INT32_MESSAGE(lv_font_get_line_height(&lv_font_montserrat_36), line_height("script"), "wide: the call is 36 pt or larger");
  square();
  declare(EM_MOB);
  TEST_ASSERT_GREATER_OR_EQUAL_INT32(lv_font_get_line_height(&lv_font_montserrat_20), line_height("steps"));
  TEST_ASSERT_GREATER_OR_EQUAL_INT32_MESSAGE(lv_font_get_line_height(&lv_font_montserrat_24), line_height("script"), "square: 24 pt or larger");
}

// The longest name: the text shrinks to fit, but never below 20 pt, and nothing runs off the bottom (pages must not scroll).
static void fits(size_class_t sc, int32_t w, int32_t h) {
  memset(vname, 'W', 31);
  vname[31] = 0;
  own_position(-123.456789, -123.456789, 0);
  build(sc, w, h);
  declare(EM_MOB);
  lv_obj_update_layout(scr);
  lv_obj_t *script = part("script");
  TEST_ASSERT_GREATER_THAN_INT32(0, lv_obj_get_height(script));
  TEST_ASSERT_LESS_OR_EQUAL_INT32_MESSAGE(h, lv_obj_get_y(script) + lv_obj_get_height(script), "the call ends on the page");
  TEST_ASSERT_GREATER_OR_EQUAL_INT32_MESSAGE(lv_font_get_line_height(&lv_font_montserrat_20), line_height("script"), "never below 20 pt");
}

static void test_the_longest_call_fits_on_the_wide_board(void) { fits(SIZE_WIDE, 800, 440); }
static void test_the_longest_call_fits_on_the_square_board(void) { fits(SIZE_SQUARE, 480, 432); }
static void test_the_longest_call_fits_on_the_1024_board(void) { fits(SIZE_WIDE, 1024, 560); }

static void test_the_page_follows_a_declared_emergency_and_its_clearing(void) {
  wide();
  em_on_notification(EM_FIRE, true, false, 0, 0, "f-1", T);   // raised on another display
  refresh();
  TEST_ASSERT_TRUE(shown("steps"));
  TEST_ASSERT_NOT_NULL(strstr(label("script"), "fire on board"));
  em_on_notification(EM_FIRE, false, false, 0, 0, "f-1", T + 1);
  refresh();
  TEST_ASSERT_FALSE(shown("steps"));
  TEST_ASSERT_NOT_NULL(strstr(label("script"), "No emergency declared"));
}

int main(void) {
  lv_init();
  lv_display_t *d = lv_display_create(1024, 600);
  lv_display_set_flush_cb(d, flush);
  lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  scr = lv_screen_active();
  UNITY_BEGIN();
  RUN_TEST(test_the_page_has_no_buttons);
  RUN_TEST(test_the_call_is_one_thing_to_say_per_line);
  RUN_TEST(test_steps_tell_a_beginner_how_to_use_the_radio);
  RUN_TEST(test_nothing_declared_points_at_the_quick_panel);
  RUN_TEST(test_no_fix_is_said_in_the_call_not_guessed);
  RUN_TEST(test_unknown_name_is_bracketed_not_guessed);
  RUN_TEST(test_the_send_status_is_for_this_displays_own_press_only);
  RUN_TEST(test_the_type_is_big);
  RUN_TEST(test_the_longest_call_fits_on_the_wide_board);
  RUN_TEST(test_the_longest_call_fits_on_the_square_board);
  RUN_TEST(test_the_longest_call_fits_on_the_1024_board);
  RUN_TEST(test_the_page_follows_a_declared_emergency_and_its_clearing);
  return UNITY_END();
}
