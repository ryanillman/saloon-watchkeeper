#include <math.h>
#include <string.h>
#include "unity.h"
#include "n2k_decode.h"

static n2k_value_t got[16];
static int ngot;
static void on_value(const n2k_value_t *v, void *ctx) {
  (void)ctx;
  if (ngot < 16) got[ngot++] = *v;
}
void setUp(void) { ngot = 0; }
void tearDown(void) {}

static bool decode(uint32_t pgn, const uint8_t *d, uint16_t len) {
  n2k_msg_t m = {pgn, 0x20, 255, 3, len, d};
  return n2k_decode(&m, on_value, NULL);
}
static const n2k_value_t *find(n2k_field_t f) {
  for (int i = 0; i < ngot; i++)
    if (got[i].field == f) return &got[i];
  return NULL;
}

static void test_depth(void) {
  const uint8_t d[8] = {1, 0xD2, 0x04, 0, 0, 0xF4, 0x01, 10};  // 12.34 m, offset +0.5 m
  TEST_ASSERT_TRUE(decode(128267, d, 8));
  TEST_ASSERT_EQUAL_INT(2, ngot);
  TEST_ASSERT_EQUAL_DOUBLE(12.34, find(N2K_F_DEPTH_BELOW_TRANSDUCER)->value);
  TEST_ASSERT_EQUAL_DOUBLE(0.5, find(N2K_F_DEPTH_SURFACE_TO_TRANSDUCER)->value);
  const uint8_t k[8] = {1, 0xD2, 0x04, 0, 0, 0xE0, 0xFC, 10};  // offset -0.8 m
  ngot = 0;
  decode(128267, k, 8);
  TEST_ASSERT_EQUAL_DOUBLE(0.8, find(N2K_F_DEPTH_TRANSDUCER_TO_KEEL)->value);
  TEST_ASSERT_NULL(find(N2K_F_DEPTH_SURFACE_TO_TRANSDUCER));
  const uint8_t z[8] = {1, 0xD2, 0x04, 0, 0, 0, 0, 10};
  ngot = 0;
  decode(128267, z, 8);
  TEST_ASSERT_EQUAL_INT(1, ngot);
}

static void test_depth_not_available(void) {
  const uint8_t d[8] = {1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF};
  TEST_ASSERT_TRUE(decode(128267, d, 8));
  TEST_ASSERT_EQUAL_INT(0, ngot);
}

static void test_wind_apparent(void) {
  const uint8_t d[8] = {1, 0xE8, 0x03, 0x5C, 0x3D, 2, 0xFF, 0xFF};
  TEST_ASSERT_TRUE(decode(130306, d, 8));
  TEST_ASSERT_EQUAL_DOUBLE(10.0, find(N2K_F_WIND_SPEED_APPARENT)->value);
  TEST_ASSERT_DOUBLE_WITHIN(1e-4, 1.5708, find(N2K_F_WIND_ANGLE_APPARENT)->value);
}

static void test_wind_angle_wraps(void) {
  const uint8_t d[8] = {1, 0xE8, 0x03, 0x14, 0xB8, 2, 0xFF, 0xFF};  // 4.7124 rad
  decode(130306, d, 8);
  TEST_ASSERT_DOUBLE_WITHIN(1e-3, -1.5708, find(N2K_F_WIND_ANGLE_APPARENT)->value);
}

static void test_wind_true_ignored(void) {
  const uint8_t d[8] = {1, 0xE8, 0x03, 0x5C, 0x3D, 3, 0xFF, 0xFF};
  TEST_ASSERT_TRUE(decode(130306, d, 8));
  TEST_ASSERT_EQUAL_INT(0, ngot);
}

static void test_battery(void) {
  const uint8_t d[8] = {1, 0xE2, 0x04, 0x67, 0xFF, 0x77, 0x74, 0xFF};
  TEST_ASSERT_TRUE(decode(127508, d, 8));
  TEST_ASSERT_EQUAL_INT(3, ngot);
  TEST_ASSERT_EQUAL_DOUBLE(12.5, find(N2K_F_BATT_VOLTAGE)->value);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, -15.3, find(N2K_F_BATT_CURRENT)->value);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 298.15, find(N2K_F_BATT_TEMPERATURE)->value);
  TEST_ASSERT_EQUAL_UINT8(1, find(N2K_F_BATT_VOLTAGE)->instance);
}

static void test_battery_current_not_available(void) {
  const uint8_t d[8] = {1, 0xE2, 0x04, 0xFF, 0x7F, 0x77, 0x74, 0xFF};
  decode(127508, d, 8);
  TEST_ASSERT_EQUAL_INT(2, ngot);
  TEST_ASSERT_NULL(find(N2K_F_BATT_CURRENT));
}

static void test_engine_rapid(void) {
  const uint8_t d[8] = {1, 0xE0, 0x2E, 0xFF, 0xFF, 0, 0xFF, 0xFF};
  TEST_ASSERT_TRUE(decode(127488, d, 8));
  TEST_ASSERT_EQUAL_INT(1, ngot);
  TEST_ASSERT_EQUAL_DOUBLE(50.0, find(N2K_F_ENGINE_REVOLUTIONS)->value);
  TEST_ASSERT_EQUAL_UINT8(1, got[0].instance);
}

static void test_tank(void) {
  const uint8_t d[8] = {0x11, 0xD4, 0x30, 0xD0, 0x07, 0, 0, 0xFF};
  TEST_ASSERT_TRUE(decode(127505, d, 8));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.5, find(N2K_F_TANK_LEVEL)->value);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.2, find(N2K_F_TANK_CAPACITY)->value);
  TEST_ASSERT_EQUAL_UINT8(1, find(N2K_F_TANK_LEVEL)->instance);
  TEST_ASSERT_EQUAL_UINT8(1, find(N2K_F_TANK_LEVEL)->fluid);
  const uint8_t o[8] = {0xF2, 0xD4, 0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  ngot = 0;
  decode(127505, o, 8);
  TEST_ASSERT_EQUAL_UINT8(255, find(N2K_F_TANK_LEVEL)->fluid);
  TEST_ASSERT_EQUAL_UINT8(2, find(N2K_F_TANK_LEVEL)->instance);
  TEST_ASSERT_NULL(find(N2K_F_TANK_CAPACITY));
}

static void test_unknown_pgn_and_short_messages(void) {
  const uint8_t d[8] = {0};
  TEST_ASSERT_FALSE(decode(129540, d, 8));   // GNSS satellites in view: not decoded
  TEST_ASSERT_TRUE(decode(128267, d, 3));
  TEST_ASSERT_TRUE(decode(130306, d, 0));
  TEST_ASSERT_TRUE(decode(127505, d, 2));
  TEST_ASSERT_EQUAL_INT(0, ngot);
}

static void test_fluid_names(void) {
  TEST_ASSERT_EQUAL_STRING("fuel", n2k_fluid_name(0));
  TEST_ASSERT_EQUAL_STRING("freshWater", n2k_fluid_name(1));
  TEST_ASSERT_EQUAL_STRING("blackWater", n2k_fluid_name(5));
  TEST_ASSERT_NULL(n2k_fluid_name(255));
}


// ---- second slice ----
static void le(uint8_t *d, int off, uint64_t v, int n) {
  for (int i = 0; i < n; i++) d[off + i] = (uint8_t)(v >> (8 * i));
}

static void test_engine_dynamic(void) {
  uint8_t d[26];
  memset(d, 0xFF, sizeof d);
  d[0] = 0;
  le(d, 1, 3200, 2); le(d, 3, 3532, 2); le(d, 5, 35000, 2); le(d, 7, 1380, 2); le(d, 9, 125, 2); le(d, 11, 3600000, 4);
  TEST_ASSERT_TRUE(decode(127489, d, 26));
  TEST_ASSERT_EQUAL_DOUBLE(320000.0, find(N2K_F_ENGINE_OIL_PRESSURE)->value);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 353.2, find(N2K_F_ENGINE_OIL_TEMPERATURE)->value);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 350.0, find(N2K_F_ENGINE_COOLANT_TEMPERATURE)->value);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 13.8, find(N2K_F_ENGINE_ALTERNATOR_VOLTAGE)->value);
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, 12.5 / 3.6e6, find(N2K_F_ENGINE_FUEL_RATE)->value);
  TEST_ASSERT_EQUAL_DOUBLE(3600000.0, find(N2K_F_ENGINE_HOURS)->value);
  TEST_ASSERT_EQUAL_UINT8(0, find(N2K_F_ENGINE_HOURS)->instance);
  le(d, 5, 0xFFFF, 2);
  ngot = 0;
  decode(127489, d, 26);
  TEST_ASSERT_NULL(find(N2K_F_ENGINE_COOLANT_TEMPERATURE));
  TEST_ASSERT_NOT_NULL(find(N2K_F_ENGINE_OIL_PRESSURE));
  ngot = 0;
  TEST_ASSERT_TRUE(decode(127489, d, 5));   // truncated: only what fits
  TEST_ASSERT_NOT_NULL(find(N2K_F_ENGINE_OIL_PRESSURE));
  TEST_ASSERT_NULL(find(N2K_F_ENGINE_ALTERNATOR_VOLTAGE));
}

static void test_battery_soc(void) {
  uint8_t d[11];
  memset(d, 0xFF, sizeof d);
  d[0] = 1; d[1] = 1; d[2] = 0; d[3] = 82; d[4] = 100; le(d, 5, 600, 2);
  TEST_ASSERT_TRUE(decode(127506, d, 11));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.82, find(N2K_F_BATT_SOC)->value);
  TEST_ASSERT_EQUAL_DOUBLE(36000.0, find(N2K_F_BATT_TIME_REMAINING)->value);
  TEST_ASSERT_EQUAL_UINT8(1, find(N2K_F_BATT_SOC)->instance);
  d[3] = 0xFF; le(d, 5, 0xFFFF, 2);
  ngot = 0;
  decode(127506, d, 11);
  TEST_ASSERT_EQUAL_INT(0, ngot);
}

static void test_heading(void) {
  uint8_t d[8];
  memset(d, 0xFF, sizeof d);
  d[0] = 1; le(d, 1, 15708, 2); le(d, 3, 100, 2); le(d, 5, 500, 2); d[7] = 0;
  TEST_ASSERT_TRUE(decode(127250, d, 8));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.5708, find(N2K_F_HEADING_TRUE)->value);
  TEST_ASSERT_NULL(find(N2K_F_HEADING_MAGNETIC));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.05, find(N2K_F_MAG_VARIATION)->value);
  d[7] = 1;
  ngot = 0;
  decode(127250, d, 8);
  TEST_ASSERT_NOT_NULL(find(N2K_F_HEADING_MAGNETIC));
  TEST_ASSERT_NULL(find(N2K_F_HEADING_TRUE));
  le(d, 1, 0xFFFF, 2);
  ngot = 0;
  decode(127250, d, 8);
  TEST_ASSERT_NULL(find(N2K_F_HEADING_MAGNETIC));
}

static void test_cog_sog(void) {
  uint8_t d[8];
  memset(d, 0xFF, sizeof d);
  d[0] = 1; d[1] = 0; le(d, 2, 31416, 2); le(d, 4, 514, 2);
  TEST_ASSERT_TRUE(decode(129026, d, 8));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 3.1416, find(N2K_F_COG_TRUE)->value);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 5.14, find(N2K_F_SOG)->value);
  d[1] = 1;
  ngot = 0;
  decode(129026, d, 8);
  TEST_ASSERT_NOT_NULL(find(N2K_F_COG_MAGNETIC));
  le(d, 2, 0xFFFF, 2);
  ngot = 0;
  decode(129026, d, 8);
  TEST_ASSERT_NULL(find(N2K_F_COG_MAGNETIC));
  TEST_ASSERT_NOT_NULL(find(N2K_F_SOG));
}

static void test_speed_through_water(void) {
  uint8_t d[8] = {1, 0, 0, 0xFF, 0xFF, 0, 0xFF, 0xFF};
  le(d, 1, 490, 2);
  TEST_ASSERT_TRUE(decode(128259, d, 8));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 4.9, find(N2K_F_STW)->value);
}

static void test_position_rapid(void) {
  uint8_t d[8];
  le(d, 0, (uint32_t)(int32_t)415402833, 4);
  le(d, 4, (uint32_t)(int32_t)-702500000, 4);
  TEST_ASSERT_TRUE(decode(129025, d, 8));
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 41.5402833, find(N2K_F_POSITION)->value);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, -70.25, find(N2K_F_POSITION)->value2);
  le(d, 4, 0x7FFFFFFF, 4);   // longitude not available: no position at all, never (lat, 0)
  ngot = 0;
  decode(129025, d, 8);
  TEST_ASSERT_EQUAL_INT(0, ngot);
}

static void test_gnss_position(void) {
  uint8_t d[43];
  memset(d, 0xFF, sizeof d);
  d[0] = 1; le(d, 1, 20000, 2); le(d, 3, 432000000, 4);
  le(d, 7, (uint64_t)415402833000000000LL, 8);
  le(d, 15, (uint64_t)-702500000000000000LL, 8);
  TEST_ASSERT_TRUE(decode(129029, d, 43));
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 41.5402833, find(N2K_F_POSITION)->value);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, -70.25, find(N2K_F_POSITION)->value2);
  TEST_ASSERT_EQUAL_DOUBLE(20000.0 * 86400 + 43200, find(N2K_F_TIME_EPOCH)->value);
  le(d, 7, 0x7FFFFFFFFFFFFFFFull, 8);   // latitude not available: no position, the time still counts
  ngot = 0;
  decode(129029, d, 43);
  TEST_ASSERT_NULL(find(N2K_F_POSITION));
  TEST_ASSERT_NOT_NULL(find(N2K_F_TIME_EPOCH));
  le(d, 1, 0xFFFF, 2);   // date not available: no clock
  ngot = 0;
  decode(129029, d, 43);
  TEST_ASSERT_NULL(find(N2K_F_TIME_EPOCH));
}

static void test_system_time(void) {
  uint8_t d[8] = {1, 0};
  le(d, 2, 20000, 2); le(d, 4, 432000000, 4);
  TEST_ASSERT_TRUE(decode(126992, d, 8));
  TEST_ASSERT_EQUAL_DOUBLE(20000.0 * 86400 + 43200, find(N2K_F_TIME_EPOCH)->value);
  le(d, 4, 0xFFFFFFFFu, 4);
  ngot = 0;
  decode(126992, d, 8);
  TEST_ASSERT_EQUAL_INT(0, ngot);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_depth);
  RUN_TEST(test_depth_not_available);
  RUN_TEST(test_wind_apparent);
  RUN_TEST(test_wind_angle_wraps);
  RUN_TEST(test_wind_true_ignored);
  RUN_TEST(test_battery);
  RUN_TEST(test_battery_current_not_available);
  RUN_TEST(test_engine_rapid);
  RUN_TEST(test_tank);
  RUN_TEST(test_unknown_pgn_and_short_messages);
  RUN_TEST(test_fluid_names);
  RUN_TEST(test_engine_dynamic);
  RUN_TEST(test_battery_soc);
  RUN_TEST(test_heading);
  RUN_TEST(test_cog_sog);
  RUN_TEST(test_speed_through_water);
  RUN_TEST(test_position_rapid);
  RUN_TEST(test_gnss_position);
  RUN_TEST(test_system_time);
  return UNITY_END();
}
