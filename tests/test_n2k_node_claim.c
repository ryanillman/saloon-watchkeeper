#include <string.h>
#include "unity.h"
#include "n2k_node.h"

#define MAXF 64
static n2k_frame_t frames[MAXF];
static int nframes;
static bool tx_ok;
static uint32_t now;
static n2k_node_t node;
static n2k_qent_t queue[4];
static n2k_node_config_t cfg;

static bool fake_tx(const n2k_frame_t *f, void *ctx) {
  (void)ctx;
  if (!tx_ok) return false;
  if (nframes < MAXF) frames[nframes++] = *f;
  return true;
}
static uint32_t fake_now(void *ctx) { (void)ctx; return now; }

static const n2k_node_ops_t ops = {fake_tx, fake_now, NULL, NULL, NULL};

static uint64_t name_with_unique(uint32_t u) {
  n2k_name_fields_t f = cfg.name;
  f.unique_number = u;
  return n2k_name_pack(&f);
}
static uint64_t our_name(void) { return n2k_name_pack(&cfg.name); }

void setUp(void) {
  memset(&cfg, 0, sizeof cfg);
  cfg.name = (n2k_name_fields_t){.unique_number = 100, .manufacturer = 999, .function = 130,
                                 .device_class = 25, .industry_group = 4};
  cfg.preferred_address = 0x40;
  nframes = 0;
  tx_ok = true;
  now = 1000;
  n2k_node_init(&node, &cfg, &ops, queue, 4);
}
void tearDown(void) {}

static void deliver_claim(uint8_t src, uint64_t name) {
  uint8_t d[8];
  for (int i = 0; i < 8; i++) d[i] = (uint8_t)(name >> (8 * i));
  n2k_msg_t m = {60928, src, 255, 6, 8, d};
  n2k_node_on_msg(&node, &m);
}
static uint64_t frame_name(const n2k_frame_t *f) {
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v |= (uint64_t)f->data[i] << (8 * i);
  return v;
}
static uint8_t frame_src(const n2k_frame_t *f) { return f->id & 0xFF; }

static void test_name_pack_orders_fields(void) {
  n2k_name_fields_t f;
  memset(&f, 0, sizeof f);
  f.industry_group = 4;
  TEST_ASSERT_TRUE(n2k_name_pack(&f) == ((1ull << 63) | (4ull << 60)));
  memset(&f, 0, sizeof f);
  f.unique_number = 0x1FFFFF;
  TEST_ASSERT_TRUE(n2k_name_pack(&f) == ((1ull << 63) | 0x1FFFFFull));
  f.unique_number = 0;
  uint64_t a = n2k_name_pack(&f);
  f.function = 1;
  TEST_ASSERT_TRUE(n2k_name_pack(&f) - a == (1ull << 40));
}

static void test_start_sends_claim_and_enters_claiming(void) {
  n2k_node_start(&node);
  TEST_ASSERT_EQUAL_INT(1, nframes);
  TEST_ASSERT_EQUAL_HEX32(0x18EEFF40, frames[0].id);
  TEST_ASSERT_EQUAL_UINT8(8, frames[0].len);
  TEST_ASSERT_TRUE(frame_name(&frames[0]) == our_name());
  TEST_ASSERT_EQUAL(N2K_NODE_CLAIMING, n2k_node_state(&node));
}

static void test_ready_after_250ms(void) {
  n2k_node_start(&node);
  now += 249;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL(N2K_NODE_CLAIMING, n2k_node_state(&node));
  now += 1;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL(N2K_NODE_READY, n2k_node_state(&node));
  TEST_ASSERT_EQUAL_UINT8(0x40, n2k_node_address(&node));
}

static void test_lower_name_contender_moves_us(void) {
  n2k_node_start(&node);
  nframes = 0;
  now += 100;
  deliver_claim(0x40, name_with_unique(1));
  TEST_ASSERT_EQUAL_INT(1, nframes);
  TEST_ASSERT_EQUAL_UINT8(128, frame_src(&frames[0]));
  TEST_ASSERT_EQUAL_HEX32(0x18EEFF80, frames[0].id);
  TEST_ASSERT_EQUAL(N2K_NODE_CLAIMING, n2k_node_state(&node));
  now += 249;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL(N2K_NODE_CLAIMING, n2k_node_state(&node));  // the wait restarted at the new claim
  now += 1;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL(N2K_NODE_READY, n2k_node_state(&node));
  TEST_ASSERT_EQUAL_UINT8(128, n2k_node_address(&node));
}

static void test_higher_name_contender_keeps_address(void) {
  n2k_node_start(&node);
  nframes = 0;
  now += 100;
  deliver_claim(0x40, name_with_unique(0x1FFFFF));
  TEST_ASSERT_EQUAL_INT(1, nframes);
  TEST_ASSERT_EQUAL_UINT8(0x40, frame_src(&frames[0]));
  TEST_ASSERT_TRUE(frame_name(&frames[0]) == our_name());
  now += 150;  // 250 ms after the original claim, not after the resend
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL(N2K_NODE_READY, n2k_node_state(&node));
  TEST_ASSERT_EQUAL_UINT8(0x40, n2k_node_address(&node));
}

static void test_lower_name_contender_while_ready(void) {
  n2k_node_start(&node);
  now += 250;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL(N2K_NODE_READY, n2k_node_state(&node));
  nframes = 0;
  deliver_claim(0x40, name_with_unique(1));
  TEST_ASSERT_NOT_EQUAL(N2K_NODE_READY, n2k_node_state(&node));
  now += 1000;
  n2k_node_tick(&node);
  for (int i = 0; i < nframes; i++) TEST_ASSERT_NOT_EQUAL(0x40, frame_src(&frames[i]));
  TEST_ASSERT_EQUAL_UINT8(128, n2k_node_address(&node));
}

static void test_no_free_address_cannot_claim(void) {
  n2k_node_start(&node);
  for (int a = 128; a <= 247; a++) deliver_claim((uint8_t)a, name_with_unique(1));
  nframes = 0;
  deliver_claim(0x40, name_with_unique(1));
  TEST_ASSERT_EQUAL(N2K_NODE_CANNOT_CLAIM, n2k_node_state(&node));
  TEST_ASSERT_EQUAL_UINT8(254, n2k_node_address(&node));
  TEST_ASSERT_EQUAL_INT(1, nframes);
  TEST_ASSERT_EQUAL_UINT8(254, frame_src(&frames[0]));
  now += 5000;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_INT(1, nframes);
  TEST_ASSERT_EQUAL(N2K_NODE_CANNOT_CLAIM, n2k_node_state(&node));
}

static void test_failed_tx_retried_on_tick(void) {
  tx_ok = false;
  n2k_node_start(&node);
  TEST_ASSERT_EQUAL_INT(0, nframes);
  now += 400;
  tx_ok = true;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL_INT(1, nframes);
  TEST_ASSERT_EQUAL(N2K_NODE_CLAIMING, n2k_node_state(&node));  // the 250 ms count from the send
  now += 250;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL(N2K_NODE_READY, n2k_node_state(&node));
}

static void test_claim_timer_survives_clock_wrap(void) {
  now = 0xFFFFFF80u;
  n2k_node_start(&node);
  now = 0x00000079u;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL(N2K_NODE_CLAIMING, n2k_node_state(&node));
  now = 0x0000007Au;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL(N2K_NODE_READY, n2k_node_state(&node));
}

static void test_bus_reset_restarts_claim(void) {
  n2k_node_start(&node);
  now += 250;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL(N2K_NODE_READY, n2k_node_state(&node));
  nframes = 0;
  n2k_node_bus_reset(&node);
  TEST_ASSERT_EQUAL_INT(1, nframes);
  TEST_ASSERT_EQUAL_HEX32(0x18EEFF40, frames[0].id);
  TEST_ASSERT_EQUAL(N2K_NODE_CLAIMING, n2k_node_state(&node));
}

static void test_defended_claim_keeps_the_wait(void) {
  tx_ok = false;
  n2k_node_start(&node);  // first claim refused: still pending
  now += 100;
  tx_ok = true;
  deliver_claim(0x40, name_with_unique(0x1FFFFF));  // higher NAME: we defend
  TEST_ASSERT_EQUAL_INT(1, nframes);
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL(N2K_NODE_CLAIMING, n2k_node_state(&node));  // not READY on the strength of an unset timestamp
  now += 249;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL(N2K_NODE_CLAIMING, n2k_node_state(&node));
  now += 1;
  n2k_node_tick(&node);
  TEST_ASSERT_EQUAL(N2K_NODE_READY, n2k_node_state(&node));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_name_pack_orders_fields);
  RUN_TEST(test_start_sends_claim_and_enters_claiming);
  RUN_TEST(test_ready_after_250ms);
  RUN_TEST(test_lower_name_contender_moves_us);
  RUN_TEST(test_higher_name_contender_keeps_address);
  RUN_TEST(test_lower_name_contender_while_ready);
  RUN_TEST(test_no_free_address_cannot_claim);
  RUN_TEST(test_failed_tx_retried_on_tick);
  RUN_TEST(test_claim_timer_survives_clock_wrap);
  RUN_TEST(test_bus_reset_restarts_claim);
  RUN_TEST(test_defended_claim_keeps_the_wait);
  return UNITY_END();
}
