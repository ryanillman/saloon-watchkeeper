#include <math.h>
#include <string.h>
#include "electrical.h"
#include "unity.h"

static el_t el;
void setUp(void) { el_init(&el); }
void tearDown(void) {}

static const el_endpoint_t *find_key(uint64_t key) {
  for (size_t i = 0; i < el_endpoint_count(&el); i++)
    if (el_endpoint(&el, i)->key == key) return el_endpoint(&el, i);
  return NULL;
}

static void test_two_devices_same_instance_stay_separate(void) {
  el_observe(&el, 0, 10, 0, EL_F_VOLTAGE, 13.1, 1000);
  el_observe(&el, 0, 20, 0, EL_F_VOLTAGE, 13.9, 1000);
  TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)el_endpoint_count(&el));
  TEST_ASSERT_NOT_EQUAL(el_endpoint(&el, 0)->key, el_endpoint(&el, 1)->key);
  TEST_ASSERT_EQUAL_DOUBLE(13.1, el_endpoint(&el, 0)->val[EL_F_VOLTAGE]);
  TEST_ASSERT_EQUAL_DOUBLE(13.9, el_endpoint(&el, 1)->val[EL_F_VOLTAGE]);
}

static void test_same_device_two_instances_are_two_endpoints(void) {
  el_observe(&el, 0, 10, 0, EL_F_VOLTAGE, 13.1, 1000);
  el_observe(&el, 0, 10, 1, EL_F_VOLTAGE, 2.1, 1000);
  TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)el_endpoint_count(&el));
}

static void test_address_change_keeps_key(void) {
  el_identity_t id = {.can_name = 0xC046A0002CC001F8ull};
  el_set_identity(&el, 0, 36, &id);
  el_observe(&el, 0, 36, 0, EL_F_VOLTAGE, 13.1, 1000);
  uint64_t key = el_endpoint(&el, 0)->key;
  el_set_identity(&el, 0, 52, &id);                       // re-claimed at another address
  el_observe(&el, 0, 52, 0, EL_F_VOLTAGE, 13.2, 2000);
  TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)el_endpoint_count(&el));
  TEST_ASSERT_TRUE(find_key(key) != NULL);
  TEST_ASSERT_EQUAL_DOUBLE(13.2, find_key(key)->val[EL_F_VOLTAGE]);
}

static void test_identity_after_data_migrates_endpoint(void) {
  el_observe(&el, 0, 36, 0, EL_F_VOLTAGE, 13.1, 1000);   // provisional key
  uint64_t before = el_endpoint(&el, 0)->key;
  el_identity_t id = {.can_name = 0x1234567890ABCDEFull};
  el_set_identity(&el, 0, 36, &id);
  TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)el_endpoint_count(&el));
  TEST_ASSERT_NOT_EQUAL(before, el_endpoint(&el, 0)->key);
  TEST_ASSERT_EQUAL_DOUBLE(13.1, el_endpoint(&el, 0)->val[EL_F_VOLTAGE]);
  el_observe(&el, 0, 36, 0, EL_F_CURRENT, 2.0, 1100);    // still one endpoint after the migration
  TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)el_endpoint_count(&el));
}

static void test_identity_merges_into_existing_stable_endpoint(void) {
  el_identity_t id = {.can_name = 0x1234567890ABCDEFull};
  el_set_identity(&el, 0, 36, &id);
  el_observe(&el, 0, 36, 0, EL_F_VOLTAGE, 13.1, 1000);
  el_observe(&el, 0, 52, 0, EL_F_CURRENT, 2.0, 1000);    // same device seen at a new address before its claim
  el_set_identity(&el, 0, 52, &id);
  TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)el_endpoint_count(&el));
  TEST_ASSERT_TRUE(el_endpoint(&el, 0)->has[EL_F_VOLTAGE] && el_endpoint(&el, 0)->has[EL_F_CURRENT]);
}

static void test_serial_gives_stable_key_without_name(void) {
  el_identity_t a = {.model_id = "M1", .serial = "S1"}, b = {.model_id = "M1", .serial = "S2"};
  el_set_identity(&el, 1, 7, &a);
  el_set_identity(&el, 1, 8, &b);
  el_observe(&el, 1, 7, 0, EL_F_VOLTAGE, 13.0, 1000);
  el_observe(&el, 1, 8, 0, EL_F_VOLTAGE, 13.0, 1000);
  TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)el_endpoint_count(&el));
  el_observe(&el, 1, 9, 0, EL_F_VOLTAGE, 13.0, 1000);    // unidentified: provisional, stays its own
  TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)el_endpoint_count(&el));
}

static void test_identical_devices_without_serial_not_merged(void) {
  el_identity_t a = {.model_id = "M1"};
  el_set_identity(&el, 1, 7, &a);
  el_set_identity(&el, 1, 8, &a);
  el_observe(&el, 1, 7, 0, EL_F_VOLTAGE, 13.0, 1000);
  el_observe(&el, 1, 8, 0, EL_F_VOLTAGE, 13.5, 1000);
  TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)el_endpoint_count(&el));
}

static void test_nan_and_inf_rejected(void) {
  el_observe(&el, 0, 10, 0, EL_F_VOLTAGE, 13.1, 1000);
  el_observe(&el, 0, 10, 0, EL_F_VOLTAGE, NAN, 1100);
  el_observe(&el, 0, 10, 0, EL_F_CURRENT, INFINITY, 1100);
  TEST_ASSERT_EQUAL_DOUBLE(13.1, el_endpoint(&el, 0)->val[EL_F_VOLTAGE]);
  TEST_ASSERT_FALSE(el_endpoint(&el, 0)->has[EL_F_CURRENT]);
}

static void test_backwards_timestamp_ignored(void) {
  el_observe(&el, 0, 10, 0, EL_F_VOLTAGE, 13.1, 2000);
  el_observe(&el, 0, 10, 0, EL_F_VOLTAGE, 9.9, 1000);
  TEST_ASSERT_EQUAL_DOUBLE(13.1, el_endpoint(&el, 0)->val[EL_F_VOLTAGE]);
}

static void test_table_full_counts_dropped(void) {
  for (uint32_t r = 1; r <= EL_MAX_DEVICES + 1; r++) el_observe(&el, 0, r, 0, EL_F_VOLTAGE, 12.0 + r * 0.01, 1000);
  TEST_ASSERT_EQUAL_UINT32(EL_MAX_DEVICES, (uint32_t)el_endpoint_count(&el));
  TEST_ASSERT_EQUAL_UINT32(1, el_dropped(&el));
  TEST_ASSERT_EQUAL_DOUBLE(12.01, el_endpoint(&el, 0)->val[EL_F_VOLTAGE]);   // earlier data intact
}

static void test_ac_and_dc_with_same_instance_are_separate_endpoints(void) {
  el_observe(&el, 0, 10, 0, EL_F_VOLTAGE, 13.1, 1000);
  el_observe(&el, 0, 10, 0, EL_F_AC_CURRENT, 1.0, 1000);
  el_observe(&el, 0, 10, 0, EL_F_SWITCH, 1.0, 1000);
  TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)el_endpoint_count(&el));
}

static void test_identity_fields_merge_instead_of_overwrite(void) {
  el_identity_t prod = {.model_id = "Some Shunt 500A", .serial = "S9"};
  el_identity_t claim = {.can_name = 0x1234567890ABCDEFull};
  el_set_identity(&el, 0, 36, &prod);
  el_set_identity(&el, 0, 36, &claim);   // a claim arriving after the product information must not erase it
  el_observe(&el, 0, 36, 0, EL_F_VOLTAGE, 13.1, 1000);
  TEST_ASSERT_EQUAL(EL_FN_BATTERY, el_endpoint(&el, 0)->fn);   // the model is still known: product rank decides
  TEST_ASSERT_EQUAL(EL_EV_PRODUCT, el_endpoint(&el, 0)->ev);
}

static void test_identity_only_devices_do_not_starve_data_devices(void) {
  // A server's sources tree and a bus's address claims name every node, electrical or not.
  for (uint32_t r = 1; r <= EL_MAX_DEVICES + 8; r++) {
    el_identity_t id = {.can_name = 0x1000 + r};
    el_set_identity(&el, 0, r, &id);
  }
  el_observe(&el, 0, 999, 0, EL_F_VOLTAGE, 13.1, 1000);   // a device that actually reports electrical data
  TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)el_endpoint_count(&el));
  TEST_ASSERT_EQUAL_UINT32(0, el_dropped(&el));
}

static void test_full_table_never_leaves_the_lock_held(void) {
  for (uint32_t r = 1; r <= EL_MAX_DEVICES; r++) el_observe(&el, 0, r, 0, EL_F_VOLTAGE, 12.0 + r * 0.01, 1000);   // every slot has an endpoint
  el_observe_path(&el, 0, 900, 0, EL_FN_PV, EL_F_VOLTAGE, 20.0, 1000);   // dropped
  el_identity_t id = {.can_name = 0x77};
  el_set_identity(&el, 0, 901, &id);                                     // dropped
  el_observe(&el, 0, 1, 0, EL_F_CURRENT, 2.0, 1100);                     // must still work, not wait forever
  TEST_ASSERT_TRUE(el_endpoint(&el, 0)->has[EL_F_CURRENT]);
  TEST_ASSERT_EQUAL_UINT32(2, el_dropped(&el));
}

static void test_endpoint_id_is_eight_hex_and_stable(void) {
  el_observe(&el, 0, 10, 0, EL_F_VOLTAGE, 13.1, 1000);
  char a[9], b[9];
  el_endpoint_id(el_endpoint(&el, 0), a);
  el_endpoint_id(el_endpoint(&el, 0), b);
  TEST_ASSERT_EQUAL_STRING(a, b);
  TEST_ASSERT_EQUAL_UINT32(8, (uint32_t)strlen(a));
  for (int i = 0; i < 8; i++) TEST_ASSERT_TRUE((a[i] >= '0' && a[i] <= '9') || (a[i] >= 'a' && a[i] <= 'f'));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_two_devices_same_instance_stay_separate);
  RUN_TEST(test_same_device_two_instances_are_two_endpoints);
  RUN_TEST(test_address_change_keeps_key);
  RUN_TEST(test_identity_after_data_migrates_endpoint);
  RUN_TEST(test_identity_merges_into_existing_stable_endpoint);
  RUN_TEST(test_serial_gives_stable_key_without_name);
  RUN_TEST(test_identical_devices_without_serial_not_merged);
  RUN_TEST(test_nan_and_inf_rejected);
  RUN_TEST(test_backwards_timestamp_ignored);
  RUN_TEST(test_table_full_counts_dropped);
  RUN_TEST(test_ac_and_dc_with_same_instance_are_separate_endpoints);
  RUN_TEST(test_identity_fields_merge_instead_of_overwrite);
  RUN_TEST(test_full_table_never_leaves_the_lock_held);
  RUN_TEST(test_identity_only_devices_do_not_starve_data_devices);
  RUN_TEST(test_endpoint_id_is_eight_hex_and_stable);
  return UNITY_END();
}
