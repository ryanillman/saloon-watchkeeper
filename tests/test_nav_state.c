#include <math.h>
#include <string.h>
#include "boatdata.h"
#include "history.h"
#include "nav_state.h"
#include "unity.h"

#define KN 0.514444
#define DEG (M_PI / 180.0)

#define NOW 1000000LL

static nav_ids_t ids;
static nav_view_t v;

void setUp(void) {
  bd_init();
  hist_init();
  nav_ids_bind(&ids);
}
void tearDown(void) {}

static void test_true_wind_spec_vector(void) {
  double twa, tws;
  TEST_ASSERT_TRUE(nav_true_wind(38 * DEG, 14.2 * KN, 6.4 * KN, &twa, &tws));
  TEST_ASSERT_DOUBLE_WITHIN(0.1 * DEG, 61.3 * DEG, twa);
  TEST_ASSERT_DOUBLE_WITHIN(0.05 * KN, 10.0 * KN, tws);
}

static void test_true_wind_stw_zero_equals_apparent(void) {
  double twa, tws;
  TEST_ASSERT_TRUE(nav_true_wind(0.7, 5.0, 0, &twa, &tws));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.7, twa);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 5.0, tws);
}

static void test_true_wind_dead_ahead(void) {
  double twa, tws;
  TEST_ASSERT_TRUE(nav_true_wind(0, 8.0, 3.0, &twa, &tws));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0, twa);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 5.0, tws);   // S - B
}

static void test_true_wind_from_astern_boat_faster(void) {
  // Apparent wind from dead astern (pi) at 2 m/s while the boat does 5 m/s: x = S cos(pi) - B = -7, y = 0,
  // so the true wind is 7 m/s from astern too.
  double twa, tws;
  TEST_ASSERT_TRUE(nav_true_wind(M_PI, 2.0, 5.0, &twa, &tws));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 7.0, tws);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, M_PI, fabs(twa));
}

static void test_true_wind_port_is_mirror_of_starboard(void) {
  double a, s1, b, s2;
  TEST_ASSERT_TRUE(nav_true_wind(38 * DEG, 7.0, 3.0, &a, &s1));
  TEST_ASSERT_TRUE(nav_true_wind(-38 * DEG, 7.0, 3.0, &b, &s2));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, -a, b);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, s1, s2);
}

static void test_true_wind_zero_zero(void) {
  double twa = 9, tws = 9;
  TEST_ASSERT_TRUE(nav_true_wind(0.4, 0, 0, &twa, &tws));
  TEST_ASSERT_FALSE(isnan(twa));
  TEST_ASSERT_EQUAL_DOUBLE(0, twa);
  TEST_ASSERT_EQUAL_DOUBLE(0, tws);
}

static void test_true_wind_rejects_nan_and_negative(void) {
  double twa = 7, tws = 7;
  TEST_ASSERT_FALSE(nav_true_wind(NAN, 5, 3, &twa, &tws));
  TEST_ASSERT_FALSE(nav_true_wind(0.5, NAN, 3, &twa, &tws));
  TEST_ASSERT_FALSE(nav_true_wind(0.5, 5, NAN, &twa, &tws));
  TEST_ASSERT_FALSE(nav_true_wind(0.5, -1, 3, &twa, &tws));
  TEST_ASSERT_FALSE(nav_true_wind(0.5, 5, -1, &twa, &tws));
  TEST_ASSERT_EQUAL_DOUBLE(7, twa);
  TEST_ASSERT_EQUAL_DOUBLE(7, tws);
}

static void num(const char *path, double val, int64_t at) { bd_set_number(bd_intern(path), val, 1, at); }
static void txt(const char *path, const char *val, int64_t at) { bd_set_text(bd_intern(path), val, 1, at); }
static void build(int64_t now) { nav_view_build(&ids, now, &v); }

static void test_heading_true_wins(void) {
  num("navigation.headingTrue", 1.0, NOW);
  num("navigation.headingMagnetic", 2.0, NOW);
  num("navigation.magneticVariation", 0.3, NOW);
  build(NOW);
  TEST_ASSERT_TRUE(v.have_heading);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, v.heading);
}

static void test_heading_from_magnetic_with_variation(void) {
  num("navigation.headingMagnetic", 0.8, NOW);
  num("navigation.magneticVariation", 0.25, NOW);
  build(NOW);
  TEST_ASSERT_TRUE(v.have_heading);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.05, v.heading);
}

static void test_magnetic_without_variation_is_no_heading(void) {
  num("navigation.headingMagnetic", 0.8, NOW);
  build(NOW);
  TEST_ASSERT_FALSE(v.have_heading);
  num("navigation.magneticVariation", 0.25, NOW - 61000);   // a variation a minute old is as good as none
  build(NOW);
  TEST_ASSERT_FALSE(v.have_heading);
}

static void test_heading_stale_after_5s(void) {
  num("navigation.headingTrue", 1.0, NOW);
  build(NOW + 5000);
  TEST_ASSERT_TRUE(v.have_heading);
  build(NOW + 5001);
  TEST_ASSERT_FALSE(v.have_heading);
}

static void test_angles_are_normalised(void) {
  num("navigation.headingTrue", -0.1, NOW);
  num("environment.wind.angleApparent", 4.0, NOW);
  num("navigation.courseOverGroundTrue", 7.0, NOW);
  build(NOW);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 2 * M_PI - 0.1, v.heading);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 4.0 - 2 * M_PI, v.awa);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 7.0 - 2 * M_PI, v.cog);
}

static void test_server_true_wind_used_when_fresh(void) {
  num("environment.wind.angleApparent", 0.66, NOW);
  num("environment.wind.speedApparent", 7.0, NOW);
  num("navigation.speedThroughWater", 3.0, NOW);
  num("environment.wind.angleTrueWater", 1.0, NOW);
  num("environment.wind.speedTrue", 4.0, NOW);
  build(NOW);
  TEST_ASSERT_EQUAL(NAV_TW_SERVER, v.tw_src);
  TEST_ASSERT_EQUAL_DOUBLE(1.0, v.twa);
  TEST_ASSERT_EQUAL_DOUBLE(4.0, v.tws);
}

static void test_server_angle_without_speed_falls_to_computed(void) {
  num("environment.wind.angleApparent", 0.0, NOW);
  num("environment.wind.speedApparent", 8.0, NOW);
  num("navigation.speedThroughWater", 3.0, NOW);
  num("environment.wind.angleTrueWater", 1.0, NOW);   // no speedTrue: never mix the two sources
  build(NOW);
  TEST_ASSERT_EQUAL(NAV_TW_COMPUTED, v.tw_src);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, v.twa);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 5.0, v.tws);
}

static void test_server_true_wind_stale_falls_to_computed(void) {
  num("environment.wind.angleApparent", 0.0, NOW);
  num("environment.wind.speedApparent", 8.0, NOW);
  num("navigation.speedThroughWater", 3.0, NOW);
  num("environment.wind.angleTrueWater", 1.0, NOW - 6000);
  num("environment.wind.speedTrue", 4.0, NOW);
  build(NOW);
  TEST_ASSERT_EQUAL(NAV_TW_COMPUTED, v.tw_src);
}

static void test_computed_true_wind_needs_stw(void) {
  num("environment.wind.angleApparent", 0.6, NOW);
  num("environment.wind.speedApparent", 7.0, NOW);
  num("navigation.speedOverGround", 3.0, NOW);   // SOG is never substituted
  build(NOW);
  TEST_ASSERT_EQUAL(NAV_TW_NONE, v.tw_src);
  TEST_ASSERT_TRUE(v.have_sog);
  TEST_ASSERT_FALSE(v.have_stw);
  TEST_ASSERT_TRUE(v.have_awa);
  TEST_ASSERT_TRUE(v.have_aws);
}

static void test_course_needs_fresh_dtk(void) {
  num("navigation.course.calcValues.bearingTrackTrue", 1.2, NOW);
  num("navigation.course.calcValues.crossTrackError", 180.0, NOW);
  num("navigation.course.calcValues.distance", 9000.0, NOW);
  build(NOW + 10000);
  TEST_ASSERT_TRUE(v.have_course);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.2, v.dtk);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 9000.0, v.dtw);
  build(NOW + 10001);
  TEST_ASSERT_FALSE(v.have_course);
  TEST_ASSERT_TRUE(isnan(v.xte));
  TEST_ASSERT_TRUE(isnan(v.dtw));
}

static void test_course_with_stale_xte_keeps_the_line(void) {
  num("navigation.course.calcValues.bearingTrackTrue", 1.2, NOW);
  num("navigation.course.calcValues.crossTrackError", 180.0, NOW - 11000);
  build(NOW);
  TEST_ASSERT_TRUE(v.have_course);
  TEST_ASSERT_TRUE(isnan(v.xte));
  TEST_ASSERT_TRUE(isnan(v.dtw));
}

static void test_xte_positive_means_boat_right_of_track(void) {
  num("navigation.course.calcValues.bearingTrackTrue", 1.2, NOW);
  num("navigation.course.calcValues.crossTrackError", 180.0, NOW);
  build(NOW);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, NAV_XTE_RIGHT_POSITIVE ? 180.0 : -180.0, v.xte);
}

static void test_ap_engaged_states(void) {
  static const char *const on[] = {"auto", "wind", "route", "track", "Auto"};
  static const char *const off[] = {"standby", "off", "disabled", "", "Standby"};
  for (size_t i = 0; i < sizeof on / sizeof on[0]; i++) {
    txt("steering.autopilot.state", on[i], NOW);
    build(NOW);
    TEST_ASSERT_TRUE_MESSAGE(v.ap_engaged, on[i]);
  }
  for (size_t i = 0; i < sizeof off / sizeof off[0]; i++) {
    txt("steering.autopilot.state", off[i], NOW);
    build(NOW);
    TEST_ASSERT_FALSE_MESSAGE(v.ap_engaged, off[i]);
  }
}

static void test_ap_stale_state_is_not_engaged(void) {
  txt("steering.autopilot.state", "auto", NOW);
  num("steering.autopilot.target.headingTrue", 0.9, NOW);
  build(NOW + 10001);
  TEST_ASSERT_FALSE(v.ap_engaged);
  TEST_ASSERT_FALSE(v.have_ap_target);
}

static void test_ap_target_needs_engaged_and_fresh(void) {
  num("steering.autopilot.target.headingTrue", 0.9, NOW);
  txt("steering.autopilot.state", "standby", NOW);
  build(NOW);
  TEST_ASSERT_FALSE(v.have_ap_target);
  txt("steering.autopilot.state", "auto", NOW);
  build(NOW);
  TEST_ASSERT_TRUE(v.have_ap_target);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.9, v.ap_target);
  build(NOW + 1);
  num("steering.autopilot.target.headingTrue", 0.9, NOW - 11000);   // the target went stale, the state did not
  build(NOW);
  TEST_ASSERT_TRUE(v.ap_engaged);
  TEST_ASSERT_FALSE(v.have_ap_target);
}

static void test_ap_magnetic_target_converted_with_variation(void) {
  txt("steering.autopilot.state", "auto", NOW);
  num("steering.autopilot.target.headingMagnetic", 0.8, NOW);
  build(NOW);
  TEST_ASSERT_FALSE(v.have_ap_target);   // no variation: not drawn
  num("navigation.magneticVariation", 0.25, NOW);
  build(NOW);
  TEST_ASSERT_TRUE(v.have_ap_target);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.05, v.ap_target);
}

static void test_current_needs_both_members_and_30s(void) {
  num("environment.current.setTrue", 2.4, NOW);
  build(NOW);
  TEST_ASSERT_FALSE(v.have_current);
  num("environment.current.drift", 0.6, NOW);
  build(NOW + 30000);
  TEST_ASSERT_TRUE(v.have_current);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 2.4, v.set);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.6, v.drift);
  build(NOW + 30001);
  TEST_ASSERT_FALSE(v.have_current);
}

static void test_cog_valid_without_course(void) {
  num("navigation.courseOverGroundTrue", 1.2, NOW);
  build(NOW);
  TEST_ASSERT_TRUE(v.have_cog);
  TEST_ASSERT_FALSE(v.have_course);
}

static void test_sub_ids_cover_every_path(void) {
  bd_id_t out[32];
  size_t n = nav_sub_ids(&ids, out, 32);
  static const char *const need[] = {"environment.wind.angleApparent", "navigation.course.calcValues.bearingTrackTrue",
                                     "steering.autopilot.state", "environment.current"};
  for (size_t k = 0; k < sizeof need / sizeof need[0]; k++) {
    bool found = false;
    for (size_t i = 0; i < n; i++) found = found || out[i] == bd_find(need[k]);
    TEST_ASSERT_TRUE_MESSAGE(found, need[k]);
  }
  bd_id_t tiny[2];
  TEST_ASSERT_EQUAL(2, nav_sub_ids(&ids, tiny, 2));
}

static void test_position_fresh_and_stale(void) {
  bd_set_position(ids.position, 41.5, -70.25, 1, NOW);
  nav_view_build(&ids, NOW + 9000, &v);
  TEST_ASSERT_TRUE(v.have_pos);
  TEST_ASSERT_EQUAL_DOUBLE(41.5, v.lat);
  TEST_ASSERT_EQUAL_DOUBLE(-70.25, v.lon);
  nav_view_build(&ids, NOW + 11000, &v);   // an older fix is no fix
  TEST_ASSERT_FALSE(v.have_pos);
  bd_set_invalid(ids.position);
  nav_view_build(&ids, NOW, &v);
  TEST_ASSERT_FALSE(v.have_pos);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_true_wind_spec_vector);
  RUN_TEST(test_true_wind_stw_zero_equals_apparent);
  RUN_TEST(test_true_wind_dead_ahead);
  RUN_TEST(test_true_wind_from_astern_boat_faster);
  RUN_TEST(test_true_wind_port_is_mirror_of_starboard);
  RUN_TEST(test_true_wind_zero_zero);
  RUN_TEST(test_true_wind_rejects_nan_and_negative);
  RUN_TEST(test_heading_true_wins);
  RUN_TEST(test_heading_from_magnetic_with_variation);
  RUN_TEST(test_magnetic_without_variation_is_no_heading);
  RUN_TEST(test_heading_stale_after_5s);
  RUN_TEST(test_angles_are_normalised);
  RUN_TEST(test_server_true_wind_used_when_fresh);
  RUN_TEST(test_server_angle_without_speed_falls_to_computed);
  RUN_TEST(test_server_true_wind_stale_falls_to_computed);
  RUN_TEST(test_computed_true_wind_needs_stw);
  RUN_TEST(test_course_needs_fresh_dtk);
  RUN_TEST(test_course_with_stale_xte_keeps_the_line);
  RUN_TEST(test_xte_positive_means_boat_right_of_track);
  RUN_TEST(test_ap_engaged_states);
  RUN_TEST(test_ap_stale_state_is_not_engaged);
  RUN_TEST(test_ap_target_needs_engaged_and_fresh);
  RUN_TEST(test_ap_magnetic_target_converted_with_variation);
  RUN_TEST(test_current_needs_both_members_and_30s);
  RUN_TEST(test_cog_valid_without_course);
  RUN_TEST(test_sub_ids_cover_every_path);
  RUN_TEST(test_position_fresh_and_stale);
  return UNITY_END();
}
