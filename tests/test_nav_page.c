#include <math.h>
#include <string.h>
#include "alerts.h"
#include "boatdata.h"
#include "config.h"
#include "emergency.h"
#include "history.h"
#include "navigation_page.h"
#include "platform.h"
#include "role.h"
#include "unity.h"

#define T 100000LL

static uint8_t buf[1024 * 48 * 2];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  lv_display_flush_ready(d);
}
static lv_obj_t *scr;
static cfg_t cfg;

void setUp(void) {
  bd_init();
  hist_init();
  alerts_init();
  em_reset();
  cfg_defaults(&cfg);
}
void tearDown(void) {}

static void num(const char *path, double v) { bd_set_number(bd_intern(path), v, 1, T); }
static void txt(const char *path, const char *v) { bd_set_text(bd_intern(path), v, 1, T); }

// The scenario of the spec: heading 047, COG and DTK 070, 0.12 nm right of track, AP at 052, apparent 38 deg 14.2 kn,
// 6.4 kn through the water, server true wind 61 deg 10.0 kn, set 137 drift 1.2 kn.
static void full(void) {
  num("navigation.headingTrue", 0.8203);
  num("navigation.courseOverGroundTrue", 1.2217);
  num("navigation.speedOverGround", 3.292);
  num("navigation.speedThroughWater", 3.292);
  num("environment.wind.angleApparent", 0.6632);
  num("environment.wind.speedApparent", 7.305);
  num("environment.wind.angleTrueWater", 1.0696);
  num("environment.wind.speedTrue", 5.14);
  num("navigation.course.calcValues.bearingTrackTrue", 1.2217);
  num("navigation.course.calcValues.crossTrackError", 222.2);
  num("navigation.course.calcValues.distance", 13700);
  txt("steering.autopilot.state", "auto");
  num("steering.autopilot.target.headingTrue", 0.9076);
  num("environment.current.setTrue", 2.391);
  num("environment.current.drift", 0.617);
}

static void build(size_class_t sc, int32_t w, int32_t h) {
  lv_obj_clean(scr);
  lv_obj_t *area = lv_obj_create(scr);
  lv_obj_set_size(area, w, h);
  role_binding_t b = {0};
  b.units = &cfg.units;
  navigation_build_page(area, 0, sc, &b);
  navigation_refresh(&b, T);
  lv_refr_now(NULL);
}
static void wide(void) { build(SIZE_WIDE, 800, 440); }

static lv_obj_t *part(const char *n) {
  lv_obj_t *o = navigation_page_part(n);
  TEST_ASSERT_NOT_NULL_MESSAGE(o, n);
  return o;
}
static bool shown(const char *n) { return !lv_obj_has_flag(part(n), LV_OBJ_FLAG_HIDDEN); }
static const char *text(const char *n) { return ui_readout_text(part(n)); }

static void test_full_data_shows_everything(void) {
  full();
  wide();
  static const char *const all[] = {"rose", "heading", "ap_badge", "awa", "aws", "twa", "tws", "sog", "course", "xte",
                                    "dtw", "ap_hdg", "set_drift"};
  for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) TEST_ASSERT_TRUE_MESSAGE(shown(all[i]), all[i]);
  TEST_ASSERT_EQUAL_STRING("047\xC2\xB0", text("heading"));
  TEST_ASSERT_EQUAL_STRING("38\xC2\xB0 STBD", text("awa"));
  TEST_ASSERT_EQUAL_STRING("14.2 kn", text("aws"));
  TEST_ASSERT_EQUAL_STRING("61\xC2\xB0 STBD", text("twa"));
  TEST_ASSERT_EQUAL_STRING("10.0 kn", text("tws"));
  TEST_ASSERT_EQUAL_STRING("6.4 kn", text("sog"));
  TEST_ASSERT_EQUAL_STRING("070\xC2\xB0", text("course"));
  TEST_ASSERT_EQUAL_STRING("0.12 nm R", text("xte"));
  TEST_ASSERT_EQUAL_STRING("7.4 nm", text("dtw"));
  TEST_ASSERT_EQUAL_STRING("052\xC2\xB0", text("ap_hdg"));
  TEST_ASSERT_EQUAL_STRING("SET 137\xC2\xB0 DRIFT 1.2 kn", lv_label_get_text(part("set_drift")));
  const ui_rose_data_t *r = navigation_page_rose_data();
  TEST_ASSERT_TRUE(r->have_heading && r->course && r->have_cog && r->ap_bug && r->have_awa && r->have_twa);
}

static void test_no_course_hides_course_boxes_not_cog(void) {
  full();
  bd_set_invalid(bd_find("navigation.course.calcValues.bearingTrackTrue"));
  wide();
  TEST_ASSERT_FALSE(shown("course"));
  TEST_ASSERT_FALSE(shown("xte"));
  TEST_ASSERT_FALSE(shown("dtw"));
  TEST_ASSERT_FALSE(navigation_page_rose_data()->course);
  TEST_ASSERT_TRUE(navigation_page_rose_data()->have_cog);
}

static void test_no_ap_hides_badge_and_ap_box(void) {
  full();
  txt("steering.autopilot.state", "standby");
  wide();
  TEST_ASSERT_FALSE(shown("ap_badge"));
  TEST_ASSERT_FALSE(shown("ap_hdg"));
  TEST_ASSERT_FALSE(navigation_page_rose_data()->ap_bug);
}

static void test_ap_engaged_without_target_shows_badge_only(void) {
  full();
  bd_set_invalid(bd_find("steering.autopilot.target.headingTrue"));
  wide();
  TEST_ASSERT_TRUE(shown("ap_badge"));
  TEST_ASSERT_FALSE(shown("ap_hdg"));
  TEST_ASSERT_FALSE(navigation_page_rose_data()->ap_bug);
}

static void test_true_wind_hidden_without_source_or_stw(void) {
  full();
  bd_set_invalid(bd_find("environment.wind.angleTrueWater"));
  bd_set_invalid(bd_find("navigation.speedThroughWater"));
  wide();
  TEST_ASSERT_FALSE(shown("twa"));
  TEST_ASSERT_FALSE(shown("tws"));
  TEST_ASSERT_FALSE(navigation_page_rose_data()->have_twa);
}

static void test_true_wind_computed_when_the_server_gives_none(void) {
  full();
  bd_set_invalid(bd_find("environment.wind.angleTrueWater"));
  wide();
  TEST_ASSERT_TRUE(shown("twa"));
  TEST_ASSERT_EQUAL_STRING("61\xC2\xB0 STBD", text("twa"));   // the spec's check vector
  TEST_ASSERT_EQUAL_STRING("10.0 kn", text("tws"));
}

static void test_set_drift_hidden_when_absent(void) {
  full();
  bd_set_invalid(bd_find("environment.current.drift"));
  wide();
  TEST_ASSERT_FALSE(shown("set_drift"));
}

static void test_no_heading_flags_rose_keeps_wind(void) {
  full();
  bd_set_invalid(bd_find("navigation.headingTrue"));
  wide();
  TEST_ASSERT_TRUE(ui_field_faulted(part("heading")));
  TEST_ASSERT_EQUAL_STRING("HDG FAIL", ui_field_fault_text(part("heading")));
  const ui_rose_data_t *r = navigation_page_rose_data();
  TEST_ASSERT_FALSE(r->have_heading);
  TEST_ASSERT_TRUE(r->have_awa);
  TEST_ASSERT_TRUE(r->have_twa);
  TEST_ASSERT_FALSE(ui_field_faulted(part("awa")));
  TEST_ASSERT_EQUAL_STRING("38\xC2\xB0 STBD", text("awa"));
}

static void test_apparent_wind_failure_flags_awa(void) {
  full();
  bd_set_invalid(bd_find("environment.wind.angleApparent"));
  wide();
  TEST_ASSERT_TRUE(ui_field_faulted(part("awa")));
  TEST_ASSERT_EQUAL_STRING("AWA FAIL", ui_field_fault_text(part("awa")));
  TEST_ASSERT_FALSE(navigation_page_rose_data()->have_awa);
  TEST_ASSERT_FALSE(ui_field_faulted(part("aws")));   // the speed is still good
}

static void test_sog_failure_flags_sog(void) {
  full();
  bd_set_invalid(bd_find("navigation.speedOverGround"));
  wide();
  TEST_ASSERT_TRUE(ui_field_faulted(part("sog")));
  TEST_ASSERT_EQUAL_STRING("SOG FAIL", ui_field_fault_text(part("sog")));
}

static void test_port_wind_reads_PORT(void) {
  full();
  num("environment.wind.angleApparent", -0.6632);
  wide();
  TEST_ASSERT_EQUAL_STRING("38\xC2\xB0 PORT", text("awa"));
  TEST_ASSERT_TRUE(navigation_page_rose_data()->awa < 0);
}

static void test_cross_track_left_reads_L(void) {
  full();
  num("navigation.course.calcValues.crossTrackError", -222.2);
  wide();
  TEST_ASSERT_EQUAL_STRING("0.12 nm L", text("xte"));
}

static void test_units_follow_config(void) {
  full();
  strcpy(cfg.units.speed, "m/s");
  strcpy(cfg.units.distance, "km");
  wide();
  TEST_ASSERT_EQUAL_STRING("7.3 m/s", text("aws"));
  TEST_ASSERT_EQUAL_STRING("0.22 km R", text("xte"));
  TEST_ASSERT_EQUAL_STRING("13.7 km", text("dtw"));
}

static void test_square_board_drops_dtw(void) {
  full();
  build(SIZE_SQUARE, 480, 444);
  TEST_ASSERT_FALSE(shown("dtw"));
  TEST_ASSERT_TRUE(shown("course"));
  TEST_ASSERT_TRUE(shown("xte"));
}

static void test_page_builds_in_every_size_with_no_data(void) {
  const struct { size_class_t sc; int32_t w, h; } sizes[] = {{SIZE_SQUARE, 480, 444}, {SIZE_WIDE, 800, 440}, {SIZE_WIDE, 1024, 560}};
  for (size_t i = 0; i < 3; i++) {
    setUp();
    build(sizes[i].sc, sizes[i].w, sizes[i].h);   // nothing known: every required field flags, optional ones hide
    TEST_ASSERT_TRUE(ui_field_faulted(part("awa")));
    TEST_ASSERT_TRUE(ui_field_faulted(part("sog")));
    TEST_ASSERT_TRUE(ui_field_faulted(part("heading")));
    TEST_ASSERT_FALSE(shown("course"));
    TEST_ASSERT_FALSE(shown("ap_badge"));
    full();
    build(sizes[i].sc, sizes[i].w, sizes[i].h);
    TEST_ASSERT_FALSE(ui_field_faulted(part("awa")));
  }
}

static void own_position(double lat, double lon) { bd_set_position(bd_intern("navigation.position"), lat, lon, 1, T); }

static void test_position_shown_in_degrees_and_minutes(void) {
  full();
  own_position(41.5402833, -70.25);
  wide();
  TEST_ASSERT_TRUE(shown("position"));
  TEST_ASSERT_EQUAL_STRING("41\xC2\xB0" "32.417'N\n070\xC2\xB0" "15.000'W", lv_label_get_text(part("position")));
}

static void test_position_flagged_when_there_is_no_fix(void) {
  full();
  wide();
  TEST_ASSERT_TRUE(shown("position"));
  TEST_ASSERT_EQUAL_STRING("NO POSITION", lv_label_get_text(part("position")));
}

static void test_mob_target_shows_bearing_distance_and_age(void) {
  full();
  own_position(41.5, -70.25);
  em_press(EM_MOB, true, 41.6, -70.25, T - 125000, 1700000000);   // marked 2 min ago, due north of us
  wide();
  TEST_ASSERT_TRUE(shown("mob"));
  TEST_ASSERT_FALSE(ui_field_faulted(part("mob")));
  TEST_ASSERT_EQUAL_STRING("000\xC2\xB0 6.0 nm", text("mob"));
  TEST_ASSERT_EQUAL_STRING("MARKED 2 min AGO", lv_label_get_text(part("mob_age")));
  const ui_rose_data_t *r = navigation_page_rose_data();
  TEST_ASSERT_TRUE(r->have_mob);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.0, r->mob);
}

static void test_mob_without_a_position_shows_no_position_and_no_arrow(void) {
  full();
  own_position(41.5, -70.25);
  em_on_notification(EM_MOB, true, false, 0, 0, T);   // raised by something else, no position in it
  wide();
  TEST_ASSERT_TRUE(shown("mob"));
  TEST_ASSERT_TRUE(ui_field_faulted(part("mob")));
  TEST_ASSERT_EQUAL_STRING("NO POSITION", ui_field_fault_text(part("mob")));
  TEST_ASSERT_EQUAL_STRING("MOB POSITION UNKNOWN", lv_label_get_text(part("mob_age")));
  TEST_ASSERT_FALSE(navigation_page_rose_data()->have_mob);
}

static void test_mob_target_flagged_when_our_own_fix_is_lost(void) {
  full();
  em_press(EM_MOB, true, 41.6, -70.25, T, 1700000000);   // no own position at all
  wide();
  TEST_ASSERT_TRUE(ui_field_faulted(part("mob")));
  TEST_ASSERT_EQUAL_STRING("OWN POSITION LOST", lv_label_get_text(part("mob_age")));
  TEST_ASSERT_FALSE(navigation_page_rose_data()->have_mob);
}

static void test_no_mob_target_without_a_mob(void) {
  full();
  own_position(41.5, -70.25);
  wide();
  TEST_ASSERT_FALSE(shown("mob"));
  TEST_ASSERT_FALSE(shown("mob_age"));
  em_press(EM_FIRE, false, 0, 0, T, 0);
  wide();
  TEST_ASSERT_FALSE(shown("mob"));   // a fire is on the script page
  TEST_ASSERT_FALSE(navigation_page_rose_data()->have_mob);
}

static void test_mob_target_hidden_without_a_heading_but_text_stays(void) {
  full();
  bd_set_invalid(bd_find("navigation.headingTrue"));
  own_position(41.5, -70.25);
  em_press(EM_MOB, true, 41.6, -70.25, T, 1700000000);
  wide();
  TEST_ASSERT_FALSE(navigation_page_rose_data()->have_mob);   // the arrow is in the rose's frame: no heading, no arrow
  TEST_ASSERT_EQUAL_STRING("000\xC2\xB0 6.0 nm", text("mob"));
}

static void test_navigation_has_a_script_page(void) {
  TEST_ASSERT_EQUAL_UINT8(2, role_find("navigation")->page_count);
}

static void test_page_for_mob_is_the_main_page(void) {
  TEST_ASSERT_EQUAL_UINT8(0, role_find("navigation")->page_for(NULL, SIZE_WIDE, "mob"));
}

static void test_page_for_fire_flood_sinking_other_is_the_script(void) {
  const role_def_t *d = role_find("navigation");
  const char *ids[] = {"fire", "flooding", "sinking", "emergency"};
  for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
    TEST_ASSERT_EQUAL_UINT8(1, d->page_for(NULL, SIZE_WIDE, ids[i]));
    TEST_ASSERT_EQUAL_UINT8(1, d->page_for(NULL, SIZE_SQUARE, ids[i]));
  }
  TEST_ASSERT_EQUAL_UINT8(0, d->page_for(NULL, SIZE_WIDE, "something.else"));
}

int main(void) {
  lv_init();
  lv_display_t *disp = lv_display_create(1024, 600);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  scr = lv_screen_active();
  UNITY_BEGIN();
  RUN_TEST(test_full_data_shows_everything);
  RUN_TEST(test_no_course_hides_course_boxes_not_cog);
  RUN_TEST(test_no_ap_hides_badge_and_ap_box);
  RUN_TEST(test_ap_engaged_without_target_shows_badge_only);
  RUN_TEST(test_true_wind_hidden_without_source_or_stw);
  RUN_TEST(test_true_wind_computed_when_the_server_gives_none);
  RUN_TEST(test_set_drift_hidden_when_absent);
  RUN_TEST(test_no_heading_flags_rose_keeps_wind);
  RUN_TEST(test_apparent_wind_failure_flags_awa);
  RUN_TEST(test_sog_failure_flags_sog);
  RUN_TEST(test_port_wind_reads_PORT);
  RUN_TEST(test_cross_track_left_reads_L);
  RUN_TEST(test_units_follow_config);
  RUN_TEST(test_square_board_drops_dtw);
  RUN_TEST(test_page_builds_in_every_size_with_no_data);
  RUN_TEST(test_position_shown_in_degrees_and_minutes);
  RUN_TEST(test_position_flagged_when_there_is_no_fix);
  RUN_TEST(test_mob_target_shows_bearing_distance_and_age);
  RUN_TEST(test_mob_without_a_position_shows_no_position_and_no_arrow);
  RUN_TEST(test_mob_target_flagged_when_our_own_fix_is_lost);
  RUN_TEST(test_no_mob_target_without_a_mob);
  RUN_TEST(test_mob_target_hidden_without_a_heading_but_text_stays);
  RUN_TEST(test_navigation_has_a_script_page);
  RUN_TEST(test_page_for_mob_is_the_main_page);
  RUN_TEST(test_page_for_fire_flood_sinking_other_is_the_script);
  return UNITY_END();
}
