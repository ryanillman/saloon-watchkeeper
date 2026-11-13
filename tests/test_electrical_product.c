#include "el_product.h"
#include "electrical.h"
#include "unity.h"

static el_t el;
void setUp(void) {
  el_init(&el);
  el_product_set_extra(NULL, 0);
}
void tearDown(void) {}

static const el_endpoint_t *ep(uint8_t domain, uint8_t instance) {
  for (size_t i = 0; i < el_endpoint_count(&el); i++) {
    const el_endpoint_t *p = el_endpoint(&el, i);
    if (p->domain == domain && p->instance == instance) return p;
  }
  return NULL;
}

static void test_lookup_prefix_case_insensitive(void) {
  static const el_product_row_t rows[] = {{"Acme*", EL_FN_ALTERNATOR, false}};
  el_product_set_extra(rows, 1);
  el_fn_t fn = EL_FN_UNKNOWN;
  bool mon = true;
  TEST_ASSERT_TRUE(el_product_lookup("acme X1", &fn, &mon));
  TEST_ASSERT_EQUAL(EL_FN_ALTERNATOR, fn);
  TEST_ASSERT_FALSE(mon);
  TEST_ASSERT_FALSE(el_product_lookup("BigAcme", &fn, &mon));
}

static void test_extra_rows_override_shipped(void) {
  el_fn_t fn;
  bool mon;
  TEST_ASSERT_TRUE(el_product_lookup("Some Shunt 500A", &fn, &mon));
  TEST_ASSERT_EQUAL(EL_FN_BATTERY, fn);
  TEST_ASSERT_TRUE(mon);
  static const el_product_row_t rows[] = {{"*Shunt*", EL_FN_CHARGER, false}};
  el_product_set_extra(rows, 1);
  TEST_ASSERT_TRUE(el_product_lookup("Some Shunt 500A", &fn, &mon));
  TEST_ASSERT_EQUAL(EL_FN_CHARGER, fn);
}

static void test_unknown_model_returns_false(void) {
  el_fn_t fn;
  bool mon;
  TEST_ASSERT_FALSE(el_product_lookup("Mystery Box 3000", &fn, &mon));
  TEST_ASSERT_FALSE(el_product_lookup("", &fn, &mon));
}

static void test_product_classifies_unproven_voltage_endpoint(void) {
  el_identity_t id = {.model_id = "Some Shunt 500A"};
  el_set_identity(&el, 1, 7, &id);
  el_observe(&el, 1, 7, 1, EL_F_VOLTAGE, 13.2, 1000);   // one sample: the voltage fallback would still say unknown
  TEST_ASSERT_EQUAL(EL_FN_BATTERY, ep(EL_DOM_DC, 1)->fn);
  TEST_ASSERT_EQUAL(EL_EV_PRODUCT, ep(EL_DOM_DC, 1)->ev);
}

static void test_converter_secondary_instance_with_solar_product_is_pv(void) {
  el_identity_t id = {.model_id = "Sun MPPT 100/15"};
  el_set_identity(&el, 1, 7, &id);
  el_observe(&el, 1, 7, 0, EL_F_CONV_STATE, 4, 1000);
  el_observe(&el, 1, 7, 0, EL_F_VOLTAGE, 13.8, 1000);
  el_observe(&el, 1, 7, 0, EL_F_CURRENT, 5.0, 1000);
  el_observe(&el, 1, 7, 1, EL_F_VOLTAGE, 20.0, 1000);
  TEST_ASSERT_EQUAL(EL_FN_CHARGER, ep(EL_DOM_DC, 0)->fn);   // the output keeps the stronger PGN evidence
  TEST_ASSERT_EQUAL(EL_FN_PV, ep(EL_DOM_DC, 1)->fn);
  TEST_ASSERT_EQUAL(EL_EV_PRODUCT, ep(EL_DOM_DC, 1)->ev);
}

static void test_unknown_model_does_not_change_pgn_classification(void) {
  el_identity_t id = {.model_id = "Mystery Box 3000"};
  el_set_identity(&el, 1, 7, &id);
  el_observe(&el, 1, 7, 0, EL_F_CONV_STATE, 4, 1000);
  el_observe(&el, 1, 7, 0, EL_F_VOLTAGE, 13.8, 1000);
  el_observe(&el, 1, 7, 0, EL_F_CURRENT, 5.0, 1000);
  el_observe(&el, 1, 7, 1, EL_F_VOLTAGE, 20.0, 1000);
  TEST_ASSERT_EQUAL(EL_FN_CHARGER, ep(EL_DOM_DC, 0)->fn);
  TEST_ASSERT_EQUAL(EL_FN_UNKNOWN, ep(EL_DOM_DC, 1)->fn);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_lookup_prefix_case_insensitive);
  RUN_TEST(test_extra_rows_override_shipped);
  RUN_TEST(test_unknown_model_returns_false);
  RUN_TEST(test_product_classifies_unproven_voltage_endpoint);
  RUN_TEST(test_converter_secondary_instance_with_solar_product_is_pv);
  RUN_TEST(test_unknown_model_does_not_change_pgn_classification);
  return UNITY_END();
}
