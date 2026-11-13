// A person-readable name for each endpoint: the device's short model name, told apart from its neighbours only when
// it has to be.
#include <stdio.h>
#include <string.h>
#include "electrical.h"
#include "unity.h"

static el_t el;
void setUp(void) { el_init(&el); }
void tearDown(void) {}

static void device(uint32_t ref, const char *model, uint64_t name) {
  el_identity_t id = {.can_name = name};
  snprintf(id.model_id, sizeof id.model_id, "%s", model);
  el_set_identity(&el, 0, ref, &id);
}
// A battery-type endpoint (DC type 0 on a non-converter device) or a panel input (DC type 3).
static void endpoint(uint32_t ref, uint8_t inst, int dc_type) {
  el_observe(&el, 0, ref, inst, EL_F_DC_TYPE, dc_type, 1000);
  el_observe(&el, 0, ref, inst, EL_F_VOLTAGE, dc_type == 3 ? 20.0 : 13.2, 1000);
}
static const el_endpoint_t *find(uint32_t ref, uint8_t inst) {
  for (size_t i = 0; i < el_endpoint_count(&el); i++)
    if (el_endpoint(&el, i)->src[EL_F_VOLTAGE] == ref && el_endpoint(&el, i)->instance == inst) return el_endpoint(&el, i);
  return NULL;
}
static const char *label(uint32_t ref, uint8_t inst) {
  static char out[16];
  el_endpoint_label(&el, find(ref, inst), out, sizeof out);
  return out;
}

static void test_label_is_the_models_first_word_without_digits(void) {
  device(10, "SmartShunt IP65 300A", 0xAA01);
  endpoint(10, 0, 0);
  TEST_ASSERT_EQUAL_STRING("SmartShunt", label(10, 0));
  device(11, "Phoenix Smart IP43 Charger 12|30", 0xAA02);
  endpoint(11, 0, 0);
  TEST_ASSERT_EQUAL_STRING("Phoenix", label(11, 0));
}

static void test_a_model_with_no_usable_word_falls_back_to_the_function(void) {
  device(10, "E70327", 0xAA01);   // all digits
  endpoint(10, 0, 0);
  TEST_ASSERT_EQUAL_STRING("Battery", label(10, 0));
  endpoint(20, 0, 3);             // no identity at all
  TEST_ASSERT_EQUAL_STRING("Solar", label(20, 0));
}

static void test_a_second_instance_of_one_device_gets_its_instance(void) {
  device(10, "SmartShunt IP65 300A", 0xAA01);
  endpoint(10, 0, 0);
  endpoint(10, 1, 0);
  TEST_ASSERT_EQUAL_STRING("SmartShunt 0", label(10, 0));
  TEST_ASSERT_EQUAL_STRING("SmartShunt 1", label(10, 1));
}

static void test_devices_of_one_model_get_the_tail_of_their_name(void) {
  device(36, "BlueSolar Charger MPPT 100/15", 0xC046A0002CC001F5ull);
  device(37, "BlueSolar Charger MPPT 100/15", 0xC046A0002CC001F6ull);
  endpoint(36, 1, 3);
  endpoint(37, 1, 3);
  TEST_ASSERT_EQUAL_STRING("BlueSolar f5", label(36, 1));   // the end of can0.c046a0002cc001f5 in Signal K
  TEST_ASSERT_EQUAL_STRING("BlueSolar f6", label(37, 1));
}

static void test_different_functions_do_not_clash(void) {
  device(36, "BlueSolar Charger MPPT 100/15", 0xC046A0002CC001F5ull);
  el_observe(&el, 0, 36, 0, EL_F_CONV_STATE, 4, 1000);   // converter-class: instance 0 is the charger output
  endpoint(36, 0, 0);
  endpoint(36, 1, 3);
  TEST_ASSERT_EQUAL_STRING("BlueSolar", label(36, 0));
  TEST_ASSERT_EQUAL_STRING("BlueSolar", label(36, 1));
}

static void test_both_clashes_use_both_qualifiers_and_still_fit(void) {
  device(36, "BlueSolar Charger MPPT 100/15", 0xC046A0002CC001F5ull);
  device(37, "BlueSolar Charger MPPT 100/15", 0xC046A0002CC001F6ull);
  endpoint(36, 1, 3);
  endpoint(36, 2, 3);
  endpoint(37, 1, 3);
  const char *l = label(36, 1);
  TEST_ASSERT_EQUAL_STRING("BlueSolar f5.1", l);
  TEST_ASSERT_TRUE(strlen(l) <= 15);
}

static void test_a_long_word_is_cut_to_make_room(void) {
  device(10, "Extraordinarily Long 12", 0xAA01);
  endpoint(10, 0, 0);
  endpoint(10, 1, 0);
  const char *l = label(10, 1);
  TEST_ASSERT_TRUE(strlen(l) <= 15);
  TEST_ASSERT_EQUAL_CHAR('1', l[strlen(l) - 1]);
  TEST_ASSERT_EQUAL_INT(0, strncmp(l, "Extraordinarily", strlen(l) - 2));   // the front of the word, then " 1"
}

static void test_endpoints_that_are_not_shown_do_not_force_a_qualifier(void) {
  device(10, "SmartShunt IP65 300A", 0xAA01);
  endpoint(10, 0, 0);
  el_observe(&el, 0, 10, 5, EL_F_DC_TYPE, 4, 1000);   // wind: unknown, never published
  el_observe(&el, 0, 10, 5, EL_F_VOLTAGE, 14.0, 1000);
  TEST_ASSERT_EQUAL_STRING("SmartShunt", label(10, 0));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_label_is_the_models_first_word_without_digits);
  RUN_TEST(test_a_model_with_no_usable_word_falls_back_to_the_function);
  RUN_TEST(test_a_second_instance_of_one_device_gets_its_instance);
  RUN_TEST(test_devices_of_one_model_get_the_tail_of_their_name);
  RUN_TEST(test_different_functions_do_not_clash);
  RUN_TEST(test_both_clashes_use_both_qualifiers_and_still_fit);
  RUN_TEST(test_a_long_word_is_cut_to_make_room);
  RUN_TEST(test_endpoints_that_are_not_shown_do_not_force_a_qualifier);
  return UNITY_END();
}
