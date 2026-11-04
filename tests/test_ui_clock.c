#include <string.h>
#include "unity.h"
#include "ui_clock.h"

void setUp(void) { ui_clock_reset(); }
void tearDown(void) {}

#define MIN_MS 60000LL

static void test_parse_utc_with_fraction(void) {
  int64_t t = 0;
  TEST_ASSERT_TRUE(ui_clock_parse_iso("2026-11-04T19:08:24.174Z", &t));
  TEST_ASSERT_EQUAL_INT64(1793819304, t);
}
static void test_parse_without_fraction(void) {
  int64_t t = 0;
  TEST_ASSERT_TRUE(ui_clock_parse_iso("2026-11-04T19:08:24Z", &t));
  TEST_ASSERT_EQUAL_INT64(1793819304, t);
}
static void test_parse_offset_suffix_converts_to_utc(void) {
  int64_t t = 0;
  TEST_ASSERT_TRUE(ui_clock_parse_iso("2026-11-04T21:08:24+02:00", &t));
  TEST_ASSERT_EQUAL_INT64(1793819304, t);
  TEST_ASSERT_TRUE(ui_clock_parse_iso("2026-11-04T14:08:24-05:00", &t));
  TEST_ASSERT_EQUAL_INT64(1793819304, t);
}
static void test_parse_rejects_bad_input(void) {
  int64_t t = 7;
  TEST_ASSERT_FALSE(ui_clock_parse_iso("garbage", &t));
  TEST_ASSERT_FALSE(ui_clock_parse_iso("", &t));
  TEST_ASSERT_FALSE(ui_clock_parse_iso(NULL, &t));
  TEST_ASSERT_FALSE(ui_clock_parse_iso("2026-13-40T00:00:00Z", &t));
  TEST_ASSERT_FALSE(ui_clock_parse_iso("2026-11-04T25:00:00Z", &t));
  TEST_ASSERT_FALSE(ui_clock_parse_iso("2026-11-04T19:08:24", &t));   // no zone: not a time we can trust
  TEST_ASSERT_FALSE(ui_clock_parse_iso("2026-11-04T19:08:24Zjunk", &t));
}
static void test_format_crosses_midnight_with_offset(void) {
  char out[6];
  ui_clock_feed(1793836200, 1000);   // 23:50 UTC
  TEST_ASSERT_TRUE(ui_clock_format(1000 + 20 * MIN_MS, 30, true, out));
  TEST_ASSERT_EQUAL_STRING("00:40", out);   // 23:50 UTC + 20 min + 30 min offset
}
static void test_format_negative_offset(void) {
  char out[6];
  ui_clock_feed(1793844000, 0);   // 02:00 UTC
  TEST_ASSERT_TRUE(ui_clock_format(0, -300, true, out));
  TEST_ASSERT_EQUAL_STRING("21:00", out);
}
static void test_format_offset_change_applies_at_once(void) {
  char out[6];
  ui_clock_feed(1793844000, 0);
  TEST_ASSERT_TRUE(ui_clock_format(0, 0, true, out));
  TEST_ASSERT_EQUAL_STRING("02:00", out);
  TEST_ASSERT_TRUE(ui_clock_format(0, 60, true, out));
  TEST_ASSERT_EQUAL_STRING("03:00", out);
}
static void test_format_advances_with_monotonic_time(void) {
  char out[6];
  ui_clock_feed(1793844000, 5000);
  TEST_ASSERT_TRUE(ui_clock_format(5000 + 61 * MIN_MS, 0, true, out));
  TEST_ASSERT_EQUAL_STRING("03:01", out);
}
static void test_format_never_fed_is_false(void) {
  char out[6] = "x";
  TEST_ASSERT_FALSE(ui_clock_format(0, 0, true, out));
}
static void test_format_stale_with_source_down_is_false(void) {
  char out[6];
  ui_clock_feed(1793844000, 0);
  TEST_ASSERT_FALSE(ui_clock_format(11 * MIN_MS, 0, false, out));
  TEST_ASSERT_TRUE(ui_clock_format(9 * MIN_MS, 0, false, out));   // within 10 min the source being down is fine
}
static void test_format_old_feed_with_source_up_still_shows(void) {
  char out[6];
  ui_clock_feed(1793844000, 0);
  TEST_ASSERT_TRUE(ui_clock_format(11 * MIN_MS, 0, true, out));
  TEST_ASSERT_EQUAL_STRING("02:11", out);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_utc_with_fraction);
  RUN_TEST(test_parse_without_fraction);
  RUN_TEST(test_parse_offset_suffix_converts_to_utc);
  RUN_TEST(test_parse_rejects_bad_input);
  RUN_TEST(test_format_crosses_midnight_with_offset);
  RUN_TEST(test_format_negative_offset);
  RUN_TEST(test_format_offset_change_applies_at_once);
  RUN_TEST(test_format_advances_with_monotonic_time);
  RUN_TEST(test_format_never_fed_is_false);
  RUN_TEST(test_format_stale_with_source_down_is_false);
  RUN_TEST(test_format_old_feed_with_source_up_still_shows);
  return UNITY_END();
}
