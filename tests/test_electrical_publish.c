#include <stdio.h>
#include <string.h>
#include "boatdata.h"
#include "electrical.h"
#include "unity.h"

static el_t el;
void setUp(void) {
  bd_init();
  el_init(&el);
}
void tearDown(void) {}

static void bat(el_ref_t ref, uint8_t inst, double v, double i, double soc, int64_t t) {
  el_observe(&el, 0, ref, inst, EL_F_DC_TYPE, 0, t);
  el_observe(&el, 0, ref, inst, EL_F_VOLTAGE, v, t);
  el_observe(&el, 0, ref, inst, EL_F_CURRENT, i, t);
  if (soc >= 0) el_observe(&el, 0, ref, inst, EL_F_SOC, soc, t);
}

static bool get(const char *group, const el_endpoint_t *p, const char *leaf, bd_sample_t *s) {
  char id[9], path[96];
  el_endpoint_id(p, id);
  snprintf(path, sizeof path, "electrical.resolved.%s.%s.%s", group, id, leaf);
  bd_id_t b = bd_find(path);
  return b != BD_NONE && bd_get(b, s);
}
static size_t count(const char *pattern) {
  bd_id_t ids[16];
  return bd_match(pattern, ids, 16);
}

static void test_battery_endpoint_published_under_resolved_prefix(void) {
  bat(10, 0, 13.5, 2.0, 0.8, 1000);
  el_publish(&el, 1000);
  bd_sample_t s;
  const el_endpoint_t *p = el_endpoint(&el, 0);
  TEST_ASSERT_TRUE(get("batteries", p, "voltage", &s));
  TEST_ASSERT_TRUE(s.valid);
  TEST_ASSERT_EQUAL_DOUBLE(13.5, s.num);
  TEST_ASSERT_EQUAL_UINT8(BD_SRC_ELECTRICAL, s.source);
  TEST_ASSERT_TRUE(get("batteries", p, "current", &s));
  TEST_ASSERT_EQUAL_DOUBLE(2.0, s.num);
  TEST_ASSERT_TRUE(get("batteries", p, "capacity.stateOfCharge", &s));
  TEST_ASSERT_EQUAL_DOUBLE(0.8, s.num);
  TEST_ASSERT_EQUAL_INT(BD_SRC_LIVE, bd_source_state(BD_SRC_ELECTRICAL));
}

static void test_pv_published_as_solar_not_battery(void) {
  el_observe(&el, 0, 10, 1, EL_F_DC_TYPE, 3, 1000);
  el_observe(&el, 0, 10, 1, EL_F_VOLTAGE, 20.5, 1000);
  el_publish(&el, 1000);
  bd_sample_t s;
  TEST_ASSERT_TRUE(get("solar", el_endpoint(&el, 0), "voltage", &s));
  TEST_ASSERT_EQUAL_DOUBLE(20.5, s.num);
  TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)count("electrical.resolved.batteries.*.voltage"));
}

static void test_charger_published_with_mode_text(void) {
  el_observe(&el, 0, 10, 0, EL_F_CONV_STATE, 4, 1000);
  el_observe(&el, 0, 10, 0, EL_F_DC_TYPE, 0, 1000);   // output side of a converter-class device
  el_observe(&el, 0, 10, 0, EL_F_VOLTAGE, 13.9, 1000);
  el_observe(&el, 0, 10, 0, EL_F_CURRENT, 10.2, 1000);
  el_publish(&el, 1000);
  bd_sample_t s;
  const el_endpoint_t *p = el_endpoint(&el, 0);
  TEST_ASSERT_TRUE(get("chargers", p, "current", &s));
  TEST_ASSERT_EQUAL_DOUBLE(10.2, s.num);
  TEST_ASSERT_TRUE(get("chargers", p, "chargingMode", &s));
  TEST_ASSERT_EQUAL_INT(BD_TEXT, s.kind);
  TEST_ASSERT_EQUAL_STRING("absorption", s.text);
}

static void test_power_leaves_published(void) {
  bat(10, 0, 13.5, 2.0, 0.8, 1000);
  el_observe(&el, 0, 10, 0, EL_F_POWER, 27.0, 1000);
  el_observe(&el, 0, 20, 1, EL_F_DC_TYPE, 3, 1000);
  el_observe(&el, 0, 20, 1, EL_F_POWER, 260.0, 1000);
  el_publish(&el, 1000);
  bd_sample_t s;
  TEST_ASSERT_TRUE(get("batteries", el_endpoint(&el, 0), "power", &s));
  TEST_ASSERT_EQUAL_DOUBLE(27.0, s.num);
  const el_endpoint_t *pv = NULL;
  for (size_t i = 0; i < el_endpoint_count(&el); i++)
    if (el_endpoint(&el, i)->fn == EL_FN_PV) pv = el_endpoint(&el, i);
  TEST_ASSERT_NOT_NULL(pv);
  TEST_ASSERT_TRUE(get("solar", pv, "panelPower", &s));
  TEST_ASSERT_EQUAL_DOUBLE(260.0, s.num);
}

static void test_unknown_and_hidden_not_published(void) {
  el_observe(&el, 0, 10, 0, EL_F_DC_TYPE, 4, 1000);   // wind: unknown to the Power role
  el_observe(&el, 0, 10, 0, EL_F_VOLTAGE, 14.0, 1000);
  el_publish(&el, 1000);
  TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)count("electrical.resolved.*.*.voltage"));
}

static void test_mirror_published_first_is_invalidated_when_merged(void) {
  el_identity_t id = {.model_id = "Some Shunt 500A"};
  el_set_identity(&el, 0, 10, &id);
  bat(10, 0, 13.82, 0.0, 1.0, 1000);
  bat(20, 7, 13.82, 0.0, 1.0, 1000);
  el_publish(&el, 1000);
  TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)count("electrical.resolved.batteries.*.voltage"));
  for (int64_t t = 2000; t <= 13000; t += 1000) {
    bat(10, 0, 13.82, 0.0, 1.0, t);
    bat(20, 7, 13.82, 0.0, 1.0, t);
  }
  el_publish(&el, 13000);
  bd_id_t ids[4];
  size_t n = bd_match("electrical.resolved.batteries.*.voltage", ids, 4), valid = 0;
  for (size_t i = 0; i < n; i++) {
    bd_sample_t s;
    if (bd_get(ids[i], &s) && s.valid) valid++;
  }
  TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)valid);
}

static const el_endpoint_t *ep_of(uint32_t ref, uint8_t inst) {
  for (size_t i = 0; i < el_endpoint_count(&el); i++)
    if (el_endpoint(&el, i)->src[EL_F_VOLTAGE] == ref && el_endpoint(&el, i)->instance == inst) return el_endpoint(&el, i);
  return NULL;
}

static size_t valid_count(const char *pattern);

static void test_label_published_next_to_the_values(void) {
  el_identity_t id = {.can_name = 0xAA01, .model_id = "SmartShunt IP65 300A"};
  el_set_identity(&el, 0, 10, &id);
  bat(10, 0, 13.5, 2.0, 0.8, 1000);
  el_publish(&el, 1000);
  bd_sample_t s;
  TEST_ASSERT_TRUE(get("batteries", ep_of(10, 0), "label", &s));
  TEST_ASSERT_EQUAL_INT(BD_TEXT, s.kind);
  TEST_ASSERT_TRUE(s.valid);
  TEST_ASSERT_EQUAL_STRING("SmartShunt", s.text);
}

static void test_label_changes_when_a_second_device_of_the_model_arrives(void) {
  el_identity_t a = {.can_name = 0xC046A0002CC001F5ull, .model_id = "BlueSolar Charger MPPT 100/15"};
  el_identity_t b = {.can_name = 0xC046A0002CC001F6ull, .model_id = "BlueSolar Charger MPPT 100/15"};
  el_set_identity(&el, 0, 36, &a);
  el_observe(&el, 0, 36, 1, EL_F_DC_TYPE, 3, 1000);
  el_observe(&el, 0, 36, 1, EL_F_VOLTAGE, 20.0, 1000);
  el_publish(&el, 1000);
  bd_sample_t s;
  TEST_ASSERT_TRUE(get("solar", ep_of(36, 1), "label", &s));
  TEST_ASSERT_EQUAL_STRING("BlueSolar", s.text);
  el_set_identity(&el, 0, 37, &b);
  el_observe(&el, 0, 37, 1, EL_F_DC_TYPE, 3, 2000);
  el_observe(&el, 0, 37, 1, EL_F_VOLTAGE, 20.1, 2000);
  el_publish(&el, 2000);
  TEST_ASSERT_TRUE(get("solar", ep_of(36, 1), "label", &s));
  TEST_ASSERT_EQUAL_STRING("BlueSolar f5", s.text);   // the first one is renamed now that it needs telling apart
  TEST_ASSERT_TRUE(get("solar", ep_of(37, 1), "label", &s));
  TEST_ASSERT_EQUAL_STRING("BlueSolar f6", s.text);
}

static void test_unchanged_label_is_not_rewritten(void) {
  bat(10, 0, 13.5, 2.0, 0.8, 1000);
  el_publish(&el, 1000);
  el_publish(&el, 9000);
  bd_sample_t s;
  TEST_ASSERT_TRUE(get("batteries", ep_of(10, 0), "label", &s));
  TEST_ASSERT_EQUAL_INT64(1000, s.rx_ms);
}

static void test_label_goes_with_a_merged_mirror(void) {
  el_identity_t id = {.model_id = "Some Shunt 500A"};
  el_set_identity(&el, 0, 10, &id);
  bat(10, 0, 13.82, 0.0, 1.0, 1000);
  bat(20, 7, 13.82, 0.0, 1.0, 1000);
  el_publish(&el, 1000);
  TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)valid_count("electrical.resolved.batteries.*.label"));
  for (int64_t t = 2000; t <= 13000; t += 1000) {
    bat(10, 0, 13.82, 0.0, 1.0, t);
    bat(20, 7, 13.82, 0.0, 1.0, t);
  }
  el_publish(&el, 13000);
  TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)valid_count("electrical.resolved.batteries.*.label"));
}

static void test_unchanged_leaf_is_not_refreshed(void) {
  bat(10, 0, 13.5, 2.0, 0.8, 1000);
  el_publish(&el, 1000);
  el_publish(&el, 9000);   // nothing new was observed: a quiet value must not look fresh
  bd_sample_t s;
  TEST_ASSERT_TRUE(get("batteries", el_endpoint(&el, 0), "voltage", &s));
  TEST_ASSERT_EQUAL_INT64(1000, s.rx_ms);
}

static void test_feed_down_invalidates_leaves_and_up_restores(void) {
  bat(10, 0, 13.5, 2.0, 0.8, 1000);
  el_publish(&el, 1000);
  el_feed_down(&el, 0);
  bd_sample_t s;
  TEST_ASSERT_TRUE(get("batteries", el_endpoint(&el, 0), "voltage", &s));
  TEST_ASSERT_FALSE(s.valid);
  el_publish(&el, 5000);   // still down, nothing new: stays invalid
  TEST_ASSERT_TRUE(get("batteries", el_endpoint(&el, 0), "voltage", &s));
  TEST_ASSERT_FALSE(s.valid);
  bat(10, 0, 13.6, 2.0, 0.8, 6000);
  el_publish(&el, 6000);
  TEST_ASSERT_TRUE(get("batteries", el_endpoint(&el, 0), "voltage", &s));
  TEST_ASSERT_TRUE(s.valid);
  TEST_ASSERT_EQUAL_DOUBLE(13.6, s.num);
}

static void test_feed_down_leaves_other_feed_alone(void) {
  el_observe(&el, 1, 5, 0, EL_F_DC_TYPE, 0, 1000);
  el_observe(&el, 1, 5, 0, EL_F_VOLTAGE, 13.0, 1000);
  el_observe(&el, 1, 5, 0, EL_F_CURRENT, 1.0, 1000);
  el_publish(&el, 1000);
  el_feed_down(&el, 0);
  bd_sample_t s;
  TEST_ASSERT_TRUE(get("batteries", el_endpoint(&el, 0), "voltage", &s));
  TEST_ASSERT_TRUE(s.valid);
}

static size_t valid_count(const char *pattern) {
  bd_id_t ids[16];
  size_t n = bd_match(pattern, ids, 16), valid = 0;
  for (size_t i = 0; i < n; i++) {
    bd_sample_t s;
    if (bd_get(ids[i], &s) && s.valid) valid++;
  }
  return valid;
}

static void test_identity_arriving_late_moves_paths_without_leaving_a_ghost(void) {
  bat(10, 0, 13.5, 2.0, 0.8, 1000);
  el_publish(&el, 1000);
  TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)valid_count("electrical.resolved.batteries.*.voltage"));
  el_identity_t id = {.can_name = 0x1234567890ABCDEFull};
  el_set_identity(&el, 0, 10, &id);   // the claim arrives after the data: the endpoint's key, so its id, changes
  el_publish(&el, 2000);
  TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)valid_count("electrical.resolved.batteries.*.voltage"));   // the old id is not left behind
  TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)bd_match("electrical.resolved.batteries.*.voltage", (bd_id_t[16]){0}, 16));
}

static void test_identity_merging_two_endpoints_invalidates_the_absorbed_one(void) {
  el_identity_t id = {.can_name = 0x1234567890ABCDEFull};
  el_set_identity(&el, 0, 10, &id);
  bat(10, 0, 13.5, 2.0, 0.8, 1000);
  bat(20, 0, 13.5, 2.0, 0.8, 1000);   // the same device seen at another address before its claim
  el_publish(&el, 1000);
  TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)valid_count("electrical.resolved.batteries.*.voltage"));
  el_set_identity(&el, 0, 20, &id);
  el_publish(&el, 2000);
  TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)valid_count("electrical.resolved.batteries.*.voltage"));
}

static void test_path_stable_across_address_change(void) {
  el_identity_t id = {.can_name = 0xC046A0002CC001F8ull};
  el_set_identity(&el, 0, 36, &id);
  bat(36, 0, 13.5, 2.0, 0.8, 1000);
  el_publish(&el, 1000);
  el_set_identity(&el, 0, 52, &id);
  bat(52, 0, 13.6, 2.0, 0.8, 2000);
  el_publish(&el, 2000);
  TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)count("electrical.resolved.batteries.*.voltage"));
  bd_sample_t s;
  TEST_ASSERT_TRUE(get("batteries", el_endpoint(&el, 0), "voltage", &s));
  TEST_ASSERT_EQUAL_DOUBLE(13.6, s.num);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_battery_endpoint_published_under_resolved_prefix);
  RUN_TEST(test_pv_published_as_solar_not_battery);
  RUN_TEST(test_charger_published_with_mode_text);
  RUN_TEST(test_power_leaves_published);
  RUN_TEST(test_unknown_and_hidden_not_published);
  RUN_TEST(test_mirror_published_first_is_invalidated_when_merged);
  RUN_TEST(test_label_published_next_to_the_values);
  RUN_TEST(test_label_changes_when_a_second_device_of_the_model_arrives);
  RUN_TEST(test_unchanged_label_is_not_rewritten);
  RUN_TEST(test_label_goes_with_a_merged_mirror);
  RUN_TEST(test_unchanged_leaf_is_not_refreshed);
  RUN_TEST(test_feed_down_invalidates_leaves_and_up_restores);
  RUN_TEST(test_feed_down_leaves_other_feed_alone);
  RUN_TEST(test_identity_arriving_late_moves_paths_without_leaving_a_ghost);
  RUN_TEST(test_identity_merging_two_endpoints_invalidates_the_absorbed_one);
  RUN_TEST(test_path_stable_across_address_change);
  return UNITY_END();
}
