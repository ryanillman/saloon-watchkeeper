// Nodes on a simulated bus: frames go to every node but the sender, through
// the real fast-packet receive path and the real node state machine.
#include <string.h>
#include "unity.h"
#include "n2k_node.h"

#define NODES 3
#define BUSMAX 256

typedef struct {
  n2k_node_t node;
  n2k_node_config_t cfg;
  n2k_node_ops_t ops;
  n2k_qent_t queue[4];
  n2k_fp_slot_t slots[4];
  n2k_rx_t rx;
  int index;
  int app_count;         // non-claim messages the application saw
  n2k_msg_t app_last;
  uint8_t app_data[N2K_FP_MAX];
} sim_node_t;

static sim_node_t sim[NODES];
static struct { n2k_frame_t f; int from; } bus[BUSMAX];
static int bus_len;
static uint32_t now;

static bool sim_tx(const n2k_frame_t *f, void *ctx) {
  sim_node_t *s = ctx;
  if (bus_len >= BUSMAX) return false;
  bus[bus_len].f = *f;
  bus[bus_len].from = s->index;
  bus_len++;
  return true;
}
static uint32_t sim_now(void *ctx) { (void)ctx; return now; }

static void sim_msg(const n2k_msg_t *m, void *ctx) {
  sim_node_t *s = ctx;
  n2k_node_on_msg(&s->node, m);
  if (m->pgn != 60928) {
    s->app_last = *m;
    memcpy(s->app_data, m->data, m->len);
    s->app_last.data = s->app_data;
    s->app_count++;
  }
}

static void add_node(int i, uint32_t unique, uint8_t preferred) {
  sim_node_t *s = &sim[i];
  memset(s, 0, sizeof *s);
  s->index = i;
  s->cfg.name = (n2k_name_fields_t){.unique_number = unique, .manufacturer = 999, .function = 130,
                                    .device_class = 25, .industry_group = 4};
  s->cfg.preferred_address = preferred;
  s->cfg.product.n2k_db_version = 2100;
  s->cfg.product.product_code = (uint16_t)(0x1000 + i);
  s->ops = (n2k_node_ops_t){sim_tx, sim_now, NULL, NULL, s};
  n2k_node_init(&s->node, &s->cfg, &s->ops, s->queue, 4);
  n2k_rx_init(&s->rx, s->slots, 4, sim_msg, s);
}

// Deliver everything on the bus (replies may add more), then tick every started node.
static void step(int started_mask) {
  while (bus_len > 0) {
    int n = bus_len;
    bus_len = 0;
    for (int k = 0; k < n; k++)
      for (int i = 0; i < NODES; i++)
        if ((started_mask >> i & 1) && i != bus[k].from) {
          n2k_frame_t *f = &bus[k].f;
          n2k_feed_frame(&sim[i].rx, f->id, f->data, f->len, now);
        }
  }
  for (int i = 0; i < NODES; i++)
    if (started_mask >> i & 1) n2k_node_tick(&sim[i].node);
}
static void run_ms(int ms, int mask) {
  for (int t = 0; t < ms; t += 10) {
    now += 10;
    step(mask);
  }
}

void setUp(void) {
  bus_len = 0;
  now = 5000;
  memset(sim, 0, sizeof sim);
}
void tearDown(void) {}

static void test_two_nodes_same_preferred_address_settle(void) {
  add_node(0, 1, 0x40);
  add_node(1, 2, 0x40);
  n2k_node_start(&sim[0].node);
  n2k_node_start(&sim[1].node);
  run_ms(1000, 3);
  TEST_ASSERT_EQUAL(N2K_NODE_READY, n2k_node_state(&sim[0].node));
  TEST_ASSERT_EQUAL(N2K_NODE_READY, n2k_node_state(&sim[1].node));
  TEST_ASSERT_EQUAL_UINT8(0x40, n2k_node_address(&sim[0].node));  // the lower NAME keeps it
  TEST_ASSERT_NOT_EQUAL(0x40, n2k_node_address(&sim[1].node));
}

static void settle_two(void) {
  add_node(0, 1, 0x40);
  add_node(1, 2, 0x41);
  n2k_node_start(&sim[0].node);
  n2k_node_start(&sim[1].node);
  run_ms(1000, 3);
}

static void test_message_between_nodes(void) {
  settle_two();
  uint8_t d[60];
  for (int i = 0; i < 60; i++) d[i] = (uint8_t)(i * 3);
  TEST_ASSERT_EQUAL(N2K_SEND_OK, n2k_node_send(&sim[0].node, 126983, 255, 2, d, 60));
  run_ms(100, 3);
  TEST_ASSERT_EQUAL_INT(1, sim[1].app_count);
  TEST_ASSERT_EQUAL_UINT32(126983, sim[1].app_last.pgn);
  TEST_ASSERT_EQUAL_UINT16(60, sim[1].app_last.len);
  TEST_ASSERT_EQUAL_UINT8(n2k_node_address(&sim[0].node), sim[1].app_last.src);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(d, sim[1].app_data, 60);
  TEST_ASSERT_EQUAL_INT(0, sim[0].app_count);  // a node does not hear itself
}

static void test_late_joiner_does_not_displace(void) {
  settle_two();
  uint8_t a = n2k_node_address(&sim[0].node), b = n2k_node_address(&sim[1].node);
  add_node(2, 3, 0x40);
  n2k_node_start(&sim[2].node);
  run_ms(1500, 7);
  TEST_ASSERT_EQUAL(N2K_NODE_READY, n2k_node_state(&sim[2].node));
  TEST_ASSERT_EQUAL_UINT8(a, n2k_node_address(&sim[0].node));
  TEST_ASSERT_EQUAL_UINT8(b, n2k_node_address(&sim[1].node));
  uint8_t c = n2k_node_address(&sim[2].node);
  TEST_ASSERT_NOT_EQUAL(a, c);
  TEST_ASSERT_NOT_EQUAL(b, c);
}

static void test_request_round_trip(void) {
  settle_two();
  const uint8_t req[3] = {0x14, 0xF0, 0x01};  // PGN 126996
  TEST_ASSERT_EQUAL(N2K_SEND_OK, n2k_node_send(&sim[0].node, 59904, n2k_node_address(&sim[1].node), 6, req, 3));
  run_ms(100, 3);
  TEST_ASSERT_EQUAL_INT(1, sim[0].app_count);
  TEST_ASSERT_EQUAL_UINT32(126996, sim[0].app_last.pgn);
  TEST_ASSERT_EQUAL_UINT16(134, sim[0].app_last.len);
  TEST_ASSERT_EQUAL_UINT8(n2k_node_address(&sim[1].node), sim[0].app_last.src);
  TEST_ASSERT_EQUAL_HEX8(0x01, sim[0].app_data[2]);  // node 1's product code, 0x1001, little-endian
  TEST_ASSERT_EQUAL_HEX8(0x10, sim[0].app_data[3]);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_two_nodes_same_preferred_address_settle);
  RUN_TEST(test_message_between_nodes);
  RUN_TEST(test_late_joiner_does_not_displace);
  RUN_TEST(test_request_round_trip);
  return UNITY_END();
}
