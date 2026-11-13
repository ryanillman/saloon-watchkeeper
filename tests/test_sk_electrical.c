#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "boatdata.h"
#include "electrical.h"
#include "sk_electrical.h"
#include "sk_parse.h"
#include "unity.h"

static el_t el;
void setUp(void) {
  bd_init();
  el_init(&el);
  sk_parse_reset();
  sk_electrical_attach(&el);
}
void tearDown(void) { sk_electrical_attach(NULL); }

static void delta(const char *source, const char *path, const char *value_json) {
  char j[400];
  snprintf(j, sizeof j,
           "{\"context\":\"vessels.self\",\"updates\":[{\"$source\":\"%s\",\"timestamp\":\"2026-11-12T10:00:00.000Z\","
           "\"values\":[{\"path\":\"%s\",\"value\":%s}]}]}",
           source, path, value_json);
  sk_parse_message(j, strlen(j), 1000);
}
static const el_endpoint_t *ep(uint8_t domain, uint8_t instance, size_t nth) {
  for (size_t i = 0; i < el_endpoint_count(&el); i++) {
    const el_endpoint_t *p = el_endpoint(&el, i);
    if (p->domain == domain && p->instance == instance && nth-- == 0) return p;
  }
  return NULL;
}

static void test_deltas_split_by_source_not_merged(void) {
  delta("can0.224", "electrical.batteries.0.voltage", "13.82");
  delta("can0.36", "electrical.batteries.0.voltage", "13.83");
  TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)el_endpoint_count(&el));
}

static void test_sources_tree_sets_identity_key(void) {
  const char *src = "{\"can0\":{\"224\":{\"n2k\":{\"canName\":\"c0f08261e76531d5\",\"modelId\":\"Some Shunt 500A\","
                    "\"modelSerialCode\":\"HQ1\"}},\"36\":{\"n2k\":{\"canName\":\"0123456789abcdef\"}},\"label\":\"can0\"}}";
  TEST_ASSERT_TRUE(sk_electrical_load_sources(src, strlen(src)));
  delta("can0.224", "electrical.batteries.1.voltage", "13.94");
  TEST_ASSERT_EQUAL_UINT64(0, el_endpoint(&el, 0)->devkey >> 63);   // a stable key, not a provisional one
  TEST_ASSERT_EQUAL(EL_FN_BATTERY, el_endpoint(&el, 0)->fn);        // and the model names it
  TEST_ASSERT_EQUAL(EL_EV_PRODUCT, el_endpoint(&el, 0)->ev);
}

static void test_source_stamped_with_can_name_gets_identity(void) {
  // Newer servers stamp "$source" as <bus>.<canName> once a device's NAME is known; the sources tree is keyed by address.
  const char *src = "{\"can0\":{\"224\":{\"n2k\":{\"canName\":\"c046aa002cc001f9\",\"modelId\":\"Some Shunt 500A\"}}}}";
  TEST_ASSERT_TRUE(sk_electrical_load_sources(src, strlen(src)));
  delta("can0.c046aa002cc001f9", "electrical.batteries.1.voltage", "13.94");
  TEST_ASSERT_EQUAL(EL_FN_BATTERY, el_endpoint(&el, 0)->fn);
  TEST_ASSERT_EQUAL(EL_EV_PRODUCT, el_endpoint(&el, 0)->ev);
  delta("can0.224", "electrical.batteries.1.current", "0");   // an older server's form for the same device: same endpoint
  TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)el_endpoint_count(&el));
}

static void test_adopted_solar_path_is_pv(void) {
  delta("venus.0", "electrical.solar.main.voltage", "20.5");
  const el_endpoint_t *p = el_endpoint(&el, 0);
  TEST_ASSERT_EQUAL(EL_FN_PV, p->fn);
  TEST_ASSERT_EQUAL(EL_EV_PATH, p->ev);
  TEST_ASSERT_EQUAL_DOUBLE(20.5, p->val[EL_F_VOLTAGE]);
}

static void test_adopted_venus_leaves_power_and_mode(void) {
  delta("venus.0", "electrical.solar.288.panelPower", "260");
  delta("venus.0", "electrical.solar.288.chargingMode", "\"Bulk\"");
  delta("venus.0", "electrical.chargers.276.chargingMode", "\"absorption\"");
  delta("venus.0", "electrical.batteries.279.power", "-55");
  const el_endpoint_t *solar = NULL, *chg = NULL, *bat = NULL;
  for (size_t i = 0; i < el_endpoint_count(&el); i++) {
    const el_endpoint_t *p = el_endpoint(&el, i);
    if (p->fn == EL_FN_PV) solar = p;
    if (p->fn == EL_FN_CHARGER) chg = p;
    if (p->has[EL_F_POWER] && p->val[EL_F_POWER] == -55) bat = p;
  }
  TEST_ASSERT_NOT_NULL(solar);
  TEST_ASSERT_EQUAL_DOUBLE(260, solar->val[EL_F_POWER]);
  TEST_ASSERT_EQUAL_DOUBLE(3, solar->val[EL_F_CONV_STATE]);   // "Bulk"
  TEST_ASSERT_NOT_NULL(chg);
  TEST_ASSERT_EQUAL_DOUBLE(4, chg->val[EL_F_CONV_STATE]);
  TEST_ASSERT_NOT_NULL(bat);
}

static void test_adopted_charger_path(void) {
  delta("venus.0", "electrical.chargers.0.current", "7.5");
  TEST_ASSERT_EQUAL(EL_FN_CHARGER, el_endpoint(&el, 0)->fn);
}

static void test_out_of_range_values_rejected(void) {
  delta("can0.224", "electrical.batteries.0.capacity.stateOfCharge", "2.5");
  delta("can0.224", "electrical.batteries.0.voltage", "400");
  delta("can0.224", "electrical.batteries.0.voltage", "-50");
  TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)el_endpoint_count(&el));
}

static void test_rest_snapshot_does_not_feed_resolver(void) {
  const char *snap = "{\"electrical\":{\"batteries\":{\"0\":{\"voltage\":{\"value\":13.2,\"$source\":\"can0.224\"}}}}}";
  TEST_ASSERT_TRUE(sk_parse_full(snap, strlen(snap)));
  TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)el_endpoint_count(&el));   // snapshots lose the per-delta source; only the stream feeds it
}

static void test_converter_state_string_mapped_and_charger_found(void) {
  delta("can0.36", "electrical.converter.36.0.operatingState", "\"float\"");
  delta("can0.36", "electrical.batteries.0.voltage", "13.8");
  delta("can0.36", "electrical.batteries.0.current", "5.0");
  const el_endpoint_t *p = ep(EL_DOM_DC, 0, 0);
  TEST_ASSERT_EQUAL_DOUBLE(5.0, p->val[EL_F_CONV_STATE]);
  TEST_ASSERT_EQUAL(EL_FN_CHARGER, p->fn);
}

static void test_unknown_operating_state_ignored(void) {
  delta("can0.36", "electrical.converter.36.0.operatingState", "\"warp\"");
  TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)el_endpoint_count(&el));
}

static void test_ac_current_delta(void) {
  delta("can0.41", "electrical.ac.41.0.phase.A.current", "1.4");
  TEST_ASSERT_EQUAL(EL_FN_AC_IN, ep(EL_DOM_AC, 0, 0)->fn);
}

static void test_non_numeric_instance_is_accepted(void) {
  delta("venus.0", "electrical.batteries.house.voltage", "12.9");
  TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)el_endpoint_count(&el));
}

static void test_values_published_and_feed_down_invalidates(void) {
  const char *src = "{\"can0\":{\"224\":{\"n2k\":{\"modelId\":\"Some Shunt 500A\"}}}}";   // the model names it a battery at once
  TEST_ASSERT_TRUE(sk_electrical_load_sources(src, strlen(src)));
  delta("can0.224", "electrical.batteries.0.voltage", "13.82");
  delta("can0.224", "electrical.batteries.0.current", "0");
  delta("can0.224", "electrical.batteries.0.capacity.stateOfCharge", "1");
  bd_id_t ids[4];
  TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)bd_match("electrical.resolved.batteries.*.voltage", ids, 4));
  bd_sample_t s;
  bd_get(ids[0], &s);
  TEST_ASSERT_TRUE(s.valid);
  sk_electrical_feed_down();
  bd_get(ids[0], &s);
  TEST_ASSERT_FALSE(s.valid);
}

static void test_subscription_adds_electrical_wildcard_only_when_attached(void) {
  char *with = sk_build_subscribe(NULL, 0);
  TEST_ASSERT_NOT_NULL(strstr(with, "electrical.*"));
  free(with);
  sk_electrical_attach(NULL);
  char *without = sk_build_subscribe(NULL, 0);
  TEST_ASSERT_NULL(strstr(without, "electrical.*"));
  free(without);
}

static void test_alternator_paths_are_adopted(void) {
  delta("can0.36", "electrical.alternators.0.voltage", "14.1");
  delta("can0.36", "electrical.alternators.0.current", "35");
  delta("can0.36", "electrical.alternators.0.chargingMode", "\"bulk\"");
  const el_endpoint_t *p = ep(EL_DOM_DC, 0, 0);
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQUAL(EL_FN_ALTERNATOR, p->fn);
  TEST_ASSERT_EQUAL(EL_EV_PATH, p->ev);
  bd_id_t ids[2];
  TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)bd_match("electrical.resolved.alternators.*.current", ids, 2));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_deltas_split_by_source_not_merged);
  RUN_TEST(test_alternator_paths_are_adopted);
  RUN_TEST(test_sources_tree_sets_identity_key);
  RUN_TEST(test_source_stamped_with_can_name_gets_identity);
  RUN_TEST(test_adopted_solar_path_is_pv);
  RUN_TEST(test_adopted_charger_path);
  RUN_TEST(test_adopted_venus_leaves_power_and_mode);
  RUN_TEST(test_out_of_range_values_rejected);
  RUN_TEST(test_rest_snapshot_does_not_feed_resolver);
  RUN_TEST(test_converter_state_string_mapped_and_charger_found);
  RUN_TEST(test_unknown_operating_state_ignored);
  RUN_TEST(test_ac_current_delta);
  RUN_TEST(test_non_numeric_instance_is_accepted);
  RUN_TEST(test_values_published_and_feed_down_invalidates);
  RUN_TEST(test_subscription_adds_electrical_wildcard_only_when_attached);
  return UNITY_END();
}
