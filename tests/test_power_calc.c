#include <math.h>
#include <string.h>
#include "unity.h"
#include "power_calc.h"

#define NOW 100000

static bd_sample_t num(double v, int64_t age_ms) {
  return (bd_sample_t){.kind = BD_NUMBER, .num = v, .valid = true, .rx_ms = NOW - age_ms, .source = 1};
}
static bd_sample_t bad(void) { bd_sample_t s = num(0, 0); s.valid = false; return s; }

void setUp(void) {}
void tearDown(void) {}

static void test_in_sums_positive_sources(void) {
  bd_sample_t p = num(300, 0), v = num(13.6, 0), i = num(10, 0), b = num(-50, 0);
  power_src_t src[2] = {{.p = &p}, {.v = &v, .i = &i}};
  power_src_t batt = {.p = &b};
  power_summary_t r = power_summarize(src, 2, &batt, NULL, NULL, 0, NOW);
  TEST_ASSERT_TRUE(r.in_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 436, r.in_w);
}

static void test_negative_charger_counts_as_load(void) {
  // Negative contributions are already inside the battery's net: excluded from IN, not added to load.
  bd_sample_t p = num(200, 0), v = num(13, 0), i = num(-20, 0), b = num(-100, 0);
  power_src_t src[2] = {{.p = &p}, {.v = &v, .i = &i}};
  power_src_t batt = {.p = &b};
  power_summary_t r = power_summarize(src, 2, &batt, NULL, NULL, 0, NOW);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 200, r.in_w);
  TEST_ASSERT_TRUE(r.load_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 300, r.load_w);
}

static void test_invalid_source_fails_in_and_load(void) {
  bd_sample_t p = num(300, 0), q = bad(), b = num(-50, 0);
  power_src_t src[2] = {{.p = &p}, {.p = &q}};
  power_src_t batt = {.p = &b};
  power_summary_t r = power_summarize(src, 2, &batt, NULL, NULL, 0, NOW);
  TEST_ASSERT_FALSE(r.in_ok);
  TEST_ASSERT_FALSE(r.load_ok);
  TEST_ASSERT_TRUE(r.batt_ok);
}

static void test_zero_source_is_valid(void) {
  bd_sample_t p = num(0, 0), b = num(0, 0);
  power_src_t src[1] = {{.p = &p}};
  power_src_t batt = {.p = &b};
  power_summary_t r = power_summarize(src, 1, &batt, NULL, NULL, 0, NOW);
  TEST_ASSERT_TRUE(r.in_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0, r.in_w);
}

static void test_stale_source_fails_with_max_age(void) {
  bd_sample_t p = num(300, 20000), b = num(-50, 0);
  power_src_t src[1] = {{.p = &p}};
  power_src_t batt = {.p = &b};
  TEST_ASSERT_FALSE(power_summarize(src, 1, &batt, NULL, NULL, 15000, NOW).in_ok);
  TEST_ASSERT_TRUE(power_summarize(src, 1, &batt, NULL, NULL, 0, NOW).in_ok);
}

static void test_dc_system_preferred(void) {
  bd_sample_t p = num(300, 0), b = num(-50, 0), d = num(180, 0);
  power_src_t src[1] = {{.p = &p}};
  power_src_t batt = {.p = &b};
  power_summary_t r = power_summarize(src, 1, &batt, NULL, &d, 0, NOW);
  TEST_ASSERT_TRUE(r.load_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 180, r.load_w);
  d = bad();
  r = power_summarize(src, 1, &batt, NULL, &d, 0, NOW);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 350, r.load_w);
}

static void test_batt_invalid_fails_batt_load_and_soc(void) {
  bd_sample_t p = num(300, 0), b = bad(), s = num(0.8, 0);
  power_src_t src[1] = {{.p = &p}};
  power_src_t batt = {.p = &b};
  power_summary_t r = power_summarize(src, 1, &batt, &s, NULL, 0, NOW);
  TEST_ASSERT_FALSE(r.batt_ok);
  TEST_ASSERT_FALSE(r.load_ok);
  TEST_ASSERT_TRUE(r.in_ok);
  TEST_ASSERT_FALSE(r.soc_ok);   // soc is the primary bank's: no bank, no soc
  bd_sample_t b2 = num(-50, 0), sbad = bad();
  batt.p = &b2;
  TEST_ASSERT_FALSE(power_summarize(src, 1, &batt, &sbad, NULL, 0, NOW).soc_ok);
  TEST_ASSERT_FALSE(power_summarize(src, 1, &batt, NULL, NULL, 0, NOW).soc_ok);
  r = power_summarize(src, 1, &batt, &s, NULL, 0, NOW);
  TEST_ASSERT_TRUE(r.soc_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.8, r.soc);
}

static void test_dc_system_stands_alone(void) {
  bd_sample_t p = num(300, 0), q = bad(), b = bad(), d = num(180, 0);
  power_src_t src[2] = {{.p = &p}, {.p = &q}};
  power_src_t batt = {.p = &b};
  power_summary_t r = power_summarize(src, 2, &batt, NULL, &d, 0, NOW);
  TEST_ASSERT_TRUE(r.load_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 180, r.load_w);
  TEST_ASSERT_FALSE(r.in_ok);
  TEST_ASSERT_FALSE(r.batt_ok);
}

static void test_no_sources_fails_in(void) {
  bd_sample_t b = num(-50, 0);
  power_src_t batt = {.p = &b};
  power_summary_t r = power_summarize(NULL, 0, &batt, NULL, NULL, 0, NOW);
  TEST_ASSERT_FALSE(r.in_ok);
  TEST_ASSERT_FALSE(r.load_ok);
}

static void test_primary_choice(void) {
  const char ids[2][24] = {"256", "279"};
  bool bound[2] = {false, true};
  TEST_ASSERT_EQUAL(1, power_primary(ids, bound, 2, ""));
  TEST_ASSERT_EQUAL(0, power_primary(ids, bound, 2, "256"));
  TEST_ASSERT_EQUAL(1, power_primary(ids, bound, 2, "999"));
  bool none[2] = {false, false};
  TEST_ASSERT_EQUAL(0, power_primary(ids, none, 2, ""));
}

static void test_ttg_null_while_charging(void) {
  bd_sample_t t = bad(), c = num(5, 0), cn = num(-5, 0), ci = bad(), tv = num(7200, 0);
  TEST_ASSERT_EQUAL(TTG_CHARGING, power_time_remaining(&t, &c, 0, NOW));
  TEST_ASSERT_EQUAL(TTG_FAIL, power_time_remaining(&t, &cn, 0, NOW));
  TEST_ASSERT_EQUAL(TTG_FAIL, power_time_remaining(&t, &ci, 0, NOW));
  TEST_ASSERT_EQUAL(TTG_VALUE, power_time_remaining(&tv, &c, 0, NOW));
  TEST_ASSERT_EQUAL(TTG_FAIL, power_time_remaining(NULL, NULL, 0, NOW));
  bd_sample_t c0 = num(0, 0);
  TEST_ASSERT_EQUAL(TTG_CHARGING, power_time_remaining(&t, &c0, 0, NOW));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_in_sums_positive_sources);
  RUN_TEST(test_negative_charger_counts_as_load);
  RUN_TEST(test_invalid_source_fails_in_and_load);
  RUN_TEST(test_zero_source_is_valid);
  RUN_TEST(test_stale_source_fails_with_max_age);
  RUN_TEST(test_dc_system_preferred);
  RUN_TEST(test_batt_invalid_fails_batt_load_and_soc);
  RUN_TEST(test_primary_choice);
  RUN_TEST(test_ttg_null_while_charging);
  return UNITY_END();
}
