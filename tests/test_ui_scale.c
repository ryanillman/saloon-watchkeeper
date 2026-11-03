#include "unity.h"
#include "ui_scale.h"

void setUp(void) {}
void tearDown(void) {}

static void expect(double lo, double hi, const double *want, int n) {
  ui_ticks_t t;
  ui_scale_ticks(lo, hi, 5, &t);
  TEST_ASSERT_EQUAL_UINT8(n, t.major_count);
  for (int i = 0; i < n; i++) TEST_ASSERT_DOUBLE_WITHIN(1e-9, want[i], t.major[i]);
}

static void test_ticks_0_4000(void) { double w[] = {0, 1000, 2000, 3000, 4000}; expect(0, 4000, w, 5); }
static void test_ticks_40_120(void) { double w[] = {40, 60, 80, 100, 120}; expect(40, 120, w, 5); }
static void test_ticks_10_16(void) { double w[] = {10, 12, 14, 16}; expect(10, 16, w, 4); }
static void test_ticks_0_3600_stay_inside(void) {
  double w[] = {0, 1000, 2000, 3000};   // 3600 is the end of the scale but not a major
  expect(0, 3600, w, 4);
}
static void test_ticks_0_6(void) { double w[] = {0, 2, 4, 6}; expect(0, 6, w, 4); }
static void test_minor_step(void) {
  ui_ticks_t t;
  ui_scale_ticks(0, 4000, 5, &t);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 200, t.minor_step);   // five minors per major
}
static void test_fraction_clamps(void) {
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, ui_scale_fraction(-5, 0, 4000));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, ui_scale_fraction(5000, 0, 4000));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.5, ui_scale_fraction(2000, 0, 4000));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, ui_scale_fraction(5, 3, 3));   // empty range
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ticks_0_4000);
  RUN_TEST(test_ticks_40_120);
  RUN_TEST(test_ticks_10_16);
  RUN_TEST(test_ticks_0_3600_stay_inside);
  RUN_TEST(test_ticks_0_6);
  RUN_TEST(test_minor_step);
  RUN_TEST(test_fraction_clamps);
  return UNITY_END();
}
