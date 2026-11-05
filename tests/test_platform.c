#include <time.h>
#include "unity.h"
#include "platform.h"

void setUp(void) {}
void tearDown(void) {}

static void test_millis_is_monotonic(void) {
  int64_t a = platform_millis();
  struct timespec ts = {0, 20 * 1000 * 1000};
  nanosleep(&ts, NULL);
  int64_t b = platform_millis();
  TEST_ASSERT_GREATER_OR_EQUAL_INT64(20, b - a);
  TEST_ASSERT_LESS_THAN_INT64(200, b - a);
}

static void test_lock_roundtrip(void) {
  void *l = platform_lock_create();
  TEST_ASSERT_NOT_NULL(l);
  platform_lock(l);
  platform_unlock(l);
}

static void test_parse_iso8601(void) {
  int64_t t = 7;
  TEST_ASSERT_TRUE(platform_parse_iso8601("2026-11-05T02:13:37.578Z", &t));
  TEST_ASSERT_EQUAL_INT64(1793844817, t);
  TEST_ASSERT_TRUE(platform_parse_iso8601("2026-11-05T02:00:00Z", &t));
  TEST_ASSERT_EQUAL_INT64(1793844000, t);
  t = 7;
  TEST_ASSERT_FALSE(platform_parse_iso8601("2026-11-05T02:00:00", &t));   // no zone
  TEST_ASSERT_EQUAL_INT64(7, t);
  TEST_ASSERT_FALSE(platform_parse_iso8601(NULL, &t));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_millis_is_monotonic);
  RUN_TEST(test_lock_roundtrip);
  RUN_TEST(test_parse_iso8601);
  return UNITY_END();
}
