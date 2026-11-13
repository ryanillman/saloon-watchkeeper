#include <string.h>
#include <unistd.h>
#include "unity.h"
#include "boatdata.h"
#include "platform.h"
#include "electrical.h"
#include "source_can.h"

static source_can_t sc;
static const source_can_id_t ids[] = {{"engine.0", "port"}, {"engine.1", "starboard"}};
static source_can_cfg_t cfg_create = {true, ids, 2, NULL};
static source_can_cfg_t cfg_existing = {false, ids, 2, NULL};

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
  n2k_value_t v = {N2K_F_DEPTH_BELOW_TRANSDUCER, 0, 0, 1, 0};
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
  v = (n2k_value_t){N2K_F_BATT_VOLTAGE, 3, 0, 1, 0};
  source_can_path(&v, &cfg_create, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("electrical.batteries.3.voltage", p);
  v.field = N2K_F_BATT_CURRENT;
  source_can_path(&v, &cfg_create, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("electrical.batteries.3.current", p);
  v.field = N2K_F_BATT_TEMPERATURE;
  source_can_path(&v, &cfg_create, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("electrical.batteries.3.temperature", p);
  v = (n2k_value_t){N2K_F_ENGINE_REVOLUTIONS, 0, 0, 1, 0};
  source_can_path(&v, &cfg_create, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("propulsion.port.revolutions", p);   // mapped
  v.instance = 2;
  source_can_path(&v, &cfg_create, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("propulsion.2.revolutions", p);      // default id
  v.field = N2K_F_ENGINE_BOOST;
  source_can_path(&v, &cfg_create, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("propulsion.2.boostPressure", p);
  v = (n2k_value_t){N2K_F_TANK_LEVEL, 1, 1, 1, 0};
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
  const uint8_t b[8] = {1, 0xE2, 0x04, 0x67, 0xFF, 0x77, 0x74, 0xFF};   // battery 1: unmapped instance
  feed(127508, b, 8);
  TEST_ASSERT_EQUAL(BD_NONE, bd_find("electrical.batteries.1.voltage"));
  TEST_ASSERT_EQUAL_UINT32(0, sc.values);
  TEST_ASSERT_EQUAL_UINT32(3, sc.unmapped);
  bd_intern("electrical.batteries.1.voltage");   // Signal K knows this battery: the bus fills it in
  feed(127508, b, 8);
  bd_sample_t s;
  TEST_ASSERT_TRUE(get("electrical.batteries.1.voltage", &s));
  TEST_ASSERT_EQUAL_UINT32(1, sc.values);
  TEST_ASSERT_EQUAL(BD_NONE, bd_find("electrical.batteries.1.current"));
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


static void le(uint8_t *d, int off, uint64_t v, int n) {
  for (int i = 0; i < n; i++) d[off + i] = (uint8_t)(v >> (8 * i));
}

static void test_new_paths(void) {
  char p[96];
  struct { n2k_field_t f; uint8_t inst; const char *path; } t[] = {
    {N2K_F_ENGINE_COOLANT_TEMPERATURE, 0, "propulsion.port.temperature"},
    {N2K_F_ENGINE_OIL_PRESSURE, 0, "propulsion.port.oilPressure"},
    {N2K_F_ENGINE_OIL_TEMPERATURE, 0, "propulsion.port.oilTemperature"},
    {N2K_F_ENGINE_ALTERNATOR_VOLTAGE, 0, "propulsion.port.alternatorVoltage"},
    {N2K_F_ENGINE_FUEL_RATE, 0, "propulsion.port.fuel.rate"},
    {N2K_F_ENGINE_HOURS, 0, "propulsion.port.runTime"},
    {N2K_F_BATT_SOC, 1, "electrical.batteries.1.capacity.stateOfCharge"},
    {N2K_F_BATT_TIME_REMAINING, 1, "electrical.batteries.1.capacity.timeRemaining"},
    {N2K_F_HEADING_TRUE, 0, "navigation.headingTrue"},
    {N2K_F_HEADING_MAGNETIC, 0, "navigation.headingMagnetic"},
    {N2K_F_MAG_VARIATION, 0, "navigation.magneticVariation"},
    {N2K_F_COG_TRUE, 0, "navigation.courseOverGroundTrue"},
    {N2K_F_COG_MAGNETIC, 0, "navigation.courseOverGroundMagnetic"},
    {N2K_F_SOG, 0, "navigation.speedOverGround"},
    {N2K_F_STW, 0, "navigation.speedThroughWater"},
    {N2K_F_POSITION, 0, "navigation.position"},
    {N2K_F_TIME_EPOCH, 0, "navigation.datetime"},
  };
  for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
    n2k_value_t v = {t[i].f, t[i].inst, 0, 1, 0};
    TEST_ASSERT_TRUE_MESSAGE(source_can_path(&v, &cfg_create, p, sizeof p), t[i].path);
    TEST_ASSERT_EQUAL_STRING(t[i].path, p);
  }
  TEST_ASSERT_TRUE(source_can_is_instance(&(n2k_value_t){N2K_F_ENGINE_HOURS, 0, 0, 0, 0}));
  TEST_ASSERT_FALSE(source_can_is_instance(&(n2k_value_t){N2K_F_POSITION, 0, 0, 0, 0}));
  TEST_ASSERT_TRUE(source_can_is_mapped(&(n2k_value_t){N2K_F_ENGINE_HOURS, 1, 0, 0, 0}, &cfg_create));
  TEST_ASSERT_FALSE(source_can_is_mapped(&(n2k_value_t){N2K_F_ENGINE_HOURS, 5, 0, 0, 0}, &cfg_create));
}

static void test_position_and_time_written(void) {
  uint8_t d[8];
  le(d, 0, (uint32_t)(int32_t)415402833, 4);
  le(d, 4, (uint32_t)(int32_t)-702500000, 4);
  feed(129025, d, 8);
  bd_sample_t s;
  TEST_ASSERT_TRUE(get("navigation.position", &s));
  TEST_ASSERT_EQUAL(BD_POSITION, s.kind);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 41.5402833, s.pos.lat);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, -70.25, s.pos.lon);
  uint8_t t[8] = {1, 0};
  le(t, 2, 19000, 2);        // 2022-01-08
  le(t, 4, 432000000, 4);    // 12:00:00
  feed(126992, t, 8);
  TEST_ASSERT_TRUE(get("navigation.datetime", &s));
  TEST_ASSERT_EQUAL(BD_TEXT, s.kind);
  TEST_ASSERT_EQUAL_STRING("2022-01-08T12:00:00.000Z", s.text);
}

static void test_fallback_mode_creates_only_mapped_instances(void) {
  source_can_init(&sc, &cfg_existing);   // create_paths false, engine.0 -> port, engine.1 -> starboard
  const uint8_t e0[8] = {0, 0xE0, 0x2E, 0xFF, 0xFF, 0, 0xFF, 0xFF};
  const uint8_t e5[8] = {5, 0xE0, 0x2E, 0xFF, 0xFF, 0, 0xFF, 0xFF};
  feed(127488, e0, 8);
  feed(127488, e5, 8);
  bd_sample_t s;
  TEST_ASSERT_TRUE(get("propulsion.port.revolutions", &s));   // mapped: created although Signal K has not made it
  TEST_ASSERT_EQUAL(BD_NONE, bd_find("propulsion.5.revolutions"));
  TEST_ASSERT_EQUAL_UINT32(1, sc.unmapped);
  feed(128267, depth_msg, 8);                                 // not an instance value: always created
  TEST_ASSERT_TRUE(get("environment.depth.belowTransducer", &s));
}

static void test_only_mode_creates_everything(void) {
  const uint8_t b[8] = {7, 0xE2, 0x04, 0x67, 0xFF, 0x77, 0x74, 0xFF};
  feed(127508, b, 8);
  bd_sample_t s;
  TEST_ASSERT_TRUE(get("electrical.batteries.7.voltage", &s));
}


// ---- Feeding the electrical resolver ----
static el_t el;
static source_can_t rs;
static void rsetup(void) {
  bd_init();
  el_init(&el);
  source_can_cfg_t c = {true, ids, 2, &el};
  source_can_init(&rs, &c);
}
static void rfeed(uint8_t src, uint32_t pgn, const uint8_t *d, uint16_t len) {
  n2k_msg_t m = {pgn, src, 255, 3, len, d};
  source_can_on_msg(&m, &rs);
}
static const el_endpoint_t *ep_inst(uint8_t inst) {
  for (size_t i = 0; i < el_endpoint_count(&el); i++)
    if (el_endpoint(&el, i)->instance == inst && el_endpoint(&el, i)->domain == EL_DOM_DC) return el_endpoint(&el, i);
  return NULL;
}
static const uint8_t batt0_a[8] = {0x00, 0x9E, 0x05, 0x00, 0x00, 0xFF, 0xFF, 0x2E};   // inst 0, 14.38 V, 0 A (real capture)
static const uint8_t batt0_b[8] = {0x00, 0x9D, 0x05, 0x00, 0x00, 0xFF, 0xFF, 0x4C};   // inst 0, 14.37 V from another device
static const uint8_t batt1_nocur[8] = {0x01, 0x72, 0x05, 0xFF, 0x7F, 0xFF, 0xFF, 0x4C};   // inst 1, 13.94 V, current n/a
static const uint8_t claim36[8] = {0xF8, 0x01, 0xC0, 0x2C, 0x00, 0xA0, 0x46, 0xC0};

static void test_can_two_devices_instance_zero_stay_separate(void) {
  rsetup();
  rfeed(36, 127508, batt0_a, 8);
  rfeed(224, 127508, batt0_b, 8);
  TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)el_endpoint_count(&el));
  TEST_ASSERT_EQUAL(BD_NONE, bd_find("electrical.batteries.0.voltage"));   // no merged raw path
}

static void test_pv_dc_type_frames_make_pv_endpoint(void) {
  rsetup();
  const uint8_t dc[9] = {0x40, 0x01, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  const uint8_t pv[8] = {0x01, 0x24, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x46};
  rfeed(36, 127506, dc, 9);
  rfeed(36, 127508, pv, 8);
  TEST_ASSERT_EQUAL(EL_FN_PV, ep_inst(1)->fn);
}

static void test_claim_frame_sets_name_key_across_address_change(void) {
  rsetup();
  rfeed(36, 60928, claim36, 8);
  rfeed(36, 127508, batt0_a, 8);
  uint64_t key = ep_inst(0)->key;
  rfeed(52, 60928, claim36, 8);   // the same device, re-claimed at another address
  rfeed(52, 127508, batt0_b, 8);
  TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)el_endpoint_count(&el));
  TEST_ASSERT_TRUE(ep_inst(0)->key == key);
}

static void test_product_info_names_model(void) {
  rsetup();
  uint8_t d[134] = {0};
  memcpy(d + 4, "Some Shunt 500A", 15);
  rfeed(224, 126996, d, sizeof d);
  rfeed(224, 127508, batt1_nocur, 8);
  TEST_ASSERT_EQUAL(EL_FN_BATTERY, ep_inst(1)->fn);
  TEST_ASSERT_EQUAL(EL_EV_PRODUCT, ep_inst(1)->ev);
}

static struct { uint32_t pgn; uint8_t dst, d[3]; uint16_t len; } sent[16];
static int nsent;
static bool send_ok = true;
static bool fake_send(uint32_t pgn, uint8_t dst, const uint8_t *d, uint16_t len, void *ctx) {
  (void)ctx;
  if (!send_ok) return false;
  if (nsent < 16) {
    sent[nsent].pgn = pgn;
    sent[nsent].dst = dst;
    sent[nsent].len = len;
    memcpy(sent[nsent].d, d, len <= 3 ? len : 3);
    nsent++;
  }
  return true;
}

static void test_identity_request_sent_once_per_new_source(void) {
  rsetup();
  nsent = 0;
  send_ok = true;
  rfeed(36, 127508, batt0_a, 8);
  rfeed(36, 127508, batt0_a, 8);
  rfeed(37, 127508, batt0_b, 8);
  source_can_request_identity(&rs, 1000, fake_send, NULL);
  TEST_ASSERT_EQUAL_INT(2, nsent);
  TEST_ASSERT_EQUAL_UINT32(59904, sent[0].pgn);
  TEST_ASSERT_EQUAL_UINT8(36, sent[0].dst);
  TEST_ASSERT_EQUAL_UINT8(37, sent[1].dst);
  TEST_ASSERT_EQUAL_UINT8(0x14, sent[0].d[0]);   // 126996
  TEST_ASSERT_EQUAL_UINT8(0xF0, sent[0].d[1]);
  TEST_ASSERT_EQUAL_UINT8(0x01, sent[0].d[2]);
  source_can_request_identity(&rs, 1001, fake_send, NULL);
  TEST_ASSERT_EQUAL_INT(2, nsent);
}

static void test_identity_request_retries_with_backoff(void) {
  rsetup();
  nsent = 0;
  send_ok = true;
  rfeed(36, 127508, batt0_a, 8);
  source_can_request_identity(&rs, 1000, fake_send, NULL);
  source_can_request_identity(&rs, 5999, fake_send, NULL);
  TEST_ASSERT_EQUAL_INT(1, nsent);
  source_can_request_identity(&rs, 6000, fake_send, NULL);   // 5 s after the first
  TEST_ASSERT_EQUAL_INT(2, nsent);
  source_can_request_identity(&rs, 15999, fake_send, NULL);
  TEST_ASSERT_EQUAL_INT(2, nsent);
  source_can_request_identity(&rs, 16000, fake_send, NULL);   // 10 s after the second
  TEST_ASSERT_EQUAL_INT(3, nsent);
}

static void test_identity_request_gives_up_eventually(void) {
  rsetup();
  nsent = 0;
  send_ok = true;
  rfeed(36, 127508, batt0_a, 8);
  for (int64_t t = 0; t < 10000000; t += 1000) source_can_request_identity(&rs, t, fake_send, NULL);
  TEST_ASSERT_EQUAL_INT(5, nsent);
}

static void test_identity_request_stops_after_product_info_and_resumes_on_claim(void) {
  rsetup();
  nsent = 0;
  send_ok = true;
  rfeed(36, 127508, batt0_a, 8);
  uint8_t d[134] = {0};
  memcpy(d + 4, "Mystery", 7);
  rfeed(36, 126996, d, sizeof d);
  source_can_request_identity(&rs, 100000, fake_send, NULL);
  TEST_ASSERT_EQUAL_INT(0, nsent);
  rfeed(36, 60928, claim36, 8);   // it claimed an address again: whoever answers now may be someone else
  source_can_request_identity(&rs, 100001, fake_send, NULL);
  TEST_ASSERT_EQUAL_INT(1, nsent);
}

static void test_identity_request_retried_when_send_refused(void) {
  rsetup();
  nsent = 0;
  send_ok = false;
  rfeed(36, 127508, batt0_a, 8);
  source_can_request_identity(&rs, 1000, fake_send, NULL);
  send_ok = true;
  source_can_request_identity(&rs, 1001, fake_send, NULL);
  TEST_ASSERT_EQUAL_INT(1, nsent);
}

static void test_listen_only_sends_nothing(void) {
  rsetup();
  rfeed(36, 127508, batt0_a, 8);
  source_can_request_identity(&rs, 1000, NULL, NULL);   // the display never transmits: must not crash or change state
  TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)el_endpoint_count(&el));
}

static void test_tick_publishes_and_offline_invalidates(void) {
  rsetup();
  const uint8_t dc[9] = {0x40, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  rfeed(224, 127506, dc, 9);
  rfeed(224, 127508, batt0_a, 8);
  source_can_tick(&rs);
  bd_id_t ids_[4];
  TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)bd_match("electrical.resolved.batteries.*.voltage", ids_, 4));
  bd_sample_t s;
  bd_get(ids_[0], &s);
  TEST_ASSERT_TRUE(s.valid);
  TEST_ASSERT_EQUAL_DOUBLE(14.38, s.num);
  rs.last_msg_ms -= 6000;
  source_can_tick(&rs);
  bd_get(ids_[0], &s);
  TEST_ASSERT_FALSE(s.valid);
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
  RUN_TEST(test_new_paths);
  RUN_TEST(test_position_and_time_written);
  RUN_TEST(test_fallback_mode_creates_only_mapped_instances);
  RUN_TEST(test_only_mode_creates_everything);
  RUN_TEST(test_can_two_devices_instance_zero_stay_separate);
  RUN_TEST(test_pv_dc_type_frames_make_pv_endpoint);
  RUN_TEST(test_claim_frame_sets_name_key_across_address_change);
  RUN_TEST(test_product_info_names_model);
  RUN_TEST(test_identity_request_sent_once_per_new_source);
  RUN_TEST(test_identity_request_retries_with_backoff);
  RUN_TEST(test_identity_request_gives_up_eventually);
  RUN_TEST(test_identity_request_stops_after_product_info_and_resumes_on_claim);
  RUN_TEST(test_identity_request_retried_when_send_refused);
  RUN_TEST(test_listen_only_sends_nothing);
  RUN_TEST(test_tick_publishes_and_offline_invalidates);
  return UNITY_END();
}
