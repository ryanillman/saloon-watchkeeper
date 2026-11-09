#include <string.h>
#include <unistd.h>
#include "unity.h"
#include "boatdata.h"
#include "platform.h"
#include "source_can.h"

static source_can_t sc;
static const source_can_id_t ids[] = {{"engine.0", "port"}, {"engine.1", "starboard"}};
static source_can_cfg_t cfg_create = {true, ids, 2};
static source_can_cfg_t cfg_existing = {false, ids, 2};

void setUp(void) {
  bd_init();
  source_can_init(&sc, &cfg_create);
}
void tearDown(void) {}

static void feed(uint32_t pgn, const uint8_t *d, uint16_t len) {
  n2k_msg_t m = {pgn, 0x20, 255, 3, len, d};
  source_can_on_msg(&m, &sc);
}
static bool get(const char *path, bd_sample_t *s) {
  bd_id_t id = bd_find(path);
  return id != BD_NONE && bd_get(id, s) && s->valid;
}
static const uint8_t depth_msg[8] = {1, 0xD2, 0x04, 0, 0, 0xF4, 0x01, 10};  // 12.34 m, surface offset 0.5 m

static void test_paths(void) {
  char p[96];
  n2k_value_t v = {N2K_F_DEPTH_BELOW_TRANSDUCER, 0, 0, 1};
  TEST_ASSERT_TRUE(source_can_path(&v, &cfg_create, p, sizeof p));
  TEST_ASSERT_EQUAL_STRING("environment.depth.belowTransducer", p);
  v.field = N2K_F_DEPTH_SURFACE_TO_TRANSDUCER;
  source_can_path(&v, &cfg_create, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("environment.depth.surfaceToTransducer", p);
  v.field = N2K_F_DEPTH_TRANSDUCER_TO_KEEL;
  source_can_path(&v, &cfg_create, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("environment.depth.transducerToKeel", p);
  v.field = N2K_F_WIND_SPEED_APPARENT;
  source_can_path(&v, &cfg_create, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("environment.wind.speedApparent", p);
  v.field = N2K_F_WIND_ANGLE_APPARENT;
  source_can_path(&v, &cfg_create, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("environment.wind.angleApparent", p);
  v = (n2k_value_t){N2K_F_BATT_VOLTAGE, 3, 0, 1};
  source_can_path(&v, &cfg_create, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("electrical.batteries.3.voltage", p);
  v.field = N2K_F_BATT_CURRENT;
  source_can_path(&v, &cfg_create, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("electrical.batteries.3.current", p);
  v.field = N2K_F_BATT_TEMPERATURE;
  source_can_path(&v, &cfg_create, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("electrical.batteries.3.temperature", p);
  v = (n2k_value_t){N2K_F_ENGINE_REVOLUTIONS, 0, 0, 1};
  source_can_path(&v, &cfg_create, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("propulsion.port.revolutions", p);   // mapped
  v.instance = 2;
  source_can_path(&v, &cfg_create, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("propulsion.2.revolutions", p);      // default id
  v.field = N2K_F_ENGINE_BOOST;
  source_can_path(&v, &cfg_create, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("propulsion.2.boostPressure", p);
  v = (n2k_value_t){N2K_F_TANK_LEVEL, 1, 1, 1};
  source_can_path(&v, &cfg_create, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("tanks.freshWater.1.currentLevel", p);
  v.field = N2K_F_TANK_CAPACITY;
  source_can_path(&v, &cfg_create, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("tanks.freshWater.1.capacity", p);
  v.fluid = 255;
  TEST_ASSERT_FALSE(source_can_path(&v, &cfg_create, p, sizeof p));
}

static void test_depth_written_to_boatdata(void) {
  feed(128267, depth_msg, 8);
  bd_sample_t s;
  TEST_ASSERT_TRUE(get("environment.depth.belowTransducer", &s));
  TEST_ASSERT_EQUAL_DOUBLE(12.34, s.num);
  TEST_ASSERT_EQUAL_UINT8(BD_SRC_N2K, s.source);
  TEST_ASSERT_TRUE(get("environment.depth.surfaceToTransducer", &s));
}

static void test_two_engines_do_not_collide(void) {
  const uint8_t a[8] = {0, 0xE0, 0x2E, 0xFF, 0xFF, 0, 0xFF, 0xFF};   // 3000 rpm
  const uint8_t b[8] = {1, 0x80, 0x25, 0xFF, 0xFF, 0, 0xFF, 0xFF};   // 2400 rpm
  feed(127488, a, 8);
  feed(127488, b, 8);
  bd_sample_t p, q;
  TEST_ASSERT_TRUE(get("propulsion.port.revolutions", &p));
  TEST_ASSERT_TRUE(get("propulsion.starboard.revolutions", &q));
  TEST_ASSERT_EQUAL_DOUBLE(50.0, p.num);
  TEST_ASSERT_EQUAL_DOUBLE(40.0, q.num);
}

static void test_tanks_distinct_by_instance_and_fluid(void) {
  const uint8_t a[8] = {0x11, 0xD4, 0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};   // fresh 1, 50 %
  const uint8_t b[8] = {0x12, 0xA6, 0x61, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};   // fresh 2, 100 %
  const uint8_t c[8] = {0x01, 0x58, 0x1B, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};   // fuel 1, 7000*0.004 = 28 %
  feed(127505, a, 8);
  feed(127505, b, 8);
  feed(127505, c, 8);
  bd_sample_t s;
  TEST_ASSERT_TRUE(get("tanks.freshWater.1.currentLevel", &s));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.5, s.num);
  TEST_ASSERT_TRUE(get("tanks.freshWater.2.currentLevel", &s));
  TEST_ASSERT_DOUBLE_WITHIN(1e-3, 1.0, s.num);
  TEST_ASSERT_TRUE(get("tanks.fuel.1.currentLevel", &s));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.28, s.num);
}

static void test_existing_only_creates_nothing(void) {
  source_can_init(&sc, &cfg_existing);
  feed(128267, depth_msg, 8);
  TEST_ASSERT_EQUAL(BD_NONE, bd_find("environment.depth.belowTransducer"));
  TEST_ASSERT_EQUAL_UINT32(0, sc.values);
  TEST_ASSERT_EQUAL_UINT32(2, sc.unmapped);  // below transducer and surface offset
  bd_intern("environment.depth.belowTransducer");
  feed(128267, depth_msg, 8);
  bd_sample_t s;
  TEST_ASSERT_TRUE(get("environment.depth.belowTransducer", &s));
  TEST_ASSERT_EQUAL_UINT32(1, sc.values);
  TEST_ASSERT_EQUAL(BD_NONE, bd_find("environment.depth.surfaceToTransducer"));
}

static void test_signal_k_wins_while_fresh(void) {
  bd_source_register(BD_SRC_SK, "signalk", 2);
  bd_id_t id = bd_intern("environment.depth.belowTransducer");
  bd_set_number(id, 5.0, BD_SRC_SK, platform_millis());
  feed(128267, depth_msg, 8);
  bd_sample_t s;
  bd_get(id, &s);
  TEST_ASSERT_EQUAL_DOUBLE(5.0, s.num);
  bd_set_hold_ms(1);
  usleep(5000);
  feed(128267, depth_msg, 8);
  bd_get(id, &s);
  TEST_ASSERT_EQUAL_DOUBLE(12.34, s.num);
}

static void test_state_follows_traffic(void) {
  TEST_ASSERT_EQUAL(BD_SRC_CONNECTING, bd_source_state(BD_SRC_N2K));
  feed(128267, depth_msg, 8);
  source_can_tick(&sc);
  TEST_ASSERT_EQUAL(BD_SRC_LIVE, bd_source_state(BD_SRC_N2K));
  sc.last_msg_ms -= 6000;
  source_can_tick(&sc);
  TEST_ASSERT_EQUAL(BD_SRC_OFFLINE, bd_source_state(BD_SRC_N2K));
  bd_sample_t s;
  bd_get(bd_find("environment.depth.belowTransducer"), &s);
  TEST_ASSERT_FALSE(s.valid);
  TEST_ASSERT_TRUE(s.invalidated);
}

static void test_undecoded_pgn_does_not_make_it_live(void) {
  const uint8_t d[8] = {0};
  feed(126983, d, 8);
  source_can_tick(&sc);
  TEST_ASSERT_EQUAL(BD_SRC_CONNECTING, bd_source_state(BD_SRC_N2K));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_paths);
  RUN_TEST(test_depth_written_to_boatdata);
  RUN_TEST(test_two_engines_do_not_collide);
  RUN_TEST(test_tanks_distinct_by_instance_and_fluid);
  RUN_TEST(test_existing_only_creates_nothing);
  RUN_TEST(test_signal_k_wins_while_fresh);
  RUN_TEST(test_state_follows_traffic);
  RUN_TEST(test_undecoded_pgn_does_not_make_it_live);
  return UNITY_END();
}
