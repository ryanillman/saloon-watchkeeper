#include <math.h>
#include <stdio.h>
#include "unity.h"
#include "boatdata.h"

#define SRC_SK BD_SRC_SK
#define SRC_N2K BD_SRC_N2K

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

static void test_source_time_roundtrip(void) {
  bd_id_t id = bd_intern("navigation.anchor.currentRadius");
  bd_set_number(id, 12, 1, 100);
  bd_sample_t s;
  TEST_ASSERT_TRUE(bd_get(id, &s));
  TEST_ASSERT_EQUAL_INT64(0, s.src_s);   // an update with no Signal K timestamp
  bd_set_source_time(id, 1793844817);
  TEST_ASSERT_TRUE(bd_get(id, &s));
  TEST_ASSERT_EQUAL_INT64(1793844817, s.src_s);
  bd_set_number(id, 13, 1, 200);   // a newer value that carries no timestamp must not keep the old one
  TEST_ASSERT_TRUE(bd_get(id, &s));
  TEST_ASSERT_EQUAL_INT64(0, s.src_s);
}

static void test_invalidated_flag_and_newest_source_time(void) {
  bd_id_t id = bd_intern("a.b");
  bd_set_number(id, 5, 1, 100);
  bd_sample_t s;
  bd_get(id, &s);
  TEST_ASSERT_FALSE(s.invalidated);
  bd_invalidate_source(1);   // the connection went away
  bd_get(id, &s);
  TEST_ASSERT_FALSE(s.valid);
  TEST_ASSERT_TRUE(s.invalidated);   // unlike a null the source sent
  bd_set_number(id, NAN, 1, 200);
  bd_get(id, &s);
  TEST_ASSERT_FALSE(s.invalidated);
  TEST_ASSERT_EQUAL_INT64(0, bd_newest_source_time());
  bd_set_source_time(id, 5000);
  bd_set_source_time(bd_intern("c.d"), 4000);
  TEST_ASSERT_EQUAL_INT64(5000, bd_newest_source_time());   // the server's own clock, as far as it has spoken
}

// ---- arbitration ----
static void rank_sources(void) {
  bd_source_register(SRC_SK, "signalk", 2);
  bd_source_register(SRC_N2K, "n2k", 1);
}

static double num(bd_id_t id) {
  bd_sample_t s;
  TEST_ASSERT_TRUE(bd_get(id, &s));
  return s.valid ? s.num : NAN;
}

static void test_unranked_sources_are_last_writer_wins(void) {
  bd_id_t id = bd_intern("a");
  bd_set_number(id, 1, SRC_SK, 100);
  bd_set_number(id, 2, SRC_N2K, 200);
  TEST_ASSERT_EQUAL_DOUBLE(2, num(id));
}

static void test_lower_rank_is_dropped_while_higher_is_fresh(void) {
  rank_sources();
  bd_id_t id = bd_intern("a");
  bd_set_number(id, 1, SRC_SK, 1000);
  bd_set_number(id, 9, SRC_N2K, 2000);
  TEST_ASSERT_EQUAL_DOUBLE(1, num(id));
  bd_sample_t s;
  bd_get(id, &s);
  TEST_ASSERT_EQUAL_UINT8(SRC_SK, s.source);
  TEST_ASSERT_EQUAL_INT64(1000, s.rx_ms);   // the dropped write left no trace
}

static void test_lower_rank_takes_over_when_higher_goes_quiet(void) {
  rank_sources();
  bd_id_t id = bd_intern("a");
  bd_set_number(id, 1, SRC_SK, 1000);
  bd_set_number(id, 9, SRC_N2K, 1000 + BD_HOLD_MS_DEFAULT);       // exactly at the window: still held
  TEST_ASSERT_EQUAL_DOUBLE(1, num(id));
  bd_set_number(id, 9, SRC_N2K, 1000 + BD_HOLD_MS_DEFAULT + 1);   // past it
  TEST_ASSERT_EQUAL_DOUBLE(9, num(id));
}

static void test_hold_window_is_adjustable(void) {
  rank_sources();
  bd_set_hold_ms(5000);
  bd_id_t id = bd_intern("a");
  bd_set_number(id, 1, SRC_SK, 1000);
  bd_set_number(id, 9, SRC_N2K, 6001);
  TEST_ASSERT_EQUAL_DOUBLE(9, num(id));
  bd_set_hold_ms(0);   // back to the default
  bd_set_number(id, 2, SRC_SK, 7000);
  bd_set_number(id, 8, SRC_N2K, 7000 + 6000);
  TEST_ASSERT_EQUAL_DOUBLE(2, num(id));
}

static void test_higher_rank_always_takes_back(void) {
  rank_sources();
  bd_id_t id = bd_intern("a");
  bd_set_number(id, 9, SRC_N2K, 1000);
  bd_set_number(id, 1, SRC_SK, 1001);
  TEST_ASSERT_EQUAL_DOUBLE(1, num(id));
}

static void test_invalidating_the_preferred_source_hands_over_at_once(void) {
  rank_sources();
  bd_id_t id = bd_intern("a");
  bd_set_number(id, 1, SRC_SK, 1000);
  bd_invalidate_source(SRC_SK);   // the Signal K connection dropped
  bd_set_number(id, 9, SRC_N2K, 1100);
  TEST_ASSERT_EQUAL_DOUBLE(9, num(id));
  bd_sample_t s;
  bd_get(id, &s);
  TEST_ASSERT_FALSE(s.invalidated);
}

static void test_null_from_preferred_source_does_not_hide_a_fresh_lower_one(void) {
  rank_sources();
  bd_id_t id = bd_intern("a");
  bd_set_number(id, 9, SRC_N2K, 1000);
  bd_set_number(id, NAN, SRC_SK, 1100);   // Signal K has no reading; the sensor on the bus does
  TEST_ASSERT_EQUAL_DOUBLE(9, num(id));
  bd_set_number(id, NAN, SRC_SK, 1000 + BD_HOLD_MS_DEFAULT + 1);   // but not once the bus has gone quiet
  TEST_ASSERT_TRUE(isnan(num(id)));
}

static void test_null_from_lower_source_does_not_hide_the_higher_one(void) {
  rank_sources();
  bd_id_t id = bd_intern("a");
  bd_set_number(id, 1, SRC_SK, 1000);
  bd_set_number(id, NAN, SRC_N2K, 1100);
  TEST_ASSERT_EQUAL_DOUBLE(1, num(id));
}

static void test_same_source_updates_and_nulls_pass(void) {
  rank_sources();
  bd_id_t id = bd_intern("a");
  bd_set_number(id, 1, SRC_SK, 1000);
  bd_set_number(id, NAN, SRC_SK, 1100);
  TEST_ASSERT_TRUE(isnan(num(id)));
  bd_set_number(id, 3, SRC_SK, 1200);
  TEST_ASSERT_EQUAL_DOUBLE(3, num(id));
}

static void test_positions_and_text_arbitrate_too(void) {
  rank_sources();
  bd_id_t p = bd_intern("pos"), t = bd_intern("txt");
  bd_set_position(p, 49.0, -123.0, SRC_SK, 1000);
  bd_set_position(p, 10.0, 10.0, SRC_N2K, 1100);
  bd_set_text(t, "sk", SRC_SK, 1000);
  bd_set_text(t, "n2k", SRC_N2K, 1100);
  bd_sample_t s;
  bd_get(p, &s);
  TEST_ASSERT_EQUAL_DOUBLE(49.0, s.pos.lat);
  bd_get(t, &s);
  TEST_ASSERT_EQUAL_STRING("sk", s.text);
  bd_set_position(p, NAN, NAN, SRC_SK, 1200);   // an unreadable position is as weak as a NaN number
  bd_set_position(p, 10.0, 10.0, SRC_N2K, 1300);
  bd_get(p, &s);
  TEST_ASSERT_EQUAL_DOUBLE(10.0, s.pos.lat);    // the SK null was a real write (same source); the bus then fills in
}

static void test_path_is_arbitrated_per_path(void) {
  rank_sources();
  bd_id_t a = bd_intern("a"), b = bd_intern("b");
  bd_set_number(a, 1, SRC_SK, 1000);   // Signal K has `a` but not `b`
  bd_set_number(a, 9, SRC_N2K, 1100);
  bd_set_number(b, 8, SRC_N2K, 1100);
  TEST_ASSERT_EQUAL_DOUBLE(1, num(a));
  TEST_ASSERT_EQUAL_DOUBLE(8, num(b));
}

static void test_replayed_signalk_value_does_not_displace_fresh_bus_value(void) {
  rank_sources();
  bd_id_t clock = bd_intern("navigation.datetime"), id = bd_intern("a");
  bd_set_text(clock, "t", SRC_SK, 1000);
  bd_set_source_time(clock, 100000);          // the server's clock now
  bd_set_number(id, 9, SRC_N2K, 1000);        // the bus value is current
  TEST_ASSERT_FALSE(bd_admit(id, SRC_SK, 100000 - 3600, 1100));   // the server replays an hour-old value
  TEST_ASSERT_TRUE(bd_admit(id, SRC_SK, 100000 - 5, 1100));       // a reading a few seconds old is a reading
  TEST_ASSERT_TRUE(bd_admit(id, SRC_SK, 0, 1100));                // no timestamp: nothing to judge by
}

static void test_replay_is_kept_when_nothing_fresher_holds_the_path(void) {
  rank_sources();
  bd_id_t clock = bd_intern("navigation.datetime"), id = bd_intern("a");
  bd_set_text(clock, "t", SRC_SK, 1000);
  bd_set_source_time(clock, 100000);
  TEST_ASSERT_TRUE(bd_admit(id, SRC_SK, 100000 - 3600, 1100));    // empty path: an old value beats none
  bd_set_number(id, 9, SRC_N2K, 1000);
  TEST_ASSERT_TRUE(bd_admit(id, SRC_SK, 100000 - 3600, 1000 + BD_HOLD_MS_DEFAULT + 1));   // the bus has gone quiet
}

static void test_source_states_and_names(void) {
  TEST_ASSERT_EQUAL(BD_SRC_NOT_CONFIGURED, bd_source_state(SRC_N2K));
  rank_sources();
  TEST_ASSERT_EQUAL(BD_SRC_OFFLINE, bd_source_state(SRC_N2K));   // registered means configured
  bd_source_set_state(SRC_N2K, BD_SRC_LIVE);
  TEST_ASSERT_EQUAL(BD_SRC_LIVE, bd_source_state(SRC_N2K));
  TEST_ASSERT_EQUAL_STRING("signalk", bd_source_name(SRC_SK));
  TEST_ASSERT_EQUAL_STRING("", bd_source_name(5));
  TEST_ASSERT_EQUAL(BD_SRC_NOT_CONFIGURED, bd_source_state(200));   // out of range is not configured, never a crash
  bd_init();
  TEST_ASSERT_EQUAL(BD_SRC_NOT_CONFIGURED, bd_source_state(SRC_N2K));   // bd_init forgets the registry
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
  RUN_TEST(test_source_time_roundtrip);
  RUN_TEST(test_invalidated_flag_and_newest_source_time);
  RUN_TEST(test_unranked_sources_are_last_writer_wins);
  RUN_TEST(test_lower_rank_is_dropped_while_higher_is_fresh);
  RUN_TEST(test_lower_rank_takes_over_when_higher_goes_quiet);
  RUN_TEST(test_hold_window_is_adjustable);
  RUN_TEST(test_higher_rank_always_takes_back);
  RUN_TEST(test_invalidating_the_preferred_source_hands_over_at_once);
  RUN_TEST(test_null_from_preferred_source_does_not_hide_a_fresh_lower_one);
  RUN_TEST(test_null_from_lower_source_does_not_hide_the_higher_one);
  RUN_TEST(test_same_source_updates_and_nulls_pass);
  RUN_TEST(test_positions_and_text_arbitrate_too);
  RUN_TEST(test_path_is_arbitrated_per_path);
  RUN_TEST(test_replayed_signalk_value_does_not_displace_fresh_bus_value);
  RUN_TEST(test_replay_is_kept_when_nothing_fresher_holds_the_path);
  RUN_TEST(test_source_states_and_names);
  return UNITY_END();
}
