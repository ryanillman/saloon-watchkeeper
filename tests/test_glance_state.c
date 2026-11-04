#include <math.h>
#include <stdio.h>
#include <string.h>
#include "alerts.h"
#include "boatdata.h"
#include "glance.h"
#include "unity.h"

static bd_id_t id;
#define NOW 100000

void setUp(void) {
  bd_init();
  alerts_init();
  id = bd_intern("propulsion.port.temperature");
}
void tearDown(void) {}

static void zones(const bd_zone_t *z, uint8_t n) {
  bd_meta_t m = {0};
  m.zone_count = n;
  for (uint8_t i = 0; i < n; i++) m.zones[i] = z[i];
  bd_set_meta(id, &m);
}

static void alert(const char *path, alert_state_t st) {
  alert_t a = {0};
  snprintf(a.id, sizeof a.id, "%s", path);
  a.state = st;
  alerts_upsert(&a, NOW);
}

static void test_worst_is_the_most_severe_and_alarm_beats_no_data(void) {
  gt_state_t a[] = {GT_OK, GT_ALARM, GT_NO_DATA};
  TEST_ASSERT_EQUAL(GT_ALARM, glance_worst(a, 3));
  gt_state_t b[] = {GT_OK, GT_NO_DATA, GT_WARN};
  TEST_ASSERT_EQUAL(GT_NO_DATA, glance_worst(b, 3));
  gt_state_t c[] = {GT_OFF, GT_OK};
  TEST_ASSERT_EQUAL(GT_OK, glance_worst(c, 2));
  TEST_ASSERT_EQUAL(GT_OFF, glance_worst(NULL, 0));
}

static void test_valid_value_with_no_zones_is_ok(void) {
  bd_set_number(id, 350, 1, NOW);
  TEST_ASSERT_EQUAL(GT_OK, glance_path_state(id, 5000, NOW));
}

static void test_value_inside_a_warn_zone_is_warn_with_lower_inclusive_upper_exclusive(void) {
  bd_zone_t z[] = {{.lower = 360, .upper = 380, .state = AL_WARN}};
  zones(z, 1);
  bd_set_number(id, 370, 1, NOW);
  TEST_ASSERT_EQUAL(GT_WARN, glance_path_state(id, 5000, NOW));
  bd_set_number(id, 360, 1, NOW);
  TEST_ASSERT_EQUAL(GT_WARN, glance_path_state(id, 5000, NOW));
  bd_set_number(id, 380, 1, NOW);
  TEST_ASSERT_EQUAL(GT_OK, glance_path_state(id, 5000, NOW));
}

static void test_open_ended_zone_and_emergency_zone(void) {
  bd_zone_t z[] = {{.lower = -INFINITY, .upper = 100, .state = AL_ALARM}, {.lower = 400, .upper = INFINITY, .state = AL_EMERGENCY}};
  zones(z, 2);
  bd_set_number(id, 50, 1, NOW);
  TEST_ASSERT_EQUAL(GT_ALARM, glance_path_state(id, 5000, NOW));
  bd_set_number(id, 450, 1, NOW);
  TEST_ASSERT_EQUAL(GT_ALARM, glance_path_state(id, 5000, NOW));
  bd_set_number(id, 200, 1, NOW);
  TEST_ASSERT_EQUAL(GT_OK, glance_path_state(id, 5000, NOW));
}

static void test_an_active_alert_wins_over_a_normal_zone(void) {
  bd_zone_t z[] = {{.lower = -INFINITY, .upper = INFINITY, .state = AL_NORMAL}};
  zones(z, 1);
  bd_set_number(id, 350, 1, NOW);
  alert("propulsion.port.temperature", AL_ALARM);
  TEST_ASSERT_EQUAL(GT_ALARM, glance_path_state(id, 5000, NOW));
}

static void test_alert_levels_map_to_tile_states(void) {
  bd_set_number(id, 350, 1, NOW);
  alert("propulsion.port.temperature", AL_ALERT);
  TEST_ASSERT_EQUAL(GT_ALERT, glance_path_state(id, 5000, NOW));
  alert("propulsion.port.temperature", AL_WARN);
  TEST_ASSERT_EQUAL(GT_WARN, glance_path_state(id, 5000, NOW));
  alert("propulsion.port.temperature", AL_EMERGENCY);
  TEST_ASSERT_EQUAL(GT_ALARM, glance_path_state(id, 5000, NOW));
}

static void test_an_alarm_still_shows_when_the_data_is_stale(void) {
  bd_set_number(id, 350, 1, NOW - 60000);
  alert("propulsion.port.temperature", AL_ALARM);
  TEST_ASSERT_EQUAL(GT_ALARM, glance_path_state(id, 5000, NOW));
}

static void test_stale_invalid_unknown_or_missing_data_is_no_data(void) {
  bd_set_number(id, 350, 1, NOW - 60000);
  TEST_ASSERT_EQUAL(GT_NO_DATA, glance_path_state(id, 5000, NOW));
  TEST_ASSERT_EQUAL(GT_OK, glance_path_state(id, 0, NOW));   // 0: quiet is fine
  bd_set_invalid(id);
  TEST_ASSERT_EQUAL(GT_NO_DATA, glance_path_state(id, 0, NOW));
  TEST_ASSERT_EQUAL(GT_NO_DATA, glance_path_state(BD_NONE, 0, NOW));
  TEST_ASSERT_EQUAL(GT_NO_DATA, glance_path_state(bd_intern("never.received"), 0, NOW));
}

static void test_state_words(void) {
  TEST_ASSERT_EQUAL_STRING("OFF", glance_state_word(GT_OFF));
  TEST_ASSERT_EQUAL_STRING("OK", glance_state_word(GT_OK));
  TEST_ASSERT_EQUAL_STRING("ALERT", glance_state_word(GT_ALERT));
  TEST_ASSERT_EQUAL_STRING("WARN", glance_state_word(GT_WARN));
  TEST_ASSERT_EQUAL_STRING("NO DATA", glance_state_word(GT_NO_DATA));
  TEST_ASSERT_EQUAL_STRING("ALARM", glance_state_word(GT_ALARM));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_worst_is_the_most_severe_and_alarm_beats_no_data);
  RUN_TEST(test_valid_value_with_no_zones_is_ok);
  RUN_TEST(test_value_inside_a_warn_zone_is_warn_with_lower_inclusive_upper_exclusive);
  RUN_TEST(test_open_ended_zone_and_emergency_zone);
  RUN_TEST(test_an_active_alert_wins_over_a_normal_zone);
  RUN_TEST(test_alert_levels_map_to_tile_states);
  RUN_TEST(test_an_alarm_still_shows_when_the_data_is_stale);
  RUN_TEST(test_stale_invalid_unknown_or_missing_data_is_no_data);
  RUN_TEST(test_state_words);
  return UNITY_END();
}
