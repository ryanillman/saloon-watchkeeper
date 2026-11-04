#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "role.h"
#include "tanks.h"

static cfg_t cfg;
static cfg_role_t rc;

void setUp(void) {
  bd_init();
  cfg_defaults(&cfg);
  memset(&rc, 0, sizeof rc);
  strcpy(rc.role, "tanks");
}
void tearDown(void) {}

static bd_sample_t num(double v, bool valid) {
  bd_sample_t s;
  memset(&s, 0, sizeof s);
  s.kind = BD_NUMBER;
  s.num = v;
  s.valid = valid;
  return s;
}

static void test_kind_from_id(void) {
  TEST_ASSERT_EQUAL(TANK_FRESH, tank_kind("freshWater.0"));
  TEST_ASSERT_EQUAL(TANK_FUEL, tank_kind("fuel.1"));
  TEST_ASSERT_EQUAL(TANK_GREY, tank_kind("wasteWater.0"));
  TEST_ASSERT_EQUAL(TANK_BLACK, tank_kind("blackWater.0"));
  TEST_ASSERT_EQUAL(TANK_OTHER, tank_kind("liveWell.0"));
}

static void test_label_numbering(void) {
  char l[16];
  tank_label("freshWater.0", 2, l, sizeof l);
  TEST_ASSERT_EQUAL_STRING("FRESH 1", l);
  tank_label("freshWater.1", 2, l, sizeof l);
  TEST_ASSERT_EQUAL_STRING("FRESH 2", l);
  tank_label("fuel.0", 1, l, sizeof l);
  TEST_ASSERT_EQUAL_STRING("FUEL", l);
}

static void test_volume_prefers_reported(void) {
  bd_sample_t lv = num(0.5, true), vo = num(0.1, true), ca = num(0.4, true);
  double m3 = 0;
  TEST_ASSERT_TRUE(tank_volume(&lv, &vo, &ca, &m3));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.1, m3);
}

static void test_volume_from_level_and_capacity(void) {
  bd_sample_t lv = num(0.5, true), ca = num(0.4, true);
  double m3 = 0;
  TEST_ASSERT_TRUE(tank_volume(&lv, NULL, &ca, &m3));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.2, m3);
}

static void test_volume_flags_when_level_invalid(void) {
  bd_sample_t lv = num(0.5, false), ca = num(0.4, true);
  double m3 = 0;
  TEST_ASSERT_FALSE(tank_volume(&lv, NULL, &ca, &m3));
}

static void test_volume_flags_with_no_capacity(void) {
  bd_sample_t lv = num(0.5, true);
  double m3 = 0;
  TEST_ASSERT_FALSE(tank_volume(&lv, NULL, NULL, &m3));
}

static void test_pages_square_and_wide(void) {
  TEST_ASSERT_EQUAL_UINT8(2, tank_pages(6, SIZE_SQUARE));
  TEST_ASSERT_EQUAL_UINT8(1, tank_pages(6, SIZE_WIDE));
  TEST_ASSERT_EQUAL_UINT8(1, tank_pages(0, SIZE_SQUARE));
}

static void test_fuel_alarm_page_is_not_the_first_on_a_square(void) {
  const char *paths[] = {
    "tanks.blackWater.0.currentLevel", "tanks.freshWater.0.currentLevel", "tanks.freshWater.1.currentLevel",
    "tanks.freshWater.2.currentLevel", "tanks.fuel.0.currentLevel",        "tanks.wasteWater.0.currentLevel",
  };
  for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) bd_set_number(bd_intern(paths[i]), 0.5, 1, 1);
  role_binding_t *b = malloc(sizeof *b);
  role_bind(role_find("tanks"), &rc, &cfg.units, b);
  TEST_ASSERT_EQUAL_UINT8(6, b->g[0].inst_count);
  TEST_ASSERT_EQUAL_STRING("fuel.0", b->g[0].inst_id[4]);
  TEST_ASSERT_EQUAL_UINT8(0, tanks_page_for(b, SIZE_SQUARE, "tanks.freshWater.2.currentLevel"));
  TEST_ASSERT_EQUAL_UINT8(1, tanks_page_for(b, SIZE_SQUARE, "tanks.fuel.0.currentLevel"));
  TEST_ASSERT_EQUAL_UINT8(1, tanks_page_for(b, SIZE_SQUARE, "tanks.wasteWater.0.currentLevel"));
  TEST_ASSERT_EQUAL_UINT8(0, tanks_page_for(b, SIZE_WIDE, "tanks.fuel.0.currentLevel"));
  TEST_ASSERT_EQUAL_UINT8(0, tanks_page_for(b, SIZE_SQUARE, "tanks.liveWell.0.currentLevel"));
  free(b);
}

static void test_bind_discovers_two_level_ids(void) {
  bd_intern("tanks.freshWater.0.currentLevel");
  bd_intern("tanks.fuel.0.currentLevel");
  const role_def_t *d = role_find("tanks");
  TEST_ASSERT_NOT_NULL(d);
  role_binding_t *b = malloc(sizeof *b);
  role_bind(d, &rc, &cfg.units, b);
  TEST_ASSERT_EQUAL_UINT8(2, b->g[0].inst_count);
  // sorted by strcmp: "freshWater.0" < "fuel.0" ('r' < 'u')
  TEST_ASSERT_EQUAL_STRING("freshWater.0", b->g[0].inst_id[0]);
  TEST_ASSERT_EQUAL_STRING("fuel.0", b->g[0].inst_id[1]);
  TEST_ASSERT_EQUAL_STRING("tanks.fuel.0.currentLevel", bd_path(b->g[0].ids[1][0]));
  free(b);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_kind_from_id);
  RUN_TEST(test_label_numbering);
  RUN_TEST(test_volume_prefers_reported);
  RUN_TEST(test_volume_from_level_and_capacity);
  RUN_TEST(test_volume_flags_when_level_invalid);
  RUN_TEST(test_volume_flags_with_no_capacity);
  RUN_TEST(test_pages_square_and_wide);
  RUN_TEST(test_fuel_alarm_page_is_not_the_first_on_a_square);
  RUN_TEST(test_bind_discovers_two_level_ids);
  return UNITY_END();
}
