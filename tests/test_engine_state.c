#include <math.h>
#include <string.h>
#include "unity.h"
#include "engine_state.h"

#define NOW 100000

static bd_sample_t num(double v, int64_t age_ms) {
  bd_sample_t s = {.kind = BD_NUMBER, .num = v, .valid = isfinite(v), .rx_ms = NOW - age_ms, .source = 1};
  return s;
}
static bd_sample_t text(const char *t) {
  bd_sample_t s = {.kind = BD_TEXT, .valid = true, .rx_ms = NOW, .source = 1};
  strcpy(s.text, t);
  return s;
}
static bd_sample_t never(void) { return (bd_sample_t){0}; }

void setUp(void) {}
void tearDown(void) {}

static void test_nothing_ever_is_no_data(void) {
  bd_sample_t r = never(), o = never();
  TEST_ASSERT_EQUAL(ENGINE_NO_DATA, engine_assess(&r, &o, NULL, NOW).mode);
  TEST_ASSERT_EQUAL(ENGINE_NO_DATA, engine_assess(NULL, NULL, NULL, NOW).mode);
}

static void test_rpm_turning_is_running(void) {
  bd_sample_t r = num(30, 100), o = num(350000, 100);
  engine_status_t s = engine_assess(&r, &o, NULL, NOW);
  TEST_ASSERT_EQUAL(ENGINE_RUNNING, s.mode);
  TEST_ASSERT_FALSE(s.rpm_suspect);
}

static void test_rpm_zero_oil_low_is_off(void) {   // always-on gateway, engine stopped
  bd_sample_t r = num(0, 100), o = num(0, 100);
  TEST_ASSERT_EQUAL(ENGINE_OFF, engine_assess(&r, &o, NULL, NOW).mode);
}

static void test_rpm_zero_no_oil_sender_is_off(void) {
  bd_sample_t r = num(0, 100);
  TEST_ASSERT_EQUAL(ENGINE_OFF, engine_assess(&r, NULL, NULL, NOW).mode);
}

static void test_rpm_zero_with_oil_pressure_is_running_rpm_suspect(void) {   // bad alternator pickup
  bd_sample_t r = num(0, 100), o = num(320000, 100);
  engine_status_t s = engine_assess(&r, &o, NULL, NOW);
  TEST_ASSERT_EQUAL(ENGINE_RUNNING, s.mode);
  TEST_ASSERT_TRUE(s.rpm_suspect);
}

static void test_silence_after_zero_is_off(void) {   // key off: controller went quiet at 0 RPM
  bd_sample_t r = num(0, 60000), o = num(0, 60000);
  TEST_ASSERT_EQUAL(ENGINE_OFF, engine_assess(&r, &o, NULL, NOW).mode);
}

static void test_silence_after_running_is_no_data(void) {   // controller dropped out mid-run
  bd_sample_t r = num(30, 60000), o = num(350000, 60000);
  TEST_ASSERT_EQUAL(ENGINE_NO_DATA, engine_assess(&r, &o, NULL, NOW).mode);
}

static void test_rpm_silent_but_oil_live_is_running(void) {   // RPM sender died, engine running
  bd_sample_t r = num(30, 60000), o = num(350000, 100);
  engine_status_t s = engine_assess(&r, &o, NULL, NOW);
  TEST_ASSERT_EQUAL(ENGINE_RUNNING, s.mode);
}

static void test_rpm_null_is_not_off(void) {   // a failed value is not a zero
  bd_sample_t r = num(NAN, 100);
  TEST_ASSERT_EQUAL(ENGINE_NO_DATA, engine_assess(&r, NULL, NULL, NOW).mode);
}

static void test_state_stopped_is_off(void) {
  bd_sample_t r = never(), st = text("stopped");
  TEST_ASSERT_EQUAL(ENGINE_OFF, engine_assess(&r, NULL, &st, NOW).mode);
}

static void test_state_started_is_running(void) {
  bd_sample_t st = text("started");
  TEST_ASSERT_EQUAL(ENGINE_RUNNING, engine_assess(NULL, NULL, &st, NOW).mode);
}

static void test_state_unusable_is_no_data(void) {
  bd_sample_t r = num(30, 100), st = text("unusable");
  TEST_ASSERT_EQUAL(ENGINE_NO_DATA, engine_assess(&r, NULL, &st, NOW).mode);
}

static void test_oil_pressure_overrides_stopped_state(void) {
  bd_sample_t st = text("stopped"), o = num(300000, 100);
  TEST_ASSERT_EQUAL(ENGINE_RUNNING, engine_assess(NULL, &o, &st, NOW).mode);
}

static void test_threshold_edges(void) {
  bd_sample_t r = num(ENGINE_RUNNING_HZ, 100), o = num(ENGINE_OIL_RUNNING_PA, 100);
  TEST_ASSERT_EQUAL(ENGINE_OFF, engine_assess(&r, &o, NULL, NOW).mode);   // at threshold = not running
  bd_sample_t r2 = num(0, ENGINE_RPM_MAX_AGE_MS);
  TEST_ASSERT_EQUAL(ENGINE_OFF, engine_assess(&r2, NULL, NULL, NOW).mode);  // exactly max age is still fresh
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_ever_is_no_data);
  RUN_TEST(test_rpm_turning_is_running);
  RUN_TEST(test_rpm_zero_oil_low_is_off);
  RUN_TEST(test_rpm_zero_no_oil_sender_is_off);
  RUN_TEST(test_rpm_zero_with_oil_pressure_is_running_rpm_suspect);
  RUN_TEST(test_silence_after_zero_is_off);
  RUN_TEST(test_silence_after_running_is_no_data);
  RUN_TEST(test_rpm_silent_but_oil_live_is_running);
  RUN_TEST(test_rpm_null_is_not_off);
  RUN_TEST(test_state_stopped_is_off);
  RUN_TEST(test_state_started_is_running);
  RUN_TEST(test_state_unusable_is_no_data);
  RUN_TEST(test_oil_pressure_overrides_stopped_state);
  RUN_TEST(test_threshold_edges);
  return UNITY_END();
}
