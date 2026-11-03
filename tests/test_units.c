#include <string.h>
#include "unity.h"
#include "units.h"

static cfg_units_t metric, imperial;

void setUp(void) {
  cfg_t c;
  cfg_defaults(&c);
  metric = c.units;   // C, bar, L, kn, nm
  imperial = metric;
  strcpy(imperial.temperature, "F");
  strcpy(imperial.pressure, "psi");
  strcpy(imperial.volume, "gal");
  strcpy(imperial.distance, "ft");
}
void tearDown(void) {}

static void test_kelvin_to_c(void) { TEST_ASSERT_DOUBLE_WITHIN(1e-9, 100.0, units_from_si(Q_TEMPERATURE, 373.15, &metric)); }
static void test_kelvin_to_f(void) { TEST_ASSERT_DOUBLE_WITHIN(1e-9, 212.0, units_from_si(Q_TEMPERATURE, 373.15, &imperial)); }
static void test_pa_to_bar(void) { TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, units_from_si(Q_PRESSURE, 100000, &metric)); }
static void test_pa_to_psi(void) { TEST_ASSERT_DOUBLE_WITHIN(0.001, 14.5038, units_from_si(Q_PRESSURE, 100000, &imperial)); }
static void test_hz_to_rpm(void) { TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1800.0, units_from_si(Q_ROTATION, 30, &metric)); }
static void test_ratio_to_percent(void) { TEST_ASSERT_DOUBLE_WITHIN(1e-9, 75.0, units_from_si(Q_RATIO, 0.75, &metric)); }
static void test_seconds_to_hours(void) { TEST_ASSERT_DOUBLE_WITHIN(1e-9, 2.5, units_from_si(Q_DURATION, 9000, &metric)); }
static void test_m3_to_litres(void) { TEST_ASSERT_DOUBLE_WITHIN(1e-9, 200.0, units_from_si(Q_VOLUME, 0.2, &metric)); }
static void test_flow_to_l_per_h(void) {
  // 10 L/h = 0.01 m3 / 3600 s
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 10.0, units_from_si(Q_FLOW, 0.01 / 3600.0, &metric));
}
static void test_ms_to_knots(void) { TEST_ASSERT_DOUBLE_WITHIN(1e-6, 1.943844, units_from_si(Q_SPEED, 1.0, &metric)); }
static void test_m_to_nm(void) { TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, units_from_si(Q_DISTANCE, 1852, &metric)); }
static void test_volts_unchanged(void) { TEST_ASSERT_DOUBLE_WITHIN(1e-9, 13.8, units_from_si(Q_VOLTAGE, 13.8, &metric)); }

static void test_roundtrip_all(void) {
  const cfg_units_t *sets[] = {&metric, &imperial};
  for (int s = 0; s < 2; s++)
    for (int q = 0; q < Q_NONE; q++) {
      double x = 123.456;
      TEST_ASSERT_DOUBLE_WITHIN(1e-9, x, units_to_si((quantity_t)q, units_from_si((quantity_t)q, x, sets[s]), sets[s]));
    }
}

static void test_power_identity_and_label(void) {
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 450.0, units_from_si(Q_POWER, 450.0, &metric));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, -20.0, units_to_si(Q_POWER, -20.0, &imperial));
  TEST_ASSERT_EQUAL_STRING("W", units_label(Q_POWER, &metric));
}

static void test_labels(void) {
  TEST_ASSERT_EQUAL_STRING("°C", units_label(Q_TEMPERATURE, &metric));
  TEST_ASSERT_EQUAL_STRING("°F", units_label(Q_TEMPERATURE, &imperial));
  TEST_ASSERT_EQUAL_STRING("bar", units_label(Q_PRESSURE, &metric));
  TEST_ASSERT_EQUAL_STRING("psi", units_label(Q_PRESSURE, &imperial));
  TEST_ASSERT_EQUAL_STRING("RPM", units_label(Q_ROTATION, &metric));
  TEST_ASSERT_EQUAL_STRING("%", units_label(Q_RATIO, &metric));
  TEST_ASSERT_EQUAL_STRING("h", units_label(Q_DURATION, &metric));
  TEST_ASSERT_EQUAL_STRING("L/h", units_label(Q_FLOW, &metric));
  TEST_ASSERT_EQUAL_STRING("gal/h", units_label(Q_FLOW, &imperial));
  TEST_ASSERT_EQUAL_STRING("V", units_label(Q_VOLTAGE, &metric));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_kelvin_to_c);
  RUN_TEST(test_kelvin_to_f);
  RUN_TEST(test_pa_to_bar);
  RUN_TEST(test_pa_to_psi);
  RUN_TEST(test_hz_to_rpm);
  RUN_TEST(test_ratio_to_percent);
  RUN_TEST(test_seconds_to_hours);
  RUN_TEST(test_m3_to_litres);
  RUN_TEST(test_flow_to_l_per_h);
  RUN_TEST(test_ms_to_knots);
  RUN_TEST(test_m_to_nm);
  RUN_TEST(test_volts_unchanged);
  RUN_TEST(test_roundtrip_all);
  RUN_TEST(test_labels);
  RUN_TEST(test_power_identity_and_label);
  return UNITY_END();
}
