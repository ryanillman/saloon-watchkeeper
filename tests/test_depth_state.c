// Which depth is shown, tide direction from the next high or low, and the times Signal K sends.
#include <stdio.h>
#include <string.h>
#include "depth_state.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define NOW 1000000LL
static bd_sample_t depth(double m, int64_t rx) {
  bd_sample_t s = {.kind = BD_NUMBER, .num = m, .rx_ms = rx, .valid = true};
  return s;
}
static bd_sample_t text(const char *t) {
  bd_sample_t s = {.kind = BD_TEXT, .rx_ms = NOW, .valid = true};
  snprintf(s.text, sizeof s.text, "%s", t);
  return s;
}

// ---- which depth ----

static void test_pick_prefers_transducer_then_keel_then_surface(void) {
  bd_sample_t x = depth(3.0, NOW), k = depth(2.0, NOW), s = depth(4.0, NOW);
  const bd_sample_t *all[3] = {&x, &k, &s};
  TEST_ASSERT_EQUAL(DEPTH_BELOW_TRANSDUCER, depth_pick(all, NOW));
  const bd_sample_t *no_x[3] = {NULL, &k, &s};
  TEST_ASSERT_EQUAL(DEPTH_BELOW_KEEL, depth_pick(no_x, NOW));
  const bd_sample_t *only_s[3] = {NULL, NULL, &s};
  TEST_ASSERT_EQUAL(DEPTH_BELOW_SURFACE, depth_pick(only_s, NOW));
}

static void test_pick_skips_stale_and_invalid_readings(void) {
  bd_sample_t x = depth(3.0, NOW - DEPTH_MAX_AGE_MS - 1), k = depth(2.0, NOW), s = depth(4.0, NOW);
  const bd_sample_t *a[3] = {&x, &k, &s};
  TEST_ASSERT_EQUAL(DEPTH_BELOW_KEEL, depth_pick(a, NOW));   // a stale transducer reading is not shown
  k.valid = false;
  TEST_ASSERT_EQUAL(DEPTH_BELOW_SURFACE, depth_pick(a, NOW));
  s.kind = BD_TEXT;
  TEST_ASSERT_EQUAL(DEPTH_NONE, depth_pick(a, NOW));
}

static void test_labels_say_what_the_number_is_measured_from(void) {
  TEST_ASSERT_EQUAL_STRING("BELOW TRANSDUCER", depth_ref_label(DEPTH_BELOW_TRANSDUCER));
  TEST_ASSERT_EQUAL_STRING("BELOW KEEL", depth_ref_label(DEPTH_BELOW_KEEL));
  TEST_ASSERT_EQUAL_STRING("BELOW SURFACE", depth_ref_label(DEPTH_BELOW_SURFACE));
  TEST_ASSERT_EQUAL_STRING("", depth_ref_label(DEPTH_NONE));
}

// ---- times ----

static void test_parse_signalk_timestamp(void) {
  int64_t t = 0;
  TEST_ASSERT_TRUE(depth_parse_time("2026-11-04T14:32:00.000Z", &t));
  TEST_ASSERT_EQUAL_INT64(1793802720, t);
  TEST_ASSERT_TRUE(depth_parse_time("2026-11-04T14:32:00Z", &t));
  TEST_ASSERT_EQUAL_INT64(1793802720, t);
  TEST_ASSERT_TRUE(depth_parse_time("2026-11-04T15:32:00.000+01:00", &t));   // an offset is the same instant
  TEST_ASSERT_EQUAL_INT64(1793802720, t);
}

static void test_parse_rejects_what_is_not_a_utc_timestamp(void) {
  int64_t t = 7;
  TEST_ASSERT_FALSE(depth_parse_time("", &t));
  TEST_ASSERT_FALSE(depth_parse_time("soon", &t));
  TEST_ASSERT_FALSE(depth_parse_time("2026-11-04T14:32:00", &t));        // no Z: not UTC, not guessed
  TEST_ASSERT_FALSE(depth_parse_time("2026-13-04T14:32:00Z", &t));
  TEST_ASSERT_FALSE(depth_parse_time("2026-11-04T25:32:00Z", &t));
  TEST_ASSERT_EQUAL_INT64(7, t);                                          // untouched on failure
}

static void test_format_local_time(void) {
  char out[8];
  depth_format_hhmm(1793802720, 0, out);
  TEST_ASSERT_EQUAL_STRING("14:32", out);
  depth_format_hhmm(1793802720, 60, out);
  TEST_ASSERT_EQUAL_STRING("15:32", out);
  depth_format_hhmm(1793802720, -330, out);   // -5:30 gives 09:02
  TEST_ASSERT_EQUAL_STRING("09:02", out);
  depth_format_hhmm(1793802720 + 10 * 3600, 0, out);   // past midnight wraps
  TEST_ASSERT_EQUAL_STRING("00:32", out);
}

// ---- tide ----

#define T_1432 1793802720LL
#define T_1200 1793793600LL

static void test_next_event_a_high_means_rising(void) {
  bd_sample_t th = text("2026-11-04T14:32:00.000Z"), tl = text("2026-11-04T20:50:00.000Z");
  tide_t t = tide_assess(&th, &tl, T_1200);
  TEST_ASSERT_EQUAL(TIDE_RISING, t.dir);
  TEST_ASSERT_TRUE(t.has_next);
  TEST_ASSERT_TRUE(t.next_high);
  TEST_ASSERT_EQUAL_INT64(T_1432, t.next_s);
}

static void test_next_event_a_low_means_falling(void) {
  bd_sample_t th = text("2026-11-04T20:50:00.000Z"), tl = text("2026-11-04T14:32:00.000Z");
  tide_t t = tide_assess(&th, &tl, T_1200);
  TEST_ASSERT_EQUAL(TIDE_FALLING, t.dir);
  TEST_ASSERT_FALSE(t.next_high);
  TEST_ASSERT_EQUAL_INT64(T_1432, t.next_s);
}

static void test_a_past_event_is_ignored_for_the_next_one(void) {
  bd_sample_t th = text("2026-11-04T08:05:00.000Z"), tl = text("2026-11-04T14:32:00.000Z");   // the high has passed
  tide_t t = tide_assess(&th, &tl, T_1200);
  TEST_ASSERT_EQUAL(TIDE_FALLING, t.dir);
  TEST_ASSERT_EQUAL_INT64(T_1432, t.next_s);
}

static void test_one_event_is_enough_and_none_is_unknown(void) {
  bd_sample_t th = text("2026-11-04T14:32:00.000Z");
  tide_t t = tide_assess(&th, NULL, T_1200);
  TEST_ASSERT_EQUAL(TIDE_RISING, t.dir);
  t = tide_assess(NULL, NULL, T_1200);
  TEST_ASSERT_EQUAL(TIDE_UNKNOWN, t.dir);
  TEST_ASSERT_FALSE(t.has_next);
}

static void test_no_clock_or_only_past_events_is_unknown_never_a_guess(void) {
  bd_sample_t th = text("2026-11-04T08:05:00.000Z"), tl = text("2026-11-04T09:00:00.000Z");
  TEST_ASSERT_EQUAL(TIDE_UNKNOWN, tide_assess(&th, &tl, T_1200).dir);   // both stale: the plugin has not updated
  th = text("2026-11-04T14:32:00.000Z");
  TEST_ASSERT_EQUAL(TIDE_UNKNOWN, tide_assess(&th, &tl, 0).dir);         // the server has not told us the time
  bd_sample_t bad = text("not a time");
  TEST_ASSERT_EQUAL(TIDE_UNKNOWN, tide_assess(&bad, &bad, T_1200).dir);
  bd_sample_t inv = text("2026-11-04T14:32:00.000Z");
  inv.valid = false;
  TEST_ASSERT_EQUAL(TIDE_UNKNOWN, tide_assess(&inv, NULL, T_1200).dir);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_pick_prefers_transducer_then_keel_then_surface);
  RUN_TEST(test_pick_skips_stale_and_invalid_readings);
  RUN_TEST(test_labels_say_what_the_number_is_measured_from);
  RUN_TEST(test_parse_signalk_timestamp);
  RUN_TEST(test_parse_rejects_what_is_not_a_utc_timestamp);
  RUN_TEST(test_format_local_time);
  RUN_TEST(test_next_event_a_high_means_rising);
  RUN_TEST(test_next_event_a_low_means_falling);
  RUN_TEST(test_a_past_event_is_ignored_for_the_next_one);
  RUN_TEST(test_one_event_is_enough_and_none_is_unknown);
  RUN_TEST(test_no_clock_or_only_past_events_is_unknown_never_a_guess);
  return UNITY_END();
}
