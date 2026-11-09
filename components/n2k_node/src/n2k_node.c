#include <string.h>
#include "n2k_node.h"

#define PGN_ADDRESS_CLAIM 60928
#define PGN_ISO_REQUEST 59904
#define PGN_PRODUCT_INFO 126996
#define PRODUCT_INFO_LEN 134

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

static void lock(n2k_node_t *n) { if (n->ops.lock) n->ops.lock(n->ops.ctx); }
static void unlock(n2k_node_t *n) { if (n->ops.unlock) n->ops.unlock(n->ops.ctx); }

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
  if (timed || !n->claim_pending) n->claim_timed = timed;  // a defence never cancels a pending timed claim
  send_claim(n);
}

void n2k_node_start(n2k_node_t *n) {
  n->state = N2K_NODE_CLAIMING;
  claim(n, true);
}

void n2k_node_bus_reset(n2k_node_t *n) {
  lock(n);  // other tasks may be inside n2k_node_send
  n->dropped += (uint32_t)n->qlen;
  n->qhead = n->qlen = 0;
  n->tx_active = false;
  n->stall = 0;
  memset(n->seen, 0, sizeof n->seen);
  n->address = n->cfg.preferred_address;
  n->state = N2K_NODE_CLAIMING;
  unlock(n);
  claim(n, true);
}

// Drop the message being sent (if any): what is on the wire came from an address we no longer hold.
static void abort_tx(n2k_node_t *n) {
  if (!n->tx_active) return;
  n->tx_active = false;
  n->stall = 0;
  lock(n);
  n->qhead = (n->qhead + 1) % n->qcount;
  n->qlen--;
  unlock(n);
  n->dropped++;
}

static void lose_address(n2k_node_t *n) {
  abort_tx(n);
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

uint32_t n2k_node_dropped(const n2k_node_t *n) { return n->dropped; }

static n2k_send_t enqueue(n2k_node_t *n, uint32_t pgn, uint8_t dst, uint8_t prio, const uint8_t *data, uint16_t len) {
  if (len == 0 || len > N2K_FP_MAX || (len > 8 && !n2k_pgn_is_fast(pgn))) return N2K_SEND_BAD_LENGTH;
  lock(n);
  n2k_send_t r = N2K_SEND_OK;
  if (n->state != N2K_NODE_READY) {
    r = N2K_SEND_NOT_READY;
  } else if (n->qlen >= n->qcount) {
    r = N2K_SEND_QUEUE_FULL;
  } else {
    n2k_qent_t *e = &n->queue[(n->qhead + n->qlen) % n->qcount];
    e->pgn = pgn;
    e->dst = dst;
    e->prio = prio;
    e->len = len;
    memcpy(e->data, data, len);
    n->qlen++;
  }
  unlock(n);
  return r;
}

n2k_send_t n2k_node_send(n2k_node_t *n, uint32_t pgn, uint8_t dst, uint8_t prio, const uint8_t *data, uint16_t len) {
  return enqueue(n, pgn, dst, prio, data, len);
}

static void put_str(uint8_t *out, const char *s) {  // 32 bytes: text, NUL, then 0xFF
  size_t l = 0;
  while (l < 31 && s[l]) l++;
  memset(out, 0xFF, 32);
  memcpy(out, s, l);
  out[l] = 0;
}

static void reply_product_info(n2k_node_t *n, uint8_t to) {
  const n2k_product_t *p = &n->cfg.product;
  uint8_t b[PRODUCT_INFO_LEN];
  b[0] = (uint8_t)p->n2k_db_version;
  b[1] = (uint8_t)(p->n2k_db_version >> 8);
  b[2] = (uint8_t)p->product_code;
  b[3] = (uint8_t)(p->product_code >> 8);
  put_str(b + 4, p->model_id);
  put_str(b + 36, p->sw_version);
  put_str(b + 68, p->model_version);
  put_str(b + 100, p->serial);
  b[132] = p->certification;
  b[133] = p->load_equivalency;
  enqueue(n, PGN_PRODUCT_INFO, to, 6, b, PRODUCT_INFO_LEN);
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
  } else if (m->pgn == PGN_ISO_REQUEST && m->len >= 3 && n->state == N2K_NODE_READY &&
             (m->dst == n->address || m->dst == N2K_BROADCAST)) {
    uint32_t want = m->data[0] | (uint32_t)m->data[1] << 8 | (uint32_t)m->data[2] << 16;
    if (want == PGN_ADDRESS_CLAIM)
      claim(n, false);
    else if (want == PGN_PRODUCT_INFO)
      reply_product_info(n, m->src);
    // anything else: ignored (v1 sends no NAK)
  }
}

// Sends at most one complete queued message; resumes a partly sent one.
static void send_one(n2k_node_t *n) {
  if (!n->tx_active) {
    if (n->qlen == 0) return;
    n2k_qent_t *e = &n->queue[n->qhead];
    n2k_tx_begin(&n->tx, e->prio, e->pgn, n->address, e->dst, e->data, e->len, n2k_seq_next(&n->seq, e->pgn, e->dst));
    n->tx_active = true;
  }
  n2k_frame_t f;
  for (;;) {
    n2k_tx_t before = n->tx;
    if (!n2k_tx_next(&n->tx, &f)) break;
    if (!n->ops.tx_frame(&f, n->ops.ctx)) {
      n->tx = before;
      if (++n->stall >= 5) {
        n->tx_active = false;
        n->stall = 0;
        n->dropped++;
        lock(n);
        n->qhead = (n->qhead + 1) % n->qcount;
        n->qlen--;
        unlock(n);
      }
      return;
    }
    n->stall = 0;
    if (n->tx.done) break;
  }
  n->tx_active = false;
  lock(n);
  n->qhead = (n->qhead + 1) % n->qcount;
  n->qlen--;
  unlock(n);
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

  if (n->state != N2K_NODE_READY) return;
  send_one(n);
}
