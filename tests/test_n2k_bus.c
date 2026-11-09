#include <string.h>
#include "unity.h"
#include "n2k_bus.h"

#define MAXF 512
static n2k_frame_t in_q[MAXF];
static int in_head, in_len;
static n2k_frame_t sent[MAXF];
static int nsent, send_calls, recover_calls;
static bool refuse;
static n2k_can_state_t can_state;
static uint32_t now;

static bool f_recv(n2k_frame_t *f, void *ctx) {
  (void)ctx;
  if (in_head >= in_len) return false;
  *f = in_q[in_head++];
  return true;
}
static bool f_send(const n2k_frame_t *f, void *ctx) {
  (void)ctx;
  send_calls++;
  if (refuse) return false;
  if (nsent < MAXF) sent[nsent++] = *f;
  return true;
}
static n2k_can_state_t f_state(void *ctx) { (void)ctx; return can_state; }
static void f_recover(void *ctx) { (void)ctx; recover_calls++; }
static uint32_t f_now(void *ctx) { (void)ctx; return now; }

static n2k_bus_t bus;
static n2k_node_t node;
static n2k_node_config_t cfg;
static n2k_node_ops_t node_ops;
static n2k_qent_t queue[4];
static n2k_fp_slot_t slots[4];
static int app_count;
static n2k_msg_t app_last;
static uint8_t app_data[N2K_FP_MAX];

static void app_cb(const n2k_msg_t *m, void *ctx) {
  (void)ctx;
  app_last = *m;
  memcpy(app_data, m->data, m->len);
  app_last.data = app_data;
  app_count++;
}

static const n2k_can_ops_t can = {f_recv, f_send, f_state, f_recover, NULL};

static void make(bool with_node) {
  in_head = in_len = nsent = send_calls = recover_calls = app_count = 0;
  refuse = false;
  can_state = N2K_CAN_OK;
  now = 1000;
  memset(&cfg, 0, sizeof cfg);
  cfg.name = (n2k_name_fields_t){.unique_number = 100, .manufacturer = 999, .function = 130,
                                 .device_class = 25, .industry_group = 4};
  cfg.preferred_address = 0x40;
  node_ops = (n2k_node_ops_t){n2k_bus_tx_frame, f_now, NULL, NULL, &bus};
  n2k_node_init(&node, &cfg, &node_ops, queue, 4);
  n2k_bus_init(&bus, &can, with_node ? &node : NULL, slots, 4, app_cb, NULL, f_now, NULL);
}
void setUp(void) { make(true); }
void tearDown(void) {}

static void queue_frame(uint32_t id, const uint8_t *d, uint8_t len) {
  in_q[in_len].id = id;
  in_q[in_len].len = len;
  memcpy(in_q[in_len].data, d, len);
  in_len++;
}
static void run(int steps, uint32_t max) {
  for (int i = 0; i < steps; i++) {
    now += 10;
    n2k_bus_step(&bus, max);
  }
}

static void test_frames_reach_app_callback(void) {
  make(false);
  const uint8_t d[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  queue_frame(n2k_id_pack(2, 127488, 0x17, 255), d, 8);
  queue_frame(n2k_id_pack(2, 127488, 0x18, 255), d, 8);
  n2k_bus_step(&bus, 8);
  TEST_ASSERT_EQUAL_INT(2, app_count);
  TEST_ASSERT_EQUAL_UINT32(2, n2k_bus_stats(&bus)->rx_frames);
}

static void test_fast_packet_through_the_bus(void) {
  make(false);
  uint8_t d[20];
  for (int i = 0; i < 20; i++) d[i] = (uint8_t)(i + 5);
  n2k_tx_t tx;
  n2k_frame_t f;
  TEST_ASSERT_TRUE(n2k_tx_begin(&tx, 6, 126996, 0x20, 255, d, 20, 1));
  while (n2k_tx_next(&tx, &f)) queue_frame(f.id, f.data, f.len);
  run(1, 8);
  TEST_ASSERT_EQUAL_INT(1, app_count);
  TEST_ASSERT_EQUAL_UINT16(20, app_last.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(d, app_data, 20);
}

static void test_node_claim_goes_out_and_ready(void) {
  n2k_node_start(&node);
  TEST_ASSERT_EQUAL_INT(1, nsent);
  TEST_ASSERT_EQUAL_HEX32(0x18EEFF40, sent[0].id);
  run(24, 8);
  TEST_ASSERT_EQUAL(N2K_NODE_CLAIMING, n2k_node_state(&node));
  run(1, 8);
  TEST_ASSERT_EQUAL(N2K_NODE_READY, n2k_node_state(&node));
  TEST_ASSERT_EQUAL_UINT32(1, n2k_bus_stats(&bus)->tx_frames);
}

static void test_listen_only_never_sends(void) {
  make(false);
  const uint8_t d[8] = {0};
  queue_frame(n2k_id_pack(6, 59904, 0x23, 0xFF), d, 3);
  queue_frame(n2k_id_pack(6, 60928, 0x23, 0xFF), d, 8);
  run(5, 8);
  TEST_ASSERT_EQUAL_INT(0, send_calls);
}

static void test_burst_does_not_starve_the_node_tick(void) {
  const uint8_t d[8] = {0};
  for (int i = 0; i < 400; i++) queue_frame(n2k_id_pack(2, 127488, 0x17, 255), d, 8);
  n2k_node_start(&node);
  run(25, 8);  // 250 ms of clock; 200 of the 400 frames taken
  TEST_ASSERT_EQUAL_UINT32(200, n2k_bus_stats(&bus)->rx_frames);
  TEST_ASSERT_EQUAL(N2K_NODE_READY, n2k_node_state(&node));
}

static void test_bus_off_recovers_and_restarts_claim(void) {
  n2k_node_start(&node);
  run(25, 8);
  TEST_ASSERT_EQUAL(N2K_NODE_READY, n2k_node_state(&node));
  uint8_t m[2] = {1, 2};
  n2k_node_send(&node, 127488, 255, 2, m, 2);
  n2k_node_send(&node, 127488, 255, 2, m, 2);
  const uint8_t d[8] = {0};
  queue_frame(n2k_id_pack(2, 127488, 0x17, 255), d, 8);
  can_state = N2K_CAN_BUS_OFF;
  run(3, 8);
  TEST_ASSERT_EQUAL_UINT32(1, n2k_bus_stats(&bus)->bus_off_events);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(1, recover_calls);
  TEST_ASSERT_EQUAL_INT(0, app_count);  // nothing received while off
  can_state = N2K_CAN_OK;
  nsent = 0;
  run(1, 8);
  TEST_ASSERT_EQUAL(N2K_NODE_CLAIMING, n2k_node_state(&node));
  TEST_ASSERT_EQUAL_UINT32(2, n2k_node_dropped(&node));
  TEST_ASSERT_GREATER_OR_EQUAL_INT(1, nsent);
  TEST_ASSERT_EQUAL_HEX32(0x18EEFF40, sent[0].id);  // the new claim, not the stale messages
  TEST_ASSERT_EQUAL_INT(1, app_count);              // receiving resumed
}

static void test_tx_refusal_counted(void) {
  n2k_node_start(&node);
  run(25, 8);
  uint8_t m[1] = {7};
  n2k_node_send(&node, 127488, 255, 2, m, 1);
  refuse = true;
  run(5, 8);
  TEST_ASSERT_EQUAL_UINT32(5, n2k_bus_stats(&bus)->tx_refused);
  TEST_ASSERT_EQUAL_UINT32(1, n2k_node_dropped(&node));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_frames_reach_app_callback);
  RUN_TEST(test_fast_packet_through_the_bus);
  RUN_TEST(test_node_claim_goes_out_and_ready);
  RUN_TEST(test_listen_only_never_sends);
  RUN_TEST(test_burst_does_not_starve_the_node_tick);
  RUN_TEST(test_bus_off_recovers_and_restarts_claim);
  RUN_TEST(test_tx_refusal_counted);
  return UNITY_END();
}
