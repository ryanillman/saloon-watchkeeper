#include <math.h>
#include <stdio.h>
#include "unity.h"
#include "boatdata.h"

#define SRC_SK 1
#define SRC_N2K 2

void setUp(void) { bd_init(); }
void tearDown(void) {}

static void test_new_path_is_invalid(void) {
  bd_id_t id = bd_intern("propulsion.port.revolutions");
  bd_sample_t s;
  TEST_ASSERT_NOT_EQUAL(BD_NONE, id);
  TEST_ASSERT_TRUE(bd_get(id, &s));
  TEST_ASSERT_FALSE(s.valid);
}

static void test_intern_is_idempotent_and_find_works(void) {
  bd_id_t a = bd_intern("a.b");
  TEST_ASSERT_EQUAL(a, bd_intern("a.b"));
  TEST_ASSERT_EQUAL(a, bd_find("a.b"));
  TEST_ASSERT_EQUAL(BD_NONE, bd_find("nope"));
  TEST_ASSERT_EQUAL_STRING("a.b", bd_path(a));
}

static void test_set_number_makes_valid(void) {
  bd_id_t id = bd_intern("x");
  bd_set_number(id, 30.0, SRC_SK, 1000);
  bd_sample_t s;
  bd_get(id, &s);
  TEST_ASSERT_TRUE(s.valid);
  TEST_ASSERT_EQUAL(BD_NUMBER, s.kind);
  TEST_ASSERT_EQUAL_DOUBLE(30.0, s.num);
  TEST_ASSERT_EQUAL_INT64(1000, s.rx_ms);
  TEST_ASSERT_EQUAL_UINT8(SRC_SK, s.source);
}

static void test_nan_and_inf_are_invalid_not_zero(void) {
  bd_id_t id = bd_intern("x");
  bd_sample_t s;
  bd_set_number(id, 5.0, SRC_SK, 1);
  bd_set_number(id, NAN, SRC_SK, 2);
  bd_get(id, &s);
  TEST_ASSERT_FALSE(s.valid);
  bd_set_number(id, 5.0, SRC_SK, 3);
  bd_set_number(id, INFINITY, SRC_SK, 4);
  bd_get(id, &s);
  TEST_ASSERT_FALSE(s.valid);
}

static void test_position_and_text(void) {
  bd_id_t p = bd_intern("navigation.position"), t = bd_intern("name");
  bd_set_position(p, 49.5, -123.1, SRC_SK, 10);
  bd_set_text(t, "Golden Dragon", SRC_SK, 10);
  bd_sample_t s;
  bd_get(p, &s);
  TEST_ASSERT_TRUE(s.valid);
  TEST_ASSERT_EQUAL(BD_POSITION, s.kind);
  TEST_ASSERT_EQUAL_DOUBLE(49.5, s.pos.lat);
  TEST_ASSERT_EQUAL_DOUBLE(-123.1, s.pos.lon);
  bd_get(t, &s);
  TEST_ASSERT_EQUAL(BD_TEXT, s.kind);
  TEST_ASSERT_EQUAL_STRING("Golden Dragon", s.text);
}

static void test_set_invalid(void) {
  bd_id_t id = bd_intern("x");
  bd_set_number(id, 1.0, SRC_SK, 1);
  bd_set_invalid(id);
  bd_sample_t s;
  bd_get(id, &s);
  TEST_ASSERT_FALSE(s.valid);
}

static void test_invalidate_source_only_hits_that_source(void) {
  bd_id_t a = bd_intern("a"), b = bd_intern("b");
  bd_set_number(a, 1.0, SRC_SK, 1);
  bd_set_number(b, 2.0, SRC_N2K, 1);
  bd_invalidate_source(SRC_SK);
  bd_sample_t s;
  bd_get(a, &s);
  TEST_ASSERT_FALSE(s.valid);
  bd_get(b, &s);
  TEST_ASSERT_TRUE(s.valid);
}

static void test_fresh_respects_max_age(void) {
  bd_id_t id = bd_intern("x");
  bd_set_number(id, 1.0, SRC_SK, 1000);
  bd_sample_t s;
  bd_get(id, &s);
  TEST_ASSERT_TRUE(bd_is_fresh(&s, 10000, 10000));    // age 9 s
  TEST_ASSERT_FALSE(bd_is_fresh(&s, 10000, 12000));   // age 11 s
  TEST_ASSERT_TRUE(bd_is_fresh(&s, 0, 1000000));      // no limit
  bd_set_invalid(id);
  bd_get(id, &s);
  TEST_ASSERT_FALSE(bd_is_fresh(&s, 0, 1000));        // invalid is never fresh
}

static void test_match_single_segment(void) {
  bd_intern("propulsion.port.revolutions");
  bd_intern("propulsion.starboard.revolutions");
  bd_intern("propulsion.port.extra.revolutions");
  bd_intern("propulsion.port.temperature");
  bd_id_t out[8];
  TEST_ASSERT_EQUAL(2, bd_match("propulsion.*.revolutions", out, 8));
  TEST_ASSERT_EQUAL(1, bd_match("propulsion.port.temperature", out, 8));
  TEST_ASSERT_EQUAL(1, bd_match("propulsion.*.revolutions", out, 1));   // respects max
}

static void test_intern_full_returns_none(void) {
  char p[32];
  for (int i = 0; i < BD_MAX_PATHS; i++) {
    snprintf(p, sizeof p, "p.%d", i);
    TEST_ASSERT_NOT_EQUAL(BD_NONE, bd_intern(p));
  }
  TEST_ASSERT_EQUAL(BD_NONE, bd_intern("one.too.many"));
}

static void test_meta_roundtrip(void) {
  bd_id_t id = bd_intern("propulsion.port.temperature");
  bd_meta_t m = {.units = "K", .has_scale = true, .scale_lo = 273.15, .scale_hi = 393.15, .zone_count = 1};
  m.zones[0] = (bd_zone_t){.lower = 368.15, .upper = 393.15, .state = 4};
  bd_set_meta(id, &m);
  bd_meta_t out;
  TEST_ASSERT_TRUE(bd_get_meta(id, &out));
  TEST_ASSERT_EQUAL_STRING("K", out.units);
  TEST_ASSERT_EQUAL_DOUBLE(393.15, out.scale_hi);
  TEST_ASSERT_EQUAL_UINT8(1, out.zone_count);
  TEST_ASSERT_EQUAL_DOUBLE(368.15, out.zones[0].lower);
  TEST_ASSERT_FALSE(bd_get_meta(bd_intern("no.meta"), &out));
}

static void test_full_table_of_long_real_paths_fits(void) {
  char p[96];
  for (int i = 0; i < BD_MAX_PATHS; i++) {
    snprintf(p, sizeof p, "electrical.batteries.house%03d.capacity.stateOfCharge.extra.segment.%d", i, i);
    TEST_ASSERT_NOT_EQUAL(BD_NONE, bd_intern(p));
  }
  TEST_ASSERT_EQUAL_STRING("electrical.batteries.house000.capacity.stateOfCharge.extra.segment.0", bd_path(1));
}

static void test_path_storage_exhausted_fails_cleanly(void) {
  char p[256];
  bd_id_t first = BD_NONE, last_ok = BD_NONE;
  bool refused = false;
  for (int i = 0; i < BD_MAX_PATHS; i++) {
    snprintf(p, sizeof p, "%0200d", i);   // 200 chars each: the path storage runs out before the table does
    bd_id_t id = bd_intern(p);
    if (id == BD_NONE) { refused = true; break; }
    if (first == BD_NONE) first = id;
    last_ok = id;
  }
  TEST_ASSERT_TRUE(refused);
  TEST_ASSERT_EQUAL_UINT16(last_ok, bd_count());
  snprintf(p, sizeof p, "%0200d", 0);
  TEST_ASSERT_EQUAL(first, bd_find(p));
  TEST_ASSERT_EQUAL_STRING(p, bd_path(first));
  bd_init();   // a reset gives the storage back
  TEST_ASSERT_NOT_EQUAL(BD_NONE, bd_intern(p));
}

static void test_meta_overwrites_and_is_cleared_by_init(void) {
  bd_id_t id = bd_intern("tank.fuel");
  bd_meta_t m = {.units = "ratio"}, out;
  bd_set_meta(id, &m);
  snprintf(m.units, sizeof m.units, "%s", "L");
  bd_set_meta(id, &m);
  TEST_ASSERT_TRUE(bd_get_meta(id, &out));
  TEST_ASSERT_EQUAL_STRING("L", out.units);
  bd_init();
  TEST_ASSERT_EQUAL(id, bd_intern("tank.fuel"));
  TEST_ASSERT_FALSE(bd_get_meta(id, &out));
}

static void test_meta_for_every_path(void) {
  char p[32];
  bd_meta_t m = {0}, out;
  for (int i = 0; i < BD_MAX_PATHS; i++) {
    snprintf(p, sizeof p, "p.%d", i);
    bd_id_t id = bd_intern(p);
    m.scale_hi = i;
    m.has_scale = true;
    bd_set_meta(id, &m);
  }
  for (int i = 0; i < BD_MAX_PATHS; i++) {
    TEST_ASSERT_TRUE(bd_get_meta((bd_id_t)(i + 1), &out));
    TEST_ASSERT_EQUAL_DOUBLE(i, out.scale_hi);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_new_path_is_invalid);
  RUN_TEST(test_intern_is_idempotent_and_find_works);
  RUN_TEST(test_set_number_makes_valid);
  RUN_TEST(test_nan_and_inf_are_invalid_not_zero);
  RUN_TEST(test_position_and_text);
  RUN_TEST(test_set_invalid);
  RUN_TEST(test_invalidate_source_only_hits_that_source);
  RUN_TEST(test_fresh_respects_max_age);
  RUN_TEST(test_match_single_segment);
  RUN_TEST(test_intern_full_returns_none);
  RUN_TEST(test_meta_roundtrip);
  RUN_TEST(test_full_table_of_long_real_paths_fits);
  RUN_TEST(test_path_storage_exhausted_fails_cleanly);
  RUN_TEST(test_meta_overwrites_and_is_cleared_by_init);
  RUN_TEST(test_meta_for_every_path);
  return UNITY_END();
}
