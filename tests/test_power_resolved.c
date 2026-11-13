// The Power role binds to what the electrical resolver publishes, not to raw per-device paths.
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "boatdata.h"
#include "electrical.h"
#include "role.h"
#include "unity.h"

static el_t el;
static cfg_t cfg;
static cfg_role_t rc;

void setUp(void) {
  bd_init();
  el_init(&el);
  cfg_defaults(&cfg);
  memset(&rc, 0, sizeof rc);
  strcpy(rc.role, "power");
}
void tearDown(void) {}

static void feed(void) {
  // House bank from a monitor, a second bank from a plain sender, and a panel input: three devices, instance 0 twice.
  el_identity_t id = {.model_id = "SmartShunt IP65 300A"};
  el_set_identity(&el, 0, 10, &id);
  el_observe(&el, 0, 10, 0, EL_F_DC_TYPE, 0, 1000);
  el_observe(&el, 0, 10, 0, EL_F_VOLTAGE, 13.4, 1000);
  el_observe(&el, 0, 10, 0, EL_F_CURRENT, -3.0, 1000);
  el_observe(&el, 0, 10, 0, EL_F_SOC, 0.82, 1000);
  el_observe(&el, 0, 20, 0, EL_F_DC_TYPE, 0, 1000);
  el_observe(&el, 0, 20, 0, EL_F_VOLTAGE, 12.1, 1000);
  el_observe(&el, 0, 30, 1, EL_F_DC_TYPE, 3, 1000);
  el_observe(&el, 0, 30, 1, EL_F_VOLTAGE, 20.5, 1000);
  el_publish(&el, 1000);
}

static const char *id_of_fn(el_fn_t fn, uint8_t instance, char out[9]) {
  for (size_t i = 0; i < el_endpoint_count(&el); i++)
    if (el_endpoint(&el, i)->fn == fn && el_endpoint(&el, i)->instance == instance) {
      el_endpoint_id(el_endpoint(&el, i), out);
      return out;
    }
  return NULL;
}

static void test_power_binds_resolved_batteries(void) {
  feed();
  role_binding_t b;
  role_bind(role_find("power"), &rc, &cfg.units, &b);
  TEST_ASSERT_EQUAL_UINT8(2, b.g[0].inst_count);   // two banks, though both report instance 0
  TEST_ASSERT_EQUAL_UINT8(1, b.g[1].inst_count);   // the panel input is solar, not a third battery
  char pv[9];
  TEST_ASSERT_NOT_NULL(id_of_fn(EL_FN_PV, 1, pv));
  TEST_ASSERT_EQUAL_STRING(pv, b.g[1].inst_id[0]);
  bd_sample_t s;
  TEST_ASSERT_TRUE(bd_get(b.g[1].ids[0][0], &s));
  TEST_ASSERT_EQUAL_DOUBLE(20.5, s.num);
  for (int i = 0; i < 2; i++) {   // each battery slot set reads its own device's voltage
    TEST_ASSERT_TRUE(bd_get(b.g[0].ids[i][0], &s));
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_TRUE(s.num == 13.4 || s.num == 12.1);
  }
}

static void test_raw_electrical_batteries_ignored_when_resolved_exist(void) {
  feed();
  bd_id_t raw = bd_intern("electrical.batteries.0.voltage");   // what a server's merged tree would hold
  bd_set_number(raw, 13.9, BD_SRC_SK, 1000);
  role_binding_t b;
  role_bind(role_find("power"), &rc, &cfg.units, &b);
  TEST_ASSERT_EQUAL_UINT8(2, b.g[0].inst_count);
  for (int i = 0; i < b.g[0].inst_count; i++) TEST_ASSERT_TRUE(strncmp(b.g[0].inst_id[i], "0", 2) != 0);
}

static const char *label_of(const role_binding_t *b, int g, const char *id) {
  for (int i = 0; i < b->g[g].inst_count; i++)
    if (!strcmp(b->g[g].inst_id[i], id)) return b->g[g].inst_label[i];
  return NULL;
}

static void test_a_discovered_instance_takes_the_name_the_resolver_published(void) {
  feed();
  role_binding_t b;
  role_bind(role_find("power"), &rc, &cfg.units, &b);
  char id[9];
  TEST_ASSERT_NOT_NULL(id_of_fn(EL_FN_BATTERY, 0, id));
  TEST_ASSERT_EQUAL_STRING("SmartShunt", label_of(&b, 0, id));   // the monitor
}

static void test_an_instance_with_no_published_name_keeps_its_id_as_before(void) {
  feed();
  char pv[9];
  TEST_ASSERT_NOT_NULL(id_of_fn(EL_FN_PV, 1, pv));
  role_binding_t b;
  role_bind(role_find("power"), &rc, &cfg.units, &b);
  TEST_ASSERT_EQUAL_STRING("Solar", label_of(&b, 1, pv));   // the resolver names an unidentified device by its function
  bd_id_t raw = bd_intern("electrical.resolved.batteries.deadbeef.voltage");   // an instance that arrived without a label
  bd_set_number(raw, 12.0, BD_SRC_ELECTRICAL, 1000);
  role_bind(role_find("power"), &rc, &cfg.units, &b);
  TEST_ASSERT_EQUAL_STRING("DEADBEEF", label_of(&b, 0, "deadbeef"));
}

static void test_a_configured_label_wins(void) {
  feed();
  char id[9];
  TEST_ASSERT_NOT_NULL(id_of_fn(EL_FN_BATTERY, 0, id));
  snprintf(rc.inst[0].id, sizeof rc.inst[0].id, "battery.%s", id);
  snprintf(rc.inst[0].label, sizeof rc.inst[0].label, "HOUSE");
  rc.inst_count = 1;
  role_binding_t b;
  role_bind(role_find("power"), &rc, &cfg.units, &b);
  TEST_ASSERT_EQUAL_STRING("HOUSE", label_of(&b, 0, id));
}

static void test_a_pinned_instance_without_a_label_gets_the_published_one(void) {
  feed();
  char id[9];
  TEST_ASSERT_NOT_NULL(id_of_fn(EL_FN_BATTERY, 0, id));
  snprintf(rc.inst[0].id, sizeof rc.inst[0].id, "battery.%s", id);
  rc.inst_count = 1;
  role_binding_t b;
  role_bind(role_find("power"), &rc, &cfg.units, &b);
  TEST_ASSERT_EQUAL_STRING("SmartShunt", label_of(&b, 0, id));
}

static void test_a_withdrawn_name_is_not_used_by_a_pinned_instance(void) {
  feed();
  char id[9];
  TEST_ASSERT_NOT_NULL(id_of_fn(EL_FN_BATTERY, 0, id));
  char path[96];
  snprintf(path, sizeof path, "electrical.resolved.batteries.%s.label", id);
  bd_set_invalid(bd_find(path));
  snprintf(rc.inst[0].id, sizeof rc.inst[0].id, "battery.%s", id);   // pinned in config: still bound, but not by that name
  rc.inst_count = 1;
  role_binding_t b;
  role_bind(role_find("power"), &rc, &cfg.units, &b);
  char up[9];
  for (int i = 0; i < 9; i++) up[i] = (char)toupper((unsigned char)id[i]);
  TEST_ASSERT_EQUAL_STRING(up, label_of(&b, 0, id));
}

static void test_role_label_of_names_a_discovery_entry(void) {
  feed();
  char key[24], id[9], out[16];
  TEST_ASSERT_NOT_NULL(id_of_fn(EL_FN_BATTERY, 0, id));
  snprintf(key, sizeof key, "battery.%s", id);   // how role_discover writes a multi-group role's ids
  TEST_ASSERT_TRUE(role_label_of(role_find("power"), key, out, sizeof out));
  TEST_ASSERT_EQUAL_STRING("SmartShunt", out);
  TEST_ASSERT_FALSE(role_label_of(role_find("power"), "battery.00000000", out, sizeof out));   // unknown
  TEST_ASSERT_FALSE(role_label_of(role_find("power"), "nogroup.abc", out, sizeof out));
  TEST_ASSERT_FALSE(role_label_of(role_find("power"), id, out, sizeof out));   // a bare id in a multi-group role names nothing
}

// Two devices reporting the same bank for long enough that the resolver merges them.
static void mirrored_banks(int64_t until_ms) {
  el_identity_t id = {.model_id = "SmartShunt IP65 300A"};
  el_set_identity(&el, 0, 10, &id);
  for (int64_t t = 0; t <= until_ms; t += 1000) {
    for (int dev = 0; dev < 2; dev++) {
      el_ref_t ref = dev ? 20 : 10;
      uint8_t inst = dev ? 239 : 0;
      el_observe(&el, 0, ref, inst, EL_F_DC_TYPE, 0, t);
      el_observe(&el, 0, ref, inst, EL_F_VOLTAGE, 14.37, t);
      el_observe(&el, 0, ref, inst, EL_F_CURRENT, 0.0, t);
      el_observe(&el, 0, ref, inst, EL_F_SOC, 1.0, t);
    }
    el_publish(&el, t);
  }
}

static void test_a_mirror_the_resolver_merged_away_is_not_an_instance(void) {
  mirrored_banks(1000);
  role_binding_t b;
  role_bind(role_find("power"), &rc, &cfg.units, &b);
  TEST_ASSERT_EQUAL_UINT8(2, b.g[0].inst_count);   // both are shown until they are known to be one bank
  mirrored_banks(13000);
  role_bind(role_find("power"), &rc, &cfg.units, &b);
  TEST_ASSERT_EQUAL_UINT8(1, b.g[0].inst_count);   // the loser's paths still exist, but its name was withdrawn
}

static void test_an_instance_that_became_another_function_leaves_its_old_group(void) {
  el_observe(&el, 0, 30, 0, EL_F_VOLTAGE, 14.4, 1000);   // first seen as a bare voltage
  el_observe(&el, 0, 30, 0, EL_F_DC_TYPE, 3, 1000);      // then known to be a panel input
  el_publish(&el, 1000);
  el_observe(&el, 0, 30, 0, EL_F_CONV_STATE, 4, 2000);   // and then the device turns out to be converter-class: this is the charger output
  el_observe(&el, 0, 30, 0, EL_F_DC_TYPE, 0, 2100);
  el_publish(&el, 2100);
  role_binding_t b;
  role_bind(role_find("power"), &rc, &cfg.units, &b);
  TEST_ASSERT_EQUAL_UINT8(1, b.g[2].inst_count);   // one charger
  char solar_probe[64];
  char id[9];
  TEST_ASSERT_NOT_NULL(id_of_fn(EL_FN_CHARGER, 0, id));
  snprintf(solar_probe, sizeof solar_probe, "electrical.resolved.solar.%s.label", id);
  TEST_ASSERT_TRUE(bd_find(solar_probe) != BD_NONE);        // the old path is still in the data model
  TEST_ASSERT_TRUE(b.g[1].inst_count == 1 && b.g[1].ids[0][0] == BD_NONE);   // but Solar has only its placeholder
}

static void test_a_sender_that_failed_without_a_name_still_shows_as_an_instance(void) {
  bd_id_t v = bd_intern("electrical.resolved.batteries.cafe0001.voltage");   // a source that never publishes names
  bd_set_number(v, 12.0, BD_SRC_ELECTRICAL, 1000);
  bd_set_invalid(v);
  role_binding_t b;
  role_bind(role_find("power"), &rc, &cfg.units, &b);
  TEST_ASSERT_EQUAL_UINT8(1, b.g[0].inst_count);   // a failed sender is not a retired one: it must still flag
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_power_binds_resolved_batteries);
  RUN_TEST(test_raw_electrical_batteries_ignored_when_resolved_exist);
  RUN_TEST(test_a_discovered_instance_takes_the_name_the_resolver_published);
  RUN_TEST(test_an_instance_with_no_published_name_keeps_its_id_as_before);
  RUN_TEST(test_a_configured_label_wins);
  RUN_TEST(test_a_pinned_instance_without_a_label_gets_the_published_one);
  RUN_TEST(test_a_withdrawn_name_is_not_used_by_a_pinned_instance);
  RUN_TEST(test_role_label_of_names_a_discovery_entry);
  RUN_TEST(test_a_mirror_the_resolver_merged_away_is_not_an_instance);
  RUN_TEST(test_an_instance_that_became_another_function_leaves_its_old_group);
  RUN_TEST(test_a_sender_that_failed_without_a_name_still_shows_as_an_instance);
  return UNITY_END();
}
