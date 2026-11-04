#include <math.h>
#include <stdio.h>
#include "boatdata.h"
#include "history.h"
#include "unity.h"

#define MIN_MS 60000LL
#define T0 (1000 * MIN_MS)   // an arbitrary monotonic start

static bd_id_t id, id2;

void setUp(void) {
  bd_init();
  hist_init();
  id = bd_intern("environment.wind.speedTrue");
  id2 = bd_intern("environment.depth.belowTransducer");
}
void tearDown(void) {}

// Sets a value and ticks at minute m (from T0).
static void put(bd_id_t i, int m, double v) {
  bd_set_number(i, v, 1, T0 + m * MIN_MS);
  hist_tick(T0 + m * MIN_MS);
}

static void test_track_is_idempotent_and_bounded(void) {
  TEST_ASSERT_TRUE(hist_track(id));
  TEST_ASSERT_TRUE(hist_track(id));
  bd_id_t out[HIST_MAX_PATHS];
  TEST_ASSERT_EQUAL_UINT(1, hist_tracked(out, HIST_MAX_PATHS));
  TEST_ASSERT_EQUAL_UINT16(id, out[0]);
  hist_init();
  for (int i = 0; i < HIST_MAX_PATHS; i++) {
    char p[32];
    snprintf(p, sizeof p, "a.p%d", i);
    TEST_ASSERT_TRUE(hist_track(bd_intern(p)));
  }
  TEST_ASSERT_FALSE(hist_track(bd_intern("a.one.too.many")));
  TEST_ASSERT_EQUAL_UINT(HIST_MAX_PATHS, hist_tracked(out, HIST_MAX_PATHS));
}

static void test_ring_wraps_and_keeps_the_newest_last(void) {
  hist_track(id);
  for (int m = 0; m < 800; m++) put(id, m, m);
  static float out[HIST_MINUTES];
  TEST_ASSERT_EQUAL_UINT(HIST_MINUTES, hist_read(id, HIST_MINUTES, out));
  TEST_ASSERT_EQUAL_FLOAT(799, out[HIST_MINUTES - 1]);
  TEST_ASSERT_EQUAL_FLOAT(80, out[0]);
}

static void test_a_minute_without_a_valid_value_is_a_gap(void) {
  hist_track(id);
  put(id, 0, 5);
  bd_set_invalid(id);
  hist_tick(T0 + 1 * MIN_MS);   // invalid at the tick
  put(id, 2, 7);
  float out[3];
  TEST_ASSERT_EQUAL_UINT(3, hist_read(id, 3, out));
  TEST_ASSERT_EQUAL_FLOAT(5, out[0]);
  TEST_ASSERT_TRUE(isnan(out[1]));
  TEST_ASSERT_EQUAL_FLOAT(7, out[2]);
}

static void test_skipped_minutes_and_stale_values_are_gaps(void) {
  hist_track(id);
  put(id, 0, 5);
  hist_tick(T0 + 4 * MIN_MS);   // no ticks for 3 minutes, and the value is 4 minutes old
  float out[5];
  TEST_ASSERT_EQUAL_UINT(5, hist_read(id, 5, out));
  TEST_ASSERT_EQUAL_FLOAT(5, out[0]);
  for (int i = 1; i < 5; i++) TEST_ASSERT_TRUE_MESSAGE(isnan(out[i]), "skipped or stale minute");
}

static void test_change_over_an_hour(void) {
  hist_track(id);
  for (int m = 0; m <= 60; m++) put(id, m, 10 + 0.1 * m);
  float d = 0;
  TEST_ASSERT_TRUE(hist_change(id, 60, &d));
  TEST_ASSERT_FLOAT_WITHIN(0.001, 6.0, d);
}

static void test_change_needs_two_valid_points_and_skips_gaps(void) {
  hist_track(id);
  float d;
  put(id, 0, 3);
  TEST_ASSERT_FALSE(hist_change(id, 60, &d));   // one point
  hist_tick(T0 + 1 * MIN_MS);                   // 60 s old: still fresh (limit 90 s)
  hist_tick(T0 + 2 * MIN_MS);
  put(id, 3, 9);
  TEST_ASSERT_TRUE(hist_change(id, 60, &d));
  TEST_ASSERT_EQUAL_FLOAT(6, d);                // gaps ignored: 9 - 3
}

static void test_backfill_fills_but_never_overwrites_live(void) {
  hist_track(id);
  for (int m = 0; m < 5; m++) put(id, m, 100 + m);   // 5 live minutes
  float vals[100];
  for (int i = 0; i < 100; i++) vals[i] = 1 + i;      // vals[99] is the current minute (minute 4)
  hist_backfill(id, vals, 100);
  float out[100];
  TEST_ASSERT_EQUAL_UINT(100, hist_read(id, 100, out));
  for (int i = 0; i < 5; i++) TEST_ASSERT_EQUAL_FLOAT(100 + i, out[95 + i]);   // live wins
  TEST_ASSERT_EQUAL_FLOAT(1, out[0]);
  TEST_ASSERT_EQUAL_FLOAT(95, out[94]);
}

static void test_backfill_null_entries_stay_gaps_and_old_data_is_dropped(void) {
  hist_track(id);
  put(id, 0, 1);
  static float vals[HIST_MINUTES + 50];
  for (int i = 0; i < HIST_MINUTES + 50; i++) vals[i] = NAN;
  vals[HIST_MINUTES + 49 - 10] = 42;   // ten minutes ago
  vals[0] = 77;                        // 769 minutes ago: older than the ring
  hist_backfill(id, vals, HIST_MINUTES + 50);
  static float out[HIST_MINUTES];
  TEST_ASSERT_EQUAL_UINT(HIST_MINUTES, hist_read(id, HIST_MINUTES, out));
  TEST_ASSERT_EQUAL_FLOAT(42, out[HIST_MINUTES - 1 - 10]);
  TEST_ASSERT_TRUE(isnan(out[HIST_MINUTES - 5]));
  TEST_ASSERT_EQUAL_FLOAT(1, out[HIST_MINUTES - 1]);
}

static void test_read_right_after_boot_returns_only_what_exists(void) {
  hist_track(id);
  for (int m = 0; m < 3; m++) put(id, m, m);
  float out[HIST_MINUTES];
  TEST_ASSERT_EQUAL_UINT(3, hist_read(id, HIST_MINUTES, out));
  TEST_ASSERT_EQUAL_UINT(0, hist_read(id2, 10, out));   // untracked
}

static void test_paths_are_independent(void) {
  hist_track(id);
  hist_track(id2);
  for (int m = 0; m < 2; m++) {   // both values set, then one tick per minute
    bd_set_number(id, 1 + m, 1, T0 + m * MIN_MS);
    bd_set_number(id2, 50 + m, 1, T0 + m * MIN_MS);
    hist_tick(T0 + m * MIN_MS);
  }
  float a[2], b[2];
  hist_read(id, 2, a);
  hist_read(id2, 2, b);
  TEST_ASSERT_EQUAL_FLOAT(2, a[1]);
  TEST_ASSERT_EQUAL_FLOAT(51, b[1]);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_track_is_idempotent_and_bounded);
  RUN_TEST(test_ring_wraps_and_keeps_the_newest_last);
  RUN_TEST(test_a_minute_without_a_valid_value_is_a_gap);
  RUN_TEST(test_skipped_minutes_and_stale_values_are_gaps);
  RUN_TEST(test_change_over_an_hour);
  RUN_TEST(test_change_needs_two_valid_points_and_skips_gaps);
  RUN_TEST(test_backfill_fills_but_never_overwrites_live);
  RUN_TEST(test_backfill_null_entries_stay_gaps_and_old_data_is_dropped);
  RUN_TEST(test_read_right_after_boot_returns_only_what_exists);
  RUN_TEST(test_paths_are_independent);
  return UNITY_END();
}
