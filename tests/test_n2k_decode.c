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
  TEST_ASSERT_FALSE(decode(127250, d, 8));
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
  return UNITY_END();
}
