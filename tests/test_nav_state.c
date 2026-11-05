#include <math.h>
#include "unity.h"
#include "nav_state.h"

#define KN 0.514444
#define DEG (M_PI / 180.0)

void setUp(void) {}
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

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_true_wind_spec_vector);
  RUN_TEST(test_true_wind_stw_zero_equals_apparent);
  RUN_TEST(test_true_wind_dead_ahead);
  RUN_TEST(test_true_wind_from_astern_boat_faster);
  RUN_TEST(test_true_wind_port_is_mirror_of_starboard);
  RUN_TEST(test_true_wind_zero_zero);
  RUN_TEST(test_true_wind_rejects_nan_and_negative);
  return UNITY_END();
}
