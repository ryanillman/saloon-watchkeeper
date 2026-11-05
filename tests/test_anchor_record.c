#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "anchor_record.h"

static char dir[64];

static void write_file(const char *name, const char *text) {
  char p[128];
  snprintf(p, sizeof p, "%s/%s", dir, name);
  FILE *f = fopen(p, "w");
  fputs(text, f);
  fclose(f);
}

void setUp(void) {
  strcpy(dir, "/tmp/bi_anchor_XXXXXX");
  TEST_ASSERT_NOT_NULL(mkdtemp(dir));
}
void tearDown(void) {
  char cmd[96];
  snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
  system(cmd);
}

static anchor_record_t rec(bool armed, double limit, int64_t set) { return (anchor_record_t){armed, limit, set}; }

static void test_record_roundtrip(void) {
  anchor_record_t in = rec(true, 32.5, 1793844817), out = {0};
  TEST_ASSERT_TRUE(anchor_record_save(dir, &in));
  TEST_ASSERT_TRUE(anchor_record_load(dir, &out));
  TEST_ASSERT_TRUE(out.armed);
  TEST_ASSERT_EQUAL_DOUBLE(32.5, out.limit_m);
  TEST_ASSERT_EQUAL_INT64(1793844817, out.set_epoch_s);
  in = rec(false, 0, 0);
  TEST_ASSERT_TRUE(anchor_record_save(dir, &in));
  TEST_ASSERT_TRUE(anchor_record_load(dir, &out));
  TEST_ASSERT_FALSE(out.armed);
}

static void test_record_falls_back_to_bak_when_main_is_corrupt(void) {
  anchor_record_t first = rec(true, 30, 100), second = rec(true, 45, 200), out = {0};
  TEST_ASSERT_TRUE(anchor_record_save(dir, &first));
  TEST_ASSERT_TRUE(anchor_record_save(dir, &second));   // the first moves to anchor.bak.json
  write_file("anchor.json", "{ \"armed\": tru");         // a torn write
  TEST_ASSERT_TRUE(anchor_record_load(dir, &out));
  TEST_ASSERT_EQUAL_DOUBLE(30, out.limit_m);
}

static void test_record_missing_returns_false(void) {
  anchor_record_t out = {0};
  TEST_ASSERT_FALSE(anchor_record_load(dir, &out));
  write_file("anchor.json", "not json");
  write_file("anchor.bak.json", "{\"limit_m\": 5}");   // no armed flag: not a record
  TEST_ASSERT_FALSE(anchor_record_load(dir, &out));
}

static void test_record_save_to_unwritable_dir_returns_false(void) {
  anchor_record_t in = rec(true, 30, 0);
  TEST_ASSERT_FALSE(anchor_record_save("/nonexistent/anchor-dir", &in));
  TEST_ASSERT_FALSE(anchor_record_save(NULL, &in));
}

static void test_record_interrupted_write_leaves_old_record(void) {
  anchor_record_t in = rec(true, 30, 100), out = {0};
  TEST_ASSERT_TRUE(anchor_record_save(dir, &in));
  write_file("anchor.tmp", "{ \"armed\": false, \"limit_m\": 1");   // the power went before the rename
  TEST_ASSERT_TRUE(anchor_record_load(dir, &out));
  TEST_ASSERT_TRUE(out.armed);
  TEST_ASSERT_EQUAL_DOUBLE(30, out.limit_m);
  in = rec(true, 40, 100);
  TEST_ASSERT_TRUE(anchor_record_save(dir, &in));   // a leftover temporary file does not block the next save
  TEST_ASSERT_TRUE(anchor_record_load(dir, &out));
  TEST_ASSERT_EQUAL_DOUBLE(40, out.limit_m);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_record_roundtrip);
  RUN_TEST(test_record_falls_back_to_bak_when_main_is_corrupt);
  RUN_TEST(test_record_missing_returns_false);
  RUN_TEST(test_record_save_to_unwritable_dir_returns_false);
  RUN_TEST(test_record_interrupted_write_leaves_old_record);
  return UNITY_END();
}
