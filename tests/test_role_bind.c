#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "role.h"

static const role_def_t *engine;
static cfg_t cfg;
static cfg_role_t rc;
static role_binding_t b;

static int slot(const char *name) {
  for (int i = 0; i < engine->groups[0].slot_count; i++)
    if (!strcmp(engine->groups[0].slots[i].slot, name)) return i;
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
  TEST_ASSERT_EQUAL_UINT8(2, b.g[0].inst_count);
  TEST_ASSERT_EQUAL_STRING("port", b.g[0].inst_id[0]);
  TEST_ASSERT_EQUAL_STRING("starboard", b.g[0].inst_id[1]);
  TEST_ASSERT_EQUAL_STRING("PORT", b.g[0].inst_label[0]);
  TEST_ASSERT_EQUAL_STRING("STBD", b.g[0].inst_label[1]);
  TEST_ASSERT_EQUAL_STRING("propulsion.starboard.revolutions", bd_path(b.g[0].ids[1][slot("rpm")]));
}

static void test_excluded_instance_dropped(void) {
  bd_intern("propulsion.port.revolutions");
  bd_intern("propulsion.wing.revolutions");
  strcpy(rc.exclude[0], "wing");
  rc.exclude_count = 1;
  bind();
  TEST_ASSERT_EQUAL_UINT8(1, b.g[0].inst_count);
}

static void test_pinned_instance_missing_still_bound(void) {
  strcpy(rc.inst[0].id, "port");
  strcpy(rc.inst[0].label, "PORT");
  rc.inst_count = 1;
  bind();
  TEST_ASSERT_EQUAL_UINT8(1, b.g[0].inst_count);
  bd_id_t id = b.g[0].ids[0][slot("rpm")];
  TEST_ASSERT_NOT_EQUAL(BD_NONE, id);
  TEST_ASSERT_EQUAL_STRING("propulsion.port.revolutions", bd_path(id));
  bd_sample_t s;
  bd_get(id, &s);
  TEST_ASSERT_FALSE(s.valid);
}

static void test_zero_engines_gives_placeholder(void) {
  bind();
  TEST_ASSERT_EQUAL_UINT8(1, b.g[0].inst_count);
  TEST_ASSERT_EQUAL_STRING("ENGINE", b.g[0].inst_label[0]);
  for (int i = 0; i < engine->groups[0].slot_count; i++) TEST_ASSERT_EQUAL(BD_NONE, b.g[0].ids[0][i]);
}

static void test_fallback_path_used_when_primary_absent(void) {
  bd_intern("propulsion.port.revolutions");
  bd_intern("propulsion.port.coolantTemperature");
  bind();
  TEST_ASSERT_EQUAL_STRING("propulsion.port.coolantTemperature", bd_path(b.g[0].ids[0][slot("coolant")]));
}

static void test_explicit_bind_wins(void) {
  bd_intern("propulsion.port.revolutions");
  bd_intern("propulsion.port.temperature");
  strcpy(rc.bind[0].slot, "port.coolant");
  strcpy(rc.bind[0].path, "propulsion.port.exhaustTemperature");
  rc.bind_count = 1;
  bind();
  TEST_ASSERT_EQUAL_STRING("propulsion.port.exhaustTemperature", bd_path(b.g[0].ids[0][slot("coolant")]));
}

static void test_override_beats_meta_beats_default(void) {
  bd_id_t rpm = bd_intern("propulsion.port.revolutions");
  bd_intern("propulsion.port.temperature");
  bind();
  int r = slot("rpm"), t = slot("coolant");
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 4000.0 / 60.0, b.g[0].max[0][r]);           // default, in Hz
  bd_meta_t m = {.has_scale = true, .scale_lo = 0, .scale_hi = 50};
  bd_set_meta(rpm, &m);
  bind();
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 50.0, b.g[0].max[0][r]);                    // meta
  strcpy(rc.ovr[0].slot, "rpm");
  rc.ovr[0].has_max = true;
  rc.ovr[0].max = 3600;
  rc.ovr_count = 1;
  bind();
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 60.0, b.g[0].max[0][r]);                    // override 3600 RPM
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 0.0, b.g[0].min[0][r]);                     // min still from meta
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 393.15, b.g[0].max[0][t]);                  // 120 °C default
}

// A test-only role with two groups: alpha (depth 1) and beta (depth 2).
static const role_slot_t one_slot[] = {{"v", "v", NULL, Q_NONE, 0, 0, 0, "V", 0}};
static const role_slot_t beta_slot[] = {{"level", "level", NULL, Q_NONE, 0, 1, 0, "LEVEL", 0}};
static const role_group_t two_groups[] = {
  {"a", "x.alpha", 1, 8, one_slot, 1},
  {"b", "x.beta", 2, 8, beta_slot, 1},
};
static const role_def_t two = {.name = "two", .title = "TWO", .groups = two_groups, .group_count = 2};
static role_binding_t b2, b3;

static void bind2(role_binding_t *out) { role_bind(&two, &rc, &cfg.units, out); }

static void test_group_depth_two_ids(void) {
  bd_intern("x.beta.fresh.0.level");
  bd_intern("x.beta.fuel.1.level");
  bind2(&b2);
  TEST_ASSERT_EQUAL_UINT8(2, b2.g[1].inst_count);
  TEST_ASSERT_EQUAL_STRING("fresh.0", b2.g[1].inst_id[0]);
  TEST_ASSERT_EQUAL_STRING("fuel.1", b2.g[1].inst_id[1]);
}

static void test_multi_group_exclude_is_prefixed(void) {
  bd_intern("x.beta.fresh.0.level");
  bd_intern("x.beta.fuel.1.level");
  strcpy(rc.exclude[0], "fuel.1");
  rc.exclude_count = 1;
  bind2(&b2);
  TEST_ASSERT_EQUAL_UINT8(2, b2.g[1].inst_count);
  strcpy(rc.exclude[0], "b.fuel.1");
  bind2(&b2);
  TEST_ASSERT_EQUAL_UINT8(1, b2.g[1].inst_count);
  TEST_ASSERT_EQUAL_STRING("fresh.0", b2.g[1].inst_id[0]);
}

static void test_max_inst_caps_discovery(void) {
  char p[48];
  for (int i = 9; i >= 0; i--) {
    snprintf(p, sizeof p, "x.alpha.i%d.v", i);
    bd_intern(p);
  }
  bind2(&b2);
  TEST_ASSERT_EQUAL_UINT8(8, b2.g[0].inst_count);
  TEST_ASSERT_EQUAL_STRING("i0", b2.g[0].inst_id[0]);
  TEST_ASSERT_EQUAL_STRING("i7", b2.g[0].inst_id[7]);
}

static void test_binding_same_detects_new_instance_in_second_group(void) {
  bd_intern("x.alpha.first.v");
  bind2(&b2);
  bd_intern("x.alpha.second.v");
  bind2(&b3);
  TEST_ASSERT_FALSE(role_binding_same(&b2, &b3));
  bind2(&b2);
  TEST_ASSERT_TRUE(role_binding_same(&b2, &b3));
}

static void test_binding_same_notices_a_changed_label(void) {   // a name the source published can change under a bound instance
  bd_intern("x.alpha.first.v");
  bind2(&b2);
  bind2(&b3);
  TEST_ASSERT_TRUE(role_binding_same(&b2, &b3));
  snprintf(b3.g[0].inst_label[0], sizeof b3.g[0].inst_label[0], "RENAMED");
  TEST_ASSERT_FALSE(role_binding_same(&b2, &b3));
}

static void test_placeholder_per_group(void) {
  bind2(&b2);
  for (int g = 0; g < 2; g++) {
    TEST_ASSERT_EQUAL_UINT8(1, b2.g[g].inst_count);
    TEST_ASSERT_EQUAL_STRING("TWO", b2.g[g].inst_label[0]);
    TEST_ASSERT_EQUAL(BD_NONE, b2.g[g].ids[0][0]);
  }
}

static void test_multi_group_pinned_and_bind_keys_are_prefixed(void) {
  strcpy(rc.inst[0].id, "b.tank.2");
  rc.inst_count = 1;
  strcpy(rc.bind[0].slot, "b.tank.2.level");
  strcpy(rc.bind[0].path, "x.other.level");
  rc.bind_count = 1;
  bind2(&b2);
  TEST_ASSERT_EQUAL_UINT8(1, b2.g[1].inst_count);
  TEST_ASSERT_EQUAL_STRING("tank.2", b2.g[1].inst_id[0]);
  TEST_ASSERT_EQUAL_STRING("x.other.level", bd_path(b2.g[1].ids[0][0]));
  TEST_ASSERT_EQUAL_UINT8(1, b2.g[0].inst_count);   // alpha has no pinned instance: placeholder
}

static void test_power_groups_bind(void) {
  const role_def_t *pw = role_find("power");
  TEST_ASSERT_NOT_NULL(pw);
  TEST_ASSERT_EQUAL_UINT8(3, pw->page_count);
  bd_intern("electrical.resolved.batteries.279.voltage");
  bd_intern("electrical.resolved.solar.288.panelPower");
  bd_intern("electrical.resolved.chargers.276.current");
  role_binding_t pb;
  strcpy(rc.role, "power");
  role_bind(pw, &rc, &cfg.units, &pb);
  TEST_ASSERT_EQUAL_STRING("battery", pw->groups[0].name);
  TEST_ASSERT_EQUAL_STRING("279", pb.g[0].inst_id[0]);
  TEST_ASSERT_EQUAL_STRING("288", pb.g[1].inst_id[0]);
  TEST_ASSERT_EQUAL_STRING("276", pb.g[2].inst_id[0]);
  for (int g = 3; g < 6; g++) {   // inverter, dcdc, alternator found nothing
    TEST_ASSERT_EQUAL_UINT8(1, pb.g[g].inst_count);
    TEST_ASSERT_EQUAL(BD_NONE, pb.g[g].ids[0][0]);
  }
  TEST_ASSERT_EQUAL_STRING("electrical.resolved.solar.288.panelPower", bd_path(pb.g[1].ids[0][2]));
  // SoC and time remaining live under capacity. (Signal K schema, signalk-venus).
  TEST_ASSERT_EQUAL_STRING("electrical.resolved.batteries.279.capacity.stateOfCharge", bd_path(pb.g[0].ids[0][3]));
  TEST_ASSERT_EQUAL_STRING("electrical.resolved.batteries.279.capacity.timeRemaining", bd_path(pb.g[0].ids[0][4]));
}

static void test_power_soc_capacity_path_and_fallback(void) {
  const role_def_t *pw = role_find("power");
  bd_intern("electrical.resolved.batteries.279.capacity.stateOfCharge");
  bd_intern("electrical.resolved.batteries.279.timeRemaining");   // only the bare name published
  role_binding_t pb;
  strcpy(rc.role, "power");
  role_bind(pw, &rc, &cfg.units, &pb);
  TEST_ASSERT_EQUAL_STRING("279", pb.g[0].inst_id[0]);
  TEST_ASSERT_EQUAL_STRING("electrical.resolved.batteries.279.capacity.stateOfCharge", bd_path(pb.g[0].ids[0][3]));
  TEST_ASSERT_EQUAL_STRING("electrical.resolved.batteries.279.timeRemaining", bd_path(pb.g[0].ids[0][4]));
}

static void test_depth_zero_group(void) {
  const role_def_t *pw = role_find("power");
  role_binding_t pb;
  strcpy(rc.role, "power");
  role_bind(pw, &rc, &cfg.units, &pb);
  TEST_ASSERT_EQUAL_UINT8(1, pb.g[6].inst_count);
  TEST_ASSERT_EQUAL_STRING("", pb.g[6].inst_id[0]);
  TEST_ASSERT_NOT_EQUAL(BD_NONE, pb.g[6].ids[0][0]);
  TEST_ASSERT_EQUAL_STRING("electrical.venus.dcPower", bd_path(pb.g[6].ids[0][0]));
}

// What a fresh bind would find, ignoring pins and excludes: setup's Discovery screen.
static void test_role_discover_ignores_pins_and_excludes(void) {
  bd_intern("propulsion.port.revolutions");
  bd_intern("propulsion.starboard.revolutions");
  strcpy(rc.inst[0].id, "port");
  rc.inst_count = 1;
  strcpy(rc.exclude[0], "starboard");
  rc.exclude_count = 1;
  char ids[16][24];
  TEST_ASSERT_EQUAL_UINT8(2, role_discover(engine, ids, 16));
  TEST_ASSERT_EQUAL_STRING("port", ids[0]);
  TEST_ASSERT_EQUAL_STRING("starboard", ids[1]);
}

static void test_role_discover_prefixes_multi_group_ids(void) {
  bd_intern("electrical.resolved.batteries.279.voltage");
  bd_intern("electrical.resolved.solar.288.panelPower");
  bd_intern("electrical.venus.dcPower");   // the depth-0 system group is not an instance
  char ids[16][24];
  uint8_t n = role_discover(role_find("power"), ids, 16);
  TEST_ASSERT_EQUAL_UINT8(2, n);
  TEST_ASSERT_EQUAL_STRING("battery.279", ids[0]);
  TEST_ASSERT_EQUAL_STRING("solar.288", ids[1]);
}

static void test_navigation_role_is_registered(void) {
  const role_def_t *d = role_find("navigation");
  TEST_ASSERT_NOT_NULL(d);
  TEST_ASSERT_EQUAL_STRING("NAVIGATION", d->title);
  TEST_ASSERT_EQUAL_UINT8(2, d->page_count);
  TEST_ASSERT_NOT_NULL(d->build_page);
  TEST_ASSERT_NOT_NULL(d->refresh);
  TEST_ASSERT_NOT_NULL(d->tile);
  bool listed = false;
  for (const char *const *n = role_names(); *n; n++) listed = listed || !strcmp(*n, "navigation");
  TEST_ASSERT_TRUE(listed);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_discovers_two_engines_sorted);
  RUN_TEST(test_role_discover_ignores_pins_and_excludes);
  RUN_TEST(test_role_discover_prefixes_multi_group_ids);
  RUN_TEST(test_excluded_instance_dropped);
  RUN_TEST(test_pinned_instance_missing_still_bound);
  RUN_TEST(test_zero_engines_gives_placeholder);
  RUN_TEST(test_fallback_path_used_when_primary_absent);
  RUN_TEST(test_explicit_bind_wins);
  RUN_TEST(test_override_beats_meta_beats_default);
  RUN_TEST(test_group_depth_two_ids);
  RUN_TEST(test_multi_group_exclude_is_prefixed);
  RUN_TEST(test_max_inst_caps_discovery);
  RUN_TEST(test_binding_same_detects_new_instance_in_second_group);
  RUN_TEST(test_binding_same_notices_a_changed_label);
  RUN_TEST(test_placeholder_per_group);
  RUN_TEST(test_multi_group_pinned_and_bind_keys_are_prefixed);
  RUN_TEST(test_power_groups_bind);
  RUN_TEST(test_power_soc_capacity_path_and_fallback);
  RUN_TEST(test_depth_zero_group);
  RUN_TEST(test_navigation_role_is_registered);
  return UNITY_END();
}
