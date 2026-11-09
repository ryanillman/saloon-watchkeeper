#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "boatdata.h"
#include "can_instances.h"
#include "unity.h"

static char dir[64];

void setUp(void) {
  bd_init();
  strcpy(dir, "/tmp/bi_ci_XXXXXX");
  TEST_ASSERT_NOT_NULL(mkdtemp(dir));
}
void tearDown(void) {
  char cmd[96];
  snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
  system(cmd);
}

static void sk(const char *path, double v) { bd_set_number(bd_intern(path), v, BD_SRC_SK, 1000); }
static bool has(const can_instances_t *c, const char *path) {
  for (uint16_t i = 0; i < c->count; i++)
    if (!strcmp(c->path[i], path)) return true;
  return false;
}
static bool file_exists(const char *name) {
  char p[128];
  snprintf(p, sizeof p, "%s/%s", dir, name);
  return access(p, F_OK) == 0;
}

static void test_scan_finds_only_valid_signal_k_leaves_the_bus_can_write(void) {
  sk("propulsion.port.revolutions", 40);
  sk("propulsion.port.oilPressure", 300000);
  sk("propulsion.port.state", 1);                         // not a leaf the bus writes
  sk("electrical.batteries.279.voltage", 12.6);
  sk("electrical.batteries.279.capacity.stateOfCharge", 0.8);
  sk("tanks.fuel.port.currentLevel", 0.5);
  sk("environment.depth.belowTransducer", 10);            // fixed path: the bus always creates it
  bd_intern("propulsion.starboard.revolutions");          // a placeholder: never valid
  bd_set_number(bd_intern("propulsion.7.revolutions"), 30, BD_SRC_N2K, 1000);   // written by the bus, not Signal K
  can_instances_t c;
  can_instances_scan(&c);
  TEST_ASSERT_EQUAL_UINT16(5, c.count);
  TEST_ASSERT_TRUE(has(&c, "propulsion.port.revolutions"));
  TEST_ASSERT_TRUE(has(&c, "propulsion.port.oilPressure"));
  TEST_ASSERT_TRUE(has(&c, "electrical.batteries.279.voltage"));
  TEST_ASSERT_TRUE(has(&c, "electrical.batteries.279.capacity.stateOfCharge"));
  TEST_ASSERT_TRUE(has(&c, "tanks.fuel.port.currentLevel"));
  TEST_ASSERT_FALSE(has(&c, "propulsion.port.state"));
  TEST_ASSERT_FALSE(has(&c, "propulsion.starboard.revolutions"));
  TEST_ASSERT_FALSE(has(&c, "propulsion.7.revolutions"));
}

static void test_update_adds_at_once_and_removes_only_when_settled(void) {
  can_instances_t cache, live;
  memset(&cache, 0, sizeof cache);
  memset(&live, 0, sizeof live);
  strcpy(cache.path[0], "propulsion.port.revolutions");
  strcpy(cache.path[1], "propulsion.starboard.revolutions");
  cache.count = 2;
  strcpy(live.path[0], "propulsion.port.revolutions");
  strcpy(live.path[1], "electrical.batteries.1.voltage");
  live.count = 2;
  TEST_ASSERT_TRUE(can_instances_update(&cache, &live, false));   // not settled: only adds
  TEST_ASSERT_EQUAL_UINT16(3, cache.count);
  TEST_ASSERT_TRUE(has(&cache, "propulsion.starboard.revolutions"));
  TEST_ASSERT_TRUE(has(&cache, "electrical.batteries.1.voltage"));
  TEST_ASSERT_TRUE(can_instances_update(&cache, &live, true));    // settled: starboard is gone
  TEST_ASSERT_EQUAL_UINT16(2, cache.count);
  TEST_ASSERT_FALSE(has(&cache, "propulsion.starboard.revolutions"));
  TEST_ASSERT_FALSE(can_instances_update(&cache, &live, true));   // nothing more to do
}

static void test_save_and_load_round_trip_with_a_backup(void) {
  can_instances_t a, b;
  memset(&a, 0, sizeof a);
  strcpy(a.path[0], "propulsion.port.revolutions");
  a.count = 1;
  TEST_ASSERT_FALSE(can_instances_load(dir, &b));   // nothing yet
  TEST_ASSERT_TRUE(can_instances_save(dir, &a));
  strcpy(a.path[1], "tanks.fuel.port.currentLevel");
  a.count = 2;
  TEST_ASSERT_TRUE(can_instances_save(dir, &a));
  TEST_ASSERT_TRUE(file_exists("instances.bak.json"));
  TEST_ASSERT_TRUE(can_instances_load(dir, &b));
  TEST_ASSERT_EQUAL_UINT16(2, b.count);
  TEST_ASSERT_TRUE(has(&b, "tanks.fuel.port.currentLevel"));
  char p[128];   // a damaged main file falls back to the backup
  snprintf(p, sizeof p, "%s/instances.json", dir);
  FILE *f = fopen(p, "w");
  fputs("{ not json", f);
  fclose(f);
  TEST_ASSERT_TRUE(can_instances_load(dir, &b));
  TEST_ASSERT_EQUAL_UINT16(1, b.count);
}

static void test_restore_interns_invalid_placeholders(void) {
  can_instances_t c;
  memset(&c, 0, sizeof c);
  strcpy(c.path[0], "propulsion.port.revolutions");
  strcpy(c.path[1], "electrical.batteries.279.voltage");
  c.count = 2;
  TEST_ASSERT_EQUAL_UINT(2, can_instances_restore(&c));
  bd_sample_t s;
  bd_id_t id = bd_find("propulsion.port.revolutions");
  TEST_ASSERT_NOT_EQUAL(BD_NONE, id);
  TEST_ASSERT_TRUE(bd_get(id, &s));
  TEST_ASSERT_FALSE(s.valid);
  TEST_ASSERT_EQUAL_UINT(2, can_instances_restore(&c));   // idempotent
  can_instances_t again;
  can_instances_scan(&again);
  TEST_ASSERT_EQUAL_UINT16(0, again.count);   // a placeholder is not something Signal K delivered
}

static void test_a_cache_with_a_foreign_path_is_ignored_for_that_entry(void) {
  can_instances_t c;
  memset(&c, 0, sizeof c);
  strcpy(c.path[0], "notifications.mob");            // not something the bus writes: refuse to intern it
  strcpy(c.path[1], "propulsion.port.revolutions");
  c.count = 2;
  TEST_ASSERT_EQUAL_UINT(1, can_instances_restore(&c));
  TEST_ASSERT_EQUAL(BD_NONE, bd_find("notifications.mob"));
}

static void test_tick_saves_on_live_data_and_waits_between_scans(void) {
  can_instances_state_t st;
  TEST_ASSERT_EQUAL_UINT(0, can_instances_init(&st, dir));
  TEST_ASSERT_FALSE(can_instances_tick(&st, false, 1000, dir));   // Signal K down: nothing to learn
  TEST_ASSERT_FALSE(file_exists("instances.json"));
  sk("propulsion.port.revolutions", 40);
  TEST_ASSERT_TRUE(can_instances_tick(&st, true, 2000, dir));
  TEST_ASSERT_TRUE(file_exists("instances.json"));
  sk("electrical.batteries.1.voltage", 12.5);
  TEST_ASSERT_FALSE(can_instances_tick(&st, true, 4000, dir));    // under 5 s since the last scan
  TEST_ASSERT_TRUE(can_instances_tick(&st, true, 7100, dir));     // a new battery is added straight away
  can_instances_t c;
  TEST_ASSERT_TRUE(can_instances_load(dir, &c));
  TEST_ASSERT_EQUAL_UINT16(2, c.count);
}

static void test_tick_removes_a_gone_path_only_after_a_minute_live(void) {
  can_instances_state_t st;
  can_instances_init(&st, dir);
  sk("propulsion.port.revolutions", 40);
  sk("propulsion.starboard.revolutions", 40);
  can_instances_tick(&st, true, 1000, dir);                        // live since 1000
  bd_set_invalid(bd_find("propulsion.starboard.revolutions"));     // the server stopped sending it
  TEST_ASSERT_FALSE(can_instances_tick(&st, true, 20000, dir));    // 19 s live: keep it
  can_instances_t c;
  can_instances_load(dir, &c);
  TEST_ASSERT_EQUAL_UINT16(2, c.count);
  TEST_ASSERT_TRUE(can_instances_tick(&st, true, 62000, dir));     // over a minute: it is gone
  can_instances_load(dir, &c);
  TEST_ASSERT_EQUAL_UINT16(1, c.count);
  can_instances_tick(&st, false, 70000, dir);                      // a disconnect restarts the settling clock
  sk("propulsion.port.revolutions", 40);
  TEST_ASSERT_FALSE(can_instances_tick(&st, true, 71000, dir));
}

static void test_init_restores_from_the_file(void) {
  can_instances_t a;
  memset(&a, 0, sizeof a);
  strcpy(a.path[0], "propulsion.port.revolutions");
  strcpy(a.path[1], "tanks.fuel.port.currentLevel");
  a.count = 2;
  TEST_ASSERT_TRUE(can_instances_save(dir, &a));
  bd_init();   // a fresh boot
  can_instances_state_t st;
  TEST_ASSERT_EQUAL_UINT(2, can_instances_init(&st, dir));
  TEST_ASSERT_NOT_EQUAL(BD_NONE, bd_find("tanks.fuel.port.currentLevel"));
}

static void test_no_directory_is_harmless(void) {
  can_instances_state_t st;
  TEST_ASSERT_EQUAL_UINT(0, can_instances_init(&st, NULL));
  sk("propulsion.port.revolutions", 40);
  TEST_ASSERT_FALSE(can_instances_tick(&st, true, 1000, NULL));   // learns, cannot save
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_scan_finds_only_valid_signal_k_leaves_the_bus_can_write);
  RUN_TEST(test_update_adds_at_once_and_removes_only_when_settled);
  RUN_TEST(test_save_and_load_round_trip_with_a_backup);
  RUN_TEST(test_restore_interns_invalid_placeholders);
  RUN_TEST(test_a_cache_with_a_foreign_path_is_ignored_for_that_entry);
  RUN_TEST(test_tick_saves_on_live_data_and_waits_between_scans);
  RUN_TEST(test_tick_removes_a_gone_path_only_after_a_minute_live);
  RUN_TEST(test_init_restores_from_the_file);
  RUN_TEST(test_no_directory_is_harmless);
  return UNITY_END();
}
