#include <string.h>
#include "n2k_node.h"

#define PGN_ADDRESS_CLAIM 60928

uint64_t n2k_name_pack(const n2k_name_fields_t *f) {
  return (uint64_t)(f->unique_number & 0x1FFFFF) | (uint64_t)(f->manufacturer & 0x7FF) << 21 |
         (uint64_t)f->device_instance << 32 | (uint64_t)f->function << 40 |
         (uint64_t)(f->device_class & 0x7F) << 49 | (uint64_t)(f->system_instance & 0xF) << 56 |
         (uint64_t)(f->industry_group & 7) << 60 | (uint64_t)1 << 63;
}

void n2k_node_init(n2k_node_t *n, const n2k_node_config_t *cfg, const n2k_node_ops_t *ops,
                   n2k_qent_t *queue, size_t qcount) {
  memset(n, 0, sizeof *n);
  n->cfg = *cfg;
  n->ops = *ops;
  n->name = n2k_name_pack(&cfg->name);
  n->state = N2K_NODE_IDLE;
  n->address = cfg->preferred_address;
  n->queue = queue;
  n->qcount = qcount;
}

n2k_node_state_t n2k_node_state(const n2k_node_t *n) { return n->state; }
uint8_t n2k_node_address(const n2k_node_t *n) { return n->address; }

static bool seen(const n2k_node_t *n, unsigned a) { return n->seen[a >> 3] >> (a & 7) & 1; }

static void send_claim(n2k_node_t *n) {
  n2k_frame_t f;
  f.id = n2k_id_pack(6, PGN_ADDRESS_CLAIM, n->address, N2K_BROADCAST);
  f.len = 8;
  for (int i = 0; i < 8; i++) f.data[i] = (uint8_t)(n->name >> (8 * i));
  if (!n->ops.tx_frame(&f, n->ops.ctx)) {
    n->claim_pending = true;  // retried on the next tick
    return;
  }
  n->claim_pending = false;
  if (n->claim_timed) n->claim_sent_ms = n->ops.now_ms(n->ops.ctx);
}

// Claim from the current address; `timed` restarts the 250 ms wait when the frame goes out.
static void claim(n2k_node_t *n, bool timed) {
  n->claim_timed = timed;
  send_claim(n);
}

void n2k_node_start(n2k_node_t *n) {
  n->state = N2K_NODE_CLAIMING;
  claim(n, true);
}

void n2k_node_bus_reset(n2k_node_t *n) {
  n->qhead = n->qlen = 0;
  n->tx_active = false;
  n->stall = 0;
  memset(n->seen, 0, sizeof n->seen);
  n->address = n->cfg.preferred_address;
  n->state = N2K_NODE_CLAIMING;
  claim(n, true);
}

static void lose_address(n2k_node_t *n) {
  for (unsigned a = 128; a <= 247; a++) {
    if (!seen(n, a)) {
      n->address = (uint8_t)a;
      n->state = N2K_NODE_CLAIMING;
      claim(n, true);
      return;
    }
  }
  n->address = N2K_NODE_ADDR_NONE;
  n->state = N2K_NODE_CANNOT_CLAIM;
  claim(n, false);
}

void n2k_node_on_msg(n2k_node_t *n, const n2k_msg_t *m) {
  if (m->pgn == PGN_ADDRESS_CLAIM && m->len == 8) {
    uint64_t other = 0;
    for (int i = 0; i < 8; i++) other |= (uint64_t)m->data[i] << (8 * i);
    n->seen[m->src >> 3] |= (uint8_t)(1u << (m->src & 7));
    if ((n->state == N2K_NODE_CLAIMING || n->state == N2K_NODE_READY) && m->src == n->address) {
      if (other <= n->name)
        lose_address(n);
      else
        claim(n, false);  // we outrank them: say so again, keep the schedule
    }
  }
}

void n2k_node_tick(n2k_node_t *n) {
  if (n->state == N2K_NODE_IDLE) return;
  if (n->claim_pending) {
    send_claim(n);
    if (n->claim_pending) return;
  }
  if (n->state == N2K_NODE_CLAIMING &&
      (uint32_t)(n->ops.now_ms(n->ops.ctx) - n->claim_sent_ms) >= N2K_NODE_CLAIM_WAIT_MS)
    n->state = N2K_NODE_READY;
}
