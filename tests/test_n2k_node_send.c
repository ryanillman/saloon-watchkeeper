#include <string.h>
#include "unity.h"
#include "n2k_node.h"

#define MAXF 128
static n2k_frame_t frames[MAXF];
static int nframes;
static int tx_budget;  // frames the fake bus accepts; -1 is unlimited
static uint32_t now;
static n2k_node_t node;
static n2k_qent_t queue[4];
static n2k_node_config_t cfg;
static int locks, unlocks;

static bool fake_tx(const n2k_frame_t *f, void *ctx) {
  (void)ctx;
  if (tx_budget == 0) return false;
  if (tx_budget > 0) tx_budget--;
  if (nframes < MAXF) frames[nframes++] = *f;
  return true;
}
static uint32_t fake_now(void *ctx) { (void)ctx; return now; }
static void fake_lock(void *ctx) { (void)ctx; locks++; }
static void fake_unlock(void *ctx) { (void)ctx; unlocks++; }

static n2k_node_ops_t ops = {fake_tx, fake_now, NULL, NULL, NULL};

// Reassembly of what the node transmitted.
static n2k_fp_slot_t slots[2];
static n2k_rx_t rx;
static int got_count;
static n2k_msg_t got;
static uint8_t got_data[N2K_FP_MAX];
static void on_msg(const n2k_msg_t *m, void *ctx) {
  (void)ctx;
  got = *m;
  memcpy(got_data, m->data, m->len);
  got_count++;
}
static void reassemble(int from) {
  got_count = 0;
  n2k_rx_init(&rx, slots, 2, on_msg, NULL);
  for (int i = from; i < nframes; i++) n2k_feed_frame(&rx, frames[i].id, frames[i].data, frames[i].len, 0);
}

static void make_node(size_t qcount) {
  nframes = 0;
  tx_budget = -1;
  now = 1000;
  n2k_node_init(&node, &cfg, &ops, queue, qcount);
}
static void make_ready(void) {
  n2k_node_start(&node);
  now += 250;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL(N2K_NODE_READY, n2k_node_state(&node));
  nframes = 0;
}

void setUp(void) {
  memset(&cfg, 0, sizeof cfg);
  cfg.name = (n2k_name_fields_t){.unique_number = 100, .manufacturer = 999, .function = 130,
                                 .device_class = 25, .industry_group = 4};
  cfg.preferred_address = 0x40;
  cfg.product.n2k_db_version = 2100;
  cfg.product.product_code = 0x1234;
  strcpy(cfg.product.model_id, "ABC");
  cfg.product.certification = 7;
  cfg.product.load_equivalency = 3;
  ops.lock = ops.unlock = NULL;
  locks = unlocks = 0;
  make_node(4);
}
void tearDown(void) {}

static void request(uint8_t src, uint8_t dst, uint32_t pgn, uint16_t len) {
  uint8_t d[3] = {(uint8_t)pgn, (uint8_t)(pgn >> 8), (uint8_t)(pgn >> 16)};
  n2k_msg_t m = {59904, src, dst, 6, len, d};
  n2k_node_on_msg(&node, &m);
}

static void test_send_before_ready_refused(void) {
  uint8_t d[3] = {1, 2, 3};
  TEST_ASSERT_EQUAL(N2K_SEND_NOT_READY, n2k_node_send(&node, 127488, 255, 2, d, 3));  // IDLE
  n2k_node_start(&node);
  TEST_ASSERT_EQUAL(N2K_SEND_NOT_READY, n2k_node_send(&node, 127488, 255, 2, d, 3));  // CLAIMING
  nframes = 0;
  now += 1000;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL(N2K_NODE_READY, n2k_node_state(&node));
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_INT(0, nframes);  // nothing was queued behind the refusal
}

static void test_send_single_frame(void) {
  make_ready();
  uint8_t d[3] = {1, 2, 3};
  TEST_ASSERT_EQUAL(N2K_SEND_OK, n2k_node_send(&node, 127488, 255, 2, d, 3));
  TEST_ASSERT_EQUAL_INT(0, nframes);  // send never touches the bus
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_INT(1, nframes);
  TEST_ASSERT_EQUAL_HEX32(0x09F20040, frames[0].id);
  const uint8_t want[8] = {1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, frames[0].data, 8);
}

static void test_send_fast_packet_in_order(void) {
  make_ready();
  uint8_t d[40];
  for (int i = 0; i < 40; i++) d[i] = (uint8_t)(i + 1);
  TEST_ASSERT_EQUAL(N2K_SEND_OK, n2k_node_send(&node, 126983, 255, 2, d, 40));
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_INT(6, nframes);
  reassemble(0);
  TEST_ASSERT_EQUAL_INT(1, got_count);
  TEST_ASSERT_EQUAL_UINT16(40, got.len);
  TEST_ASSERT_EQUAL_UINT8(0x40, got.src);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(d, got_data, 40);
}

static void test_two_messages_one_per_tick(void) {
  make_ready();
  uint8_t a[2] = {0xAA, 0xAA}, b[2] = {0xBB, 0xBB};
  n2k_node_send(&node, 127488, 255, 2, a, 2);
  n2k_node_send(&node, 127488, 255, 2, b, 2);
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_INT(1, nframes);
  TEST_ASSERT_EQUAL_HEX8(0xAA, frames[0].data[0]);
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_INT(2, nframes);
  TEST_ASSERT_EQUAL_HEX8(0xBB, frames[1].data[0]);
}

static void test_queue_full(void) {
  make_node(2);
  make_ready();
  uint8_t d[1] = {1};
  TEST_ASSERT_EQUAL(N2K_SEND_OK, n2k_node_send(&node, 127488, 255, 2, d, 1));
  TEST_ASSERT_EQUAL(N2K_SEND_OK, n2k_node_send(&node, 127488, 255, 2, d, 1));
  TEST_ASSERT_EQUAL(N2K_SEND_QUEUE_FULL, n2k_node_send(&node, 127488, 255, 2, d, 1));
  n2k_node_tick(&node);  // one slot frees up
  TEST_ASSERT_EQUAL(N2K_SEND_OK, n2k_node_send(&node, 127488, 255, 2, d, 1));
}

static void test_bad_lengths(void) {
  make_ready();
  uint8_t d[224] = {0};
  TEST_ASSERT_EQUAL(N2K_SEND_BAD_LENGTH, n2k_node_send(&node, 126983, 255, 2, d, 0));
  TEST_ASSERT_EQUAL(N2K_SEND_BAD_LENGTH, n2k_node_send(&node, 126983, 255, 2, d, 224));
  TEST_ASSERT_EQUAL(N2K_SEND_BAD_LENGTH, n2k_node_send(&node, 127488, 255, 2, d, 9));
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_INT(0, nframes);
}

static void test_tx_busy_resumes_mid_message(void) {
  make_ready();
  uint8_t d[40];
  for (int i = 0; i < 40; i++) d[i] = (uint8_t)(i + 1);
  n2k_node_send(&node, 126983, 255, 2, d, 40);
  tx_budget = 2;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_INT(2, nframes);
  tx_budget = -1;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_INT(6, nframes);
  reassemble(0);
  TEST_ASSERT_EQUAL_INT(1, got_count);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(d, got_data, 40);
}

static void test_message_dropped_after_5_failed_ticks(void) {
  make_ready();
  uint8_t a[1] = {0xAA}, b[1] = {0xBB};
  n2k_node_send(&node, 127488, 255, 2, a, 1);
  n2k_node_send(&node, 127488, 255, 2, b, 1);
  tx_budget = 0;
  for (int i = 0; i < 4; i++) n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_UINT32(0, n2k_node_dropped(&node));
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_UINT32(1, n2k_node_dropped(&node));
  tx_budget = -1;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_INT(1, nframes);
  TEST_ASSERT_EQUAL_HEX8(0xBB, frames[0].data[0]);
}

static void test_bus_reset_drops_queue(void) {
  make_ready();
  uint8_t d[1] = {1};
  n2k_node_send(&node, 127488, 255, 2, d, 1);
  n2k_node_send(&node, 127488, 255, 2, d, 1);
  n2k_node_bus_reset(&node);
  TEST_ASSERT_EQUAL_UINT32(2, n2k_node_dropped(&node));
  TEST_ASSERT_EQUAL(N2K_SEND_NOT_READY, n2k_node_send(&node, 127488, 255, 2, d, 1));
  nframes = 0;
  now += 250;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL(N2K_NODE_READY, n2k_node_state(&node));
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_INT(0, nframes);  // the dropped messages stay dropped
}

static void test_iso_request_address_claim(void) {
  make_ready();
  request(0x55, 0x40, 60928, 3);
  TEST_ASSERT_EQUAL_INT(1, nframes);
  TEST_ASSERT_EQUAL_HEX32(0x18EEFF40, frames[0].id);
  nframes = 0;
  request(0x55, 255, 60928, 3);
  TEST_ASSERT_EQUAL_INT(1, nframes);
  nframes = 0;
  request(0x55, 0x41, 60928, 3);  // addressed to another node
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_INT(0, nframes);
}

static void test_iso_request_product_info(void) {
  make_ready();
  request(0x55, 0x40, 126996, 3);
  n2k_node_tick(&node);
  reassemble(0);
  TEST_ASSERT_EQUAL_INT(1, got_count);
  TEST_ASSERT_EQUAL_UINT32(126996, got.pgn);
  TEST_ASSERT_EQUAL_UINT16(134, got.len);
  TEST_ASSERT_EQUAL_UINT8(0x40, got.src);
  TEST_ASSERT_EQUAL_HEX8(2100 & 0xFF, got_data[0]);
  TEST_ASSERT_EQUAL_HEX8(2100 >> 8, got_data[1]);
  TEST_ASSERT_EQUAL_HEX8(0x34, got_data[2]);
  TEST_ASSERT_EQUAL_HEX8(0x12, got_data[3]);
  TEST_ASSERT_EQUAL_HEX8('A', got_data[4]);
  TEST_ASSERT_EQUAL_HEX8(0x00, got_data[7]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, got_data[8]);
  TEST_ASSERT_EQUAL_HEX8(7, got_data[132]);
  TEST_ASSERT_EQUAL_HEX8(3, got_data[133]);
}

static void test_iso_request_unsupported_ignored(void) {
  make_ready();
  request(0x55, 0x40, 127488, 3);   // a PGN we do not serve
  request(0x55, 0x40, 126996, 2);   // truncated request
  request(0x55, 0x41, 126996, 3);   // for another node
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_INT(0, nframes);
  make_node(4);
  n2k_node_start(&node);            // still CLAIMING
  nframes = 0;
  request(0x55, 0x40, 126996, 3);
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_INT(0, nframes);
}

static void test_lock_called_around_queue(void) {
  ops.lock = fake_lock;
  ops.unlock = fake_unlock;
  make_node(4);
  make_ready();
  uint8_t d[1] = {1};
  locks = unlocks = 0;
  n2k_node_send(&node, 127488, 255, 2, d, 1);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(1, locks);
  TEST_ASSERT_EQUAL_INT(locks, unlocks);
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_INT(locks, unlocks);
}

static void test_address_loss_mid_message_never_resumes_from_old_address(void) {
  make_ready();
  uint8_t d[100];
  for (int i = 0; i < 100; i++) d[i] = (uint8_t)i;
  n2k_node_send(&node, 126983, 255, 2, d, 100);
  tx_budget = 3;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_INT(3, nframes);
  tx_budget = -1;
  uint8_t other[8];
  uint64_t lower = 1;  // a NAME lower than ours
  for (int i = 0; i < 8; i++) other[i] = (uint8_t)(lower >> (8 * i));
  n2k_msg_t m = {60928, 0x40, 255, 6, 8, other};
  n2k_node_on_msg(&node, &m);
  int before = nframes;
  now += 300;  // a late tick must not resume the old message
  n2k_node_tick(&node);
  for (int i = before; i < nframes; i++) TEST_ASSERT_NOT_EQUAL(0x40, frames[i].id & 0xFF);
  TEST_ASSERT_EQUAL_UINT32(1, n2k_node_dropped(&node));
}

static void test_bus_reset_takes_the_lock(void) {
  ops.lock = fake_lock;
  ops.unlock = fake_unlock;
  make_node(4);
  make_ready();
  locks = unlocks = 0;
  n2k_node_bus_reset(&node);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(1, locks);
  TEST_ASSERT_EQUAL_INT(locks, unlocks);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_send_before_ready_refused);
  RUN_TEST(test_send_single_frame);
  RUN_TEST(test_send_fast_packet_in_order);
  RUN_TEST(test_two_messages_one_per_tick);
  RUN_TEST(test_queue_full);
  RUN_TEST(test_bad_lengths);
  RUN_TEST(test_tx_busy_resumes_mid_message);
  RUN_TEST(test_message_dropped_after_5_failed_ticks);
  RUN_TEST(test_bus_reset_drops_queue);
  RUN_TEST(test_iso_request_address_claim);
  RUN_TEST(test_iso_request_product_info);
  RUN_TEST(test_iso_request_unsupported_ignored);
  RUN_TEST(test_lock_called_around_queue);
  RUN_TEST(test_address_loss_mid_message_never_resumes_from_old_address);
  RUN_TEST(test_bus_reset_takes_the_lock);
  return UNITY_END();
}
