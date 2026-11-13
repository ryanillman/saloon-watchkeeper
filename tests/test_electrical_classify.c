#include "electrical.h"
#include "unity.h"

static el_t el;
void setUp(void) { el_init(&el); }
void tearDown(void) {}

static const el_endpoint_t *ep(uint8_t domain, uint8_t instance) {
  for (size_t i = 0; i < el_endpoint_count(&el); i++) {
    const el_endpoint_t *p = el_endpoint(&el, i);
    if (p->domain == domain && p->instance == instance) return p;
  }
  return NULL;
}
static void obs(el_ref_t ref, uint8_t inst, el_field_t f, double v, int64_t t) { el_observe(&el, 0, ref, inst, f, v, t); }

static void test_dc_type_solar_is_pv(void) {
  obs(10, 1, EL_F_DC_TYPE, 3, 1000);
  TEST_ASSERT_EQUAL(EL_FN_PV, ep(EL_DOM_DC, 1)->fn);
  TEST_ASSERT_EQUAL(EL_EV_DC_TYPE, ep(EL_DOM_DC, 1)->ev);
}

static void test_dc_type_battery_is_battery(void) {
  obs(10, 0, EL_F_DC_TYPE, 0, 1000);
  TEST_ASSERT_EQUAL(EL_FN_BATTERY, ep(EL_DOM_DC, 0)->fn);
}

static void test_dc_type_other_values(void) {
  obs(10, 0, EL_F_DC_TYPE, 1, 1000);
  obs(10, 1, EL_F_DC_TYPE, 2, 1000);
  obs(10, 2, EL_F_DC_TYPE, 4, 1000);   // wind: not something the Power role shows
  TEST_ASSERT_EQUAL(EL_FN_ALTERNATOR, ep(EL_DOM_DC, 0)->fn);
  TEST_ASSERT_EQUAL(EL_FN_CONVERTER, ep(EL_DOM_DC, 1)->fn);
  TEST_ASSERT_EQUAL(EL_FN_UNKNOWN, ep(EL_DOM_DC, 2)->fn);
}

static void test_dc_type_beats_voltage_fallback(void) {
  obs(10, 1, EL_F_VOLTAGE, 24.0, 0);
  obs(10, 1, EL_F_VOLTAGE, 24.0, 10000);
  obs(10, 1, EL_F_DC_TYPE, 3, 10000);
  TEST_ASSERT_EQUAL(EL_FN_PV, ep(EL_DOM_DC, 1)->fn);
}

static void test_dc_type_conflict_is_unknown(void) {
  el_identity_t id = {.can_name = 0xABCDEFull};
  el_set_identity(&el, 0, 10, &id);
  el_set_identity(&el, 0, 11, &id);   // the same device through two suppliers
  obs(10, 0, EL_F_DC_TYPE, 3, 1000);
  obs(11, 0, EL_F_DC_TYPE, 0, 1100);
  TEST_ASSERT_EQUAL(EL_FN_UNKNOWN, ep(EL_DOM_DC, 0)->fn);
}

static void test_converter_class_battery_type_is_charger_output(void) {
  obs(10, 0, EL_F_CONV_STATE, 4, 1000);          // device reports converter status
  obs(10, 0, EL_F_DC_TYPE, 0, 1000);
  obs(10, 1, EL_F_DC_TYPE, 3, 1000);
  TEST_ASSERT_EQUAL(EL_FN_CHARGER, ep(EL_DOM_DC, 0)->fn);
  TEST_ASSERT_EQUAL(EL_FN_PV, ep(EL_DOM_DC, 1)->fn);
}

static void test_ac_current_marks_ac_in(void) {
  obs(10, 0, EL_F_AC_CURRENT, 1.0, 1000);
  TEST_ASSERT_EQUAL(EL_FN_AC_IN, ep(EL_DOM_AC, 0)->fn);
  TEST_ASSERT_EQUAL(EL_EV_PGN, ep(EL_DOM_AC, 0)->ev);
}

static void test_switch_marks_bank(void) {
  obs(10, 3, EL_F_SWITCH, 1.0, 1000);
  TEST_ASSERT_EQUAL(EL_FN_SWITCH_BANK, ep(EL_DOM_SWITCH, 3)->fn);
}

static void test_converter_class_without_dc_type_lowest_instance_is_charger(void) {
  obs(10, 0, EL_F_CONV_STATE, 4, 1000);
  obs(10, 0, EL_F_VOLTAGE, 13.8, 1000);
  obs(10, 0, EL_F_CURRENT, 5.0, 1000);
  obs(10, 1, EL_F_VOLTAGE, 20.0, 1000);
  obs(10, 1, EL_F_CURRENT, 0.0, 1000);
  TEST_ASSERT_EQUAL(EL_FN_CHARGER, ep(EL_DOM_DC, 0)->fn);
  TEST_ASSERT_EQUAL(EL_FN_UNKNOWN, ep(EL_DOM_DC, 1)->fn);   // a PV input must not become a battery or a second charger
}

static void test_voltage_only_stable_is_battery_low_confidence(void) {
  obs(10, 1, EL_F_VOLTAGE, 13.4, 0);
  TEST_ASSERT_EQUAL(EL_FN_UNKNOWN, ep(EL_DOM_DC, 1)->fn);   // not yet seen to be stable
  obs(10, 1, EL_F_VOLTAGE, 13.5, 10000);
  TEST_ASSERT_EQUAL(EL_FN_BATTERY, ep(EL_DOM_DC, 1)->fn);
  TEST_ASSERT_EQUAL(EL_EV_VOLTAGE, ep(EL_DOM_DC, 1)->ev);
}

static void test_plain_battery_with_current_and_no_dc_type_is_battery(void) {
  obs(10, 0, EL_F_VOLTAGE, 12.7, 0);
  obs(10, 0, EL_F_CURRENT, -3.0, 0);
  obs(10, 0, EL_F_VOLTAGE, 12.7, 10000);
  TEST_ASSERT_EQUAL(EL_FN_BATTERY, ep(EL_DOM_DC, 0)->fn);
}

static void test_voltage_only_unstable_or_out_of_range_is_unknown(void) {
  obs(10, 0, EL_F_VOLTAGE, 13.0, 0);
  obs(10, 0, EL_F_VOLTAGE, 15.0, 5000);    // swings by more than the stability band
  obs(10, 0, EL_F_VOLTAGE, 15.0, 12000);   // only 7 s since the swing
  TEST_ASSERT_EQUAL(EL_FN_UNKNOWN, ep(EL_DOM_DC, 0)->fn);
  obs(10, 1, EL_F_VOLTAGE, 2.1, 0);
  obs(10, 1, EL_F_VOLTAGE, 2.1, 20000);   // steady but far below a battery
  TEST_ASSERT_EQUAL(EL_FN_UNKNOWN, ep(EL_DOM_DC, 1)->fn);
  obs(10, 2, EL_F_VOLTAGE, 70.0, 0);
  obs(10, 2, EL_F_VOLTAGE, 70.0, 20000);
  TEST_ASSERT_EQUAL(EL_FN_UNKNOWN, ep(EL_DOM_DC, 2)->fn);
}

static void test_lower_rank_disagreement_does_not_change_result(void) {
  obs(10, 0, EL_F_CONV_STATE, 4, 1000);
  obs(10, 0, EL_F_VOLTAGE, 13.8, 1000);
  obs(10, 0, EL_F_CURRENT, 5.0, 1000);    // rank 2 alone would say charger
  obs(10, 0, EL_F_DC_TYPE, 3, 1000);      // rank 1 says PV
  TEST_ASSERT_EQUAL(EL_FN_PV, ep(EL_DOM_DC, 0)->fn);
  TEST_ASSERT_TRUE(ep(EL_DOM_DC, 0)->disagree);
}

static void test_adopted_path_keeps_function(void) {
  el_observe_path(&el, 1, 5, 0, EL_FN_PV, EL_F_VOLTAGE, 20.5, 1000);
  TEST_ASSERT_EQUAL(EL_FN_PV, ep(EL_DOM_DC, 0)->fn);
  TEST_ASSERT_EQUAL(EL_EV_PATH, ep(EL_DOM_DC, 0)->ev);
  el_observe(&el, 1, 5, 0, EL_F_DC_TYPE, 0, 2000);   // weaker evidence cannot override a named path
  TEST_ASSERT_EQUAL(EL_FN_PV, ep(EL_DOM_DC, 0)->fn);
  TEST_ASSERT_EQUAL_DOUBLE(20.5, ep(EL_DOM_DC, 0)->val[EL_F_VOLTAGE]);
}

static void test_voltage_battery_survives_a_load_transient(void) {
  obs(10, 0, EL_F_VOLTAGE, 13.4, 0);
  obs(10, 0, EL_F_VOLTAGE, 13.4, 10000);
  TEST_ASSERT_EQUAL(EL_FN_BATTERY, ep(EL_DOM_DC, 0)->fn);
  obs(10, 0, EL_F_VOLTAGE, 11.2, 12000);   // a windlass starts: the voltage swings well outside the stability band
  TEST_ASSERT_EQUAL(EL_FN_BATTERY, ep(EL_DOM_DC, 0)->fn);   // it does not stop being a battery
  obs(10, 0, EL_F_VOLTAGE, 5.0, 14000);    // but a reading no battery can give withdraws the claim
  TEST_ASSERT_EQUAL(EL_FN_UNKNOWN, ep(EL_DOM_DC, 0)->fn);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_dc_type_solar_is_pv);
  RUN_TEST(test_dc_type_battery_is_battery);
  RUN_TEST(test_dc_type_other_values);
  RUN_TEST(test_dc_type_beats_voltage_fallback);
  RUN_TEST(test_dc_type_conflict_is_unknown);
  RUN_TEST(test_converter_class_battery_type_is_charger_output);
  RUN_TEST(test_ac_current_marks_ac_in);
  RUN_TEST(test_switch_marks_bank);
  RUN_TEST(test_converter_class_without_dc_type_lowest_instance_is_charger);
  RUN_TEST(test_voltage_only_stable_is_battery_low_confidence);
  RUN_TEST(test_plain_battery_with_current_and_no_dc_type_is_battery);
  RUN_TEST(test_voltage_only_unstable_or_out_of_range_is_unknown);
  RUN_TEST(test_lower_rank_disagreement_does_not_change_result);
  RUN_TEST(test_adopted_path_keeps_function);
  RUN_TEST(test_voltage_battery_survives_a_load_transient);
  return UNITY_END();
}
