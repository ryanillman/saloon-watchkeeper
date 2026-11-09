// app_can: the bus source as the app starts it from config.
#include <string.h>
#include "app_can.h"
#include "boatdata.h"
#include "config.h"
#include "platform.h"
#include "unity.h"

static n2k_frame_t in_q[32];
static int in_head, in_len;
static int send_calls;

static bool f_recv(n2k_frame_t *f, void *ctx) {
  (void)ctx;
  if (in_head >= in_len) return false;
  *f = in_q[in_head++];
  return true;
}
static bool f_send(const n2k_frame_t *f, void *ctx) {
  (void)f; (void)ctx;
  send_calls++;
  return true;
}
static n2k_can_state_t f_state(void *ctx) { (void)ctx; return N2K_CAN_OK; }
static void f_recover(void *ctx) { (void)ctx; }
static const n2k_can_ops_t ops = {f_recv, f_send, f_state, f_recover, NULL};

static cfg_t cfg;

void setUp(void) {
  bd_init();
  app_can_stop();
  in_head = in_len = send_calls = 0;
  cfg_defaults(&cfg);
  cfg.n2k.enabled = true;
}
void tearDown(void) {}

static void queue(uint32_t pgn, const uint8_t *d, uint8_t len) {
  in_q[in_len].id = n2k_id_pack(3, pgn, 0x20, 255);
  in_q[in_len].len = len;
  memcpy(in_q[in_len].data, d, len);
  in_len++;
}
static bool get(const char *path, bd_sample_t *s) {
  bd_id_t id = bd_find(path);
  return id != BD_NONE && bd_get(id, s) && s->valid;
}
static const uint8_t depth_msg[8] = {1, 0xD2, 0x04, 0, 0, 0xF4, 0x01, 10};
static const uint8_t engine0[8] = {0, 0xE0, 0x2E, 0xFF, 0xFF, 0, 0xFF, 0xFF};
static const uint8_t engine5[8] = {5, 0xE0, 0x2E, 0xFF, 0xFF, 0, 0xFF, 0xFF};

static void test_start_and_poll_writes_boatdata(void) {
  cfg.n2k.only = true;
  TEST_ASSERT_TRUE(app_can_start(&cfg, ops));
  queue(128267, depth_msg, 8);
  app_can_poll();
  bd_sample_t s;
  TEST_ASSERT_TRUE(get("environment.depth.belowTransducer", &s));
  TEST_ASSERT_EQUAL_DOUBLE(12.34, s.num);
  TEST_ASSERT_EQUAL_INT(0, send_calls);   // listen-only: the display never transmits
}

static void test_ids_from_config_are_used(void) {
  strcpy(cfg.n2k.ids[0].key, "engine.0");
  strcpy(cfg.n2k.ids[0].id, "port");
  cfg.n2k.id_count = 1;
  TEST_ASSERT_TRUE(app_can_start(&cfg, ops));
  queue(127488, engine0, 8);
  app_can_poll();
  bd_sample_t s;
  TEST_ASSERT_TRUE(get("propulsion.port.revolutions", &s));   // fallback mode, but the instance is mapped
}

static void test_fallback_mode_does_not_create_unmapped_instances(void) {
  TEST_ASSERT_TRUE(app_can_start(&cfg, ops));   // fallback (only == false)
  queue(127488, engine5, 8);
  app_can_poll();
  TEST_ASSERT_EQUAL(BD_NONE, bd_find("propulsion.5.revolutions"));
  app_can_status_t st;
  app_can_status(&st);
  TEST_ASSERT_EQUAL_UINT32(1, st.unmapped);
}

static void test_status_counts(void) {
  cfg.n2k.only = true;
  app_can_status_t st;
  app_can_status(&st);
  TEST_ASSERT_FALSE(st.started);
  TEST_ASSERT_TRUE(app_can_start(&cfg, ops));
  queue(128267, depth_msg, 8);
  queue(127488, engine0, 8);
  app_can_poll();
  app_can_status(&st);
  TEST_ASSERT_TRUE(st.started);
  TEST_ASSERT_EQUAL_UINT32(2, st.rx_frames);
  TEST_ASSERT_EQUAL_UINT32(3, st.values);   // two depth values and one rpm
  TEST_ASSERT_EQUAL(BD_SRC_LIVE, st.state);
}

static void test_second_start_refused(void) {
  TEST_ASSERT_TRUE(app_can_start(&cfg, ops));
  TEST_ASSERT_FALSE(app_can_start(&cfg, ops));
}

static void test_poll_before_start_is_harmless(void) {
  app_can_poll();
  app_can_status_t st;
  app_can_status(&st);
  TEST_ASSERT_FALSE(st.started);
}

static void test_network_wanted(void) {
  TEST_ASSERT_TRUE(app_network_wanted(&cfg));    // enabled, fallback
  cfg.n2k.only = true;
  TEST_ASSERT_FALSE(app_network_wanted(&cfg));   // enabled and only: no Signal K client
  cfg.n2k.enabled = false;
  TEST_ASSERT_TRUE(app_network_wanted(&cfg));    // only means nothing while the bus is off
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_start_and_poll_writes_boatdata);
  RUN_TEST(test_ids_from_config_are_used);
  RUN_TEST(test_fallback_mode_does_not_create_unmapped_instances);
  RUN_TEST(test_status_counts);
  RUN_TEST(test_second_start_refused);
  RUN_TEST(test_poll_before_start_is_harmless);
  RUN_TEST(test_network_wanted);
  return UNITY_END();
}
