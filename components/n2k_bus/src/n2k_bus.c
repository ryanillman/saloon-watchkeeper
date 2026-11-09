#include "n2k_bus.h"

static void on_msg(const n2k_msg_t *m, void *ctx) {
  n2k_bus_t *b = ctx;
  if (b->node) n2k_node_on_msg(b->node, m);
  if (b->app_cb) b->app_cb(m, b->app_ctx);
}

void n2k_bus_init(n2k_bus_t *b, const n2k_can_ops_t *can, n2k_node_t *node, n2k_fp_slot_t *slots,
                  size_t nslots, n2k_msg_cb app_cb, void *app_ctx, uint32_t (*now_ms)(void *ctx),
                  void *now_ctx) {
  b->can = *can;
  b->node = node;
  b->now_ms = now_ms;
  b->now_ctx = now_ctx;
  b->app_cb = app_cb;
  b->app_ctx = app_ctx;
  b->stats = (n2k_bus_stats_t){0};
  b->was_off = false;
  n2k_rx_init(&b->rx, slots, nslots, on_msg, b);
}

bool n2k_bus_tx_frame(const n2k_frame_t *f, void *bus) {
  n2k_bus_t *b = bus;
  if (b->can.send(f, b->can.ctx)) {
    b->stats.tx_frames++;
    return true;
  }
  b->stats.tx_refused++;
  return false;
}

void n2k_bus_step(n2k_bus_t *b, uint32_t max_frames) {
  if (b->can.state(b->can.ctx) == N2K_CAN_BUS_OFF) {
    if (!b->was_off) {
      b->was_off = true;
      b->stats.bus_off_events++;
    }
    b->can.recover(b->can.ctx);
    return;
  }
  if (b->was_off) {
    b->was_off = false;
    if (b->node) n2k_node_bus_reset(b->node);  // drops what was queued before the outage
  }
  n2k_frame_t f;
  for (uint32_t i = 0; i < max_frames && b->can.recv(&f, b->can.ctx); i++) {
    b->stats.rx_frames++;
    n2k_feed_frame(&b->rx, f.id, f.data, f.len, b->now_ms(b->now_ctx));
  }
  if (b->node) n2k_node_tick(b->node);
}

const n2k_bus_stats_t *n2k_bus_stats(const n2k_bus_t *b) { return &b->stats; }
