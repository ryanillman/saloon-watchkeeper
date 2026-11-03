#include <string.h>
#include "unity.h"
#include "role.h"

static const role_def_t *engine;
static cfg_t cfg;
static cfg_role_t rc;
static role_binding_t b;

static int slot(const char *name) {
  for (int i = 0; i < engine->slot_count; i++)
    if (!strcmp(engine->slots[i].slot, name)) return i;
  TEST_FAIL_MESSAGE("no such slot");
  return -1;
}

void setUp(void) {
  bd_init();
  cfg_defaults(&cfg);
  memset(&rc, 0, sizeof rc);
  strcpy(rc.role, "engine");
  engine = role_find("engine");
  TEST_ASSERT_NOT_NULL(engine);
}
void tearDown(void) {}

static void bind(void) { role_bind(engine, &rc, &cfg.units, &b); }

static void test_discovers_two_engines_sorted(void) {
  bd_intern("propulsion.starboard.revolutions");
  bd_intern("propulsion.port.revolutions");
  bd_intern("propulsion.port.temperature");
  bind();
  TEST_ASSERT_EQUAL_UINT8(2, b.inst_count);
  TEST_ASSERT_EQUAL_STRING("port", b.inst_id[0]);
  TEST_ASSERT_EQUAL_STRING("starboard", b.inst_id[1]);
  TEST_ASSERT_EQUAL_STRING("PORT", b.inst_label[0]);
  TEST_ASSERT_EQUAL_STRING("STBD", b.inst_label[1]);
  TEST_ASSERT_EQUAL_STRING("propulsion.starboard.revolutions", bd_path(b.ids[1][slot("rpm")]));
}

static void test_excluded_instance_dropped(void) {
  bd_intern("propulsion.port.revolutions");
  bd_intern("propulsion.wing.revolutions");
  strcpy(rc.exclude[0], "wing");
  rc.exclude_count = 1;
  bind();
  TEST_ASSERT_EQUAL_UINT8(1, b.inst_count);
}

static void test_pinned_instance_missing_still_bound(void) {
  strcpy(rc.inst[0].id, "port");
  strcpy(rc.inst[0].label, "PORT");
  rc.inst_count = 1;
  bind();
  TEST_ASSERT_EQUAL_UINT8(1, b.inst_count);
  bd_id_t id = b.ids[0][slot("rpm")];
  TEST_ASSERT_NOT_EQUAL(BD_NONE, id);
  TEST_ASSERT_EQUAL_STRING("propulsion.port.revolutions", bd_path(id));
  bd_sample_t s;
  bd_get(id, &s);
  TEST_ASSERT_FALSE(s.valid);
}

static void test_zero_engines_gives_placeholder(void) {
  bind();
  TEST_ASSERT_EQUAL_UINT8(1, b.inst_count);
  TEST_ASSERT_EQUAL_STRING("ENGINE", b.inst_label[0]);
  for (int i = 0; i < engine->slot_count; i++) TEST_ASSERT_EQUAL(BD_NONE, b.ids[0][i]);
}

static void test_fallback_path_used_when_primary_absent(void) {
  bd_intern("propulsion.port.revolutions");
  bd_intern("propulsion.port.coolantTemperature");
  bind();
  TEST_ASSERT_EQUAL_STRING("propulsion.port.coolantTemperature", bd_path(b.ids[0][slot("coolant")]));
}

static void test_explicit_bind_wins(void) {
  bd_intern("propulsion.port.revolutions");
  bd_intern("propulsion.port.temperature");
  strcpy(rc.bind[0].slot, "port.coolant");
  strcpy(rc.bind[0].path, "propulsion.port.exhaustTemperature");
  rc.bind_count = 1;
  bind();
  TEST_ASSERT_EQUAL_STRING("propulsion.port.exhaustTemperature", bd_path(b.ids[0][slot("coolant")]));
}

static void test_override_beats_meta_beats_default(void) {
  bd_id_t rpm = bd_intern("propulsion.port.revolutions");
  bd_intern("propulsion.port.temperature");
  bind();
  int r = slot("rpm"), t = slot("coolant");
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 4000.0 / 60.0, b.max[0][r]);           // default, in Hz
  bd_meta_t m = {.has_scale = true, .scale_lo = 0, .scale_hi = 50};
  bd_set_meta(rpm, &m);
  bind();
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 50.0, b.max[0][r]);                    // meta
  strcpy(rc.ovr[0].slot, "rpm");
  rc.ovr[0].has_max = true;
  rc.ovr[0].max = 3600;
  rc.ovr_count = 1;
  bind();
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 60.0, b.max[0][r]);                    // override 3600 RPM
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 0.0, b.min[0][r]);                     // min still from meta
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 393.15, b.max[0][t]);                  // 120 °C default
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_discovers_two_engines_sorted);
  RUN_TEST(test_excluded_instance_dropped);
  RUN_TEST(test_pinned_instance_missing_still_bound);
  RUN_TEST(test_zero_engines_gives_placeholder);
  RUN_TEST(test_fallback_path_used_when_primary_absent);
  RUN_TEST(test_explicit_bind_wins);
  RUN_TEST(test_override_beats_meta_beats_default);
  return UNITY_END();
}
