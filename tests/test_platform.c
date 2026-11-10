#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
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

static void put(const char *path, const char *text) {
  FILE *f = fopen(path, "w");
  TEST_ASSERT_NOT_NULL(f);
  fputs(text, f);
  fclose(f);
}

static void get(const char *path, char *out, size_t n) {
  out[0] = 0;
  FILE *f = fopen(path, "r");
  if (!f) return;
  size_t k = fread(out, 1, n - 1, f);
  out[k] = 0;
  fclose(f);
}

static void test_replace_file_keeps_the_previous_as_backup(void) {
  char dir[] = "/tmp/bi_replace_XXXXXX", m[96], b[96], t[96], buf[16];
  TEST_ASSERT_NOT_NULL(mkdtemp(dir));
  snprintf(m, sizeof m, "%s/a.json", dir);
  snprintf(b, sizeof b, "%s/a.bak.json", dir);
  snprintf(t, sizeof t, "%s/a.tmp", dir);
  put(m, "one");
  put(t, "two");
  TEST_ASSERT_TRUE(platform_replace_file(t, m, b));
  get(m, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("two", buf);
  get(b, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("one", buf);
  put(t, "three");
  TEST_ASSERT_TRUE(platform_replace_file(t, m, b));
  get(b, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("two", buf);
  remove(m); remove(b); rmdir(dir);
}

static void test_replace_file_does_not_remove_the_backup_when_main_is_missing(void) {
  char dir[] = "/tmp/bi_replace_XXXXXX", m[96], b[96], t[96], buf[16];
  TEST_ASSERT_NOT_NULL(mkdtemp(dir));
  snprintf(m, sizeof m, "%s/a.json", dir);
  snprintf(b, sizeof b, "%s/a.bak.json", dir);
  snprintf(t, sizeof t, "%s/missing.tmp", dir);   // the finished file is not there: the final rename fails
  put(b, "good");
  TEST_ASSERT_FALSE(platform_replace_file(t, m, b));
  get(b, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("good", buf);   // still the only copy, still loadable
  remove(b); rmdir(dir);
}

static void test_replace_file_restores_main_when_the_final_rename_fails(void) {
  char dir[] = "/tmp/bi_replace_XXXXXX", m[96], b[96], t[96], buf[16];
  TEST_ASSERT_NOT_NULL(mkdtemp(dir));
  snprintf(m, sizeof m, "%s/a.json", dir);
  snprintf(b, sizeof b, "%s/a.bak.json", dir);
  snprintf(t, sizeof t, "%s/missing.tmp", dir);
  put(m, "keep");
  TEST_ASSERT_FALSE(platform_replace_file(t, m, b));
  get(m, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("keep", buf);
  TEST_ASSERT_TRUE(platform_replace_file(t, m, b) == false);   // and again: nothing is lost on a repeat
  get(m, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("keep", buf);
  remove(m); remove(b); rmdir(dir);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_millis_is_monotonic);
  RUN_TEST(test_lock_roundtrip);
  RUN_TEST(test_parse_iso8601);
  RUN_TEST(test_replace_file_keeps_the_previous_as_backup);
  RUN_TEST(test_replace_file_does_not_remove_the_backup_when_main_is_missing);
  RUN_TEST(test_replace_file_restores_main_when_the_final_rename_fails);
  return UNITY_END();
}
