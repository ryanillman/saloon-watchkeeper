#include <string.h>
#include "n2k.h"

bool n2k_tx_begin(n2k_tx_t *tx, uint8_t prio, uint32_t pgn, uint8_t src, uint8_t dst,
                  const uint8_t *data, uint16_t len, uint8_t seq) {
  bool fast = n2k_pgn_is_fast(pgn);
  if (len == 0 || len > N2K_FP_MAX || (!fast && len > 8)) return false;
  tx->id = n2k_id_pack(prio, pgn, src, dst);
  tx->data = data;
  tx->len = len;
  tx->off = 0;
  tx->seq = seq & 7;
  tx->frame = 0;
  tx->fast = fast;
  tx->done = false;
  return true;
}

bool n2k_tx_next(n2k_tx_t *tx, n2k_frame_t *out) {
  if (tx->done) return false;
  out->id = tx->id;
  out->len = 8;
  memset(out->data, 0xFF, 8);
  size_t room, at;
  if (!tx->fast) {
    at = 0;
    room = 8;
  } else if (tx->frame == 0) {
    out->data[0] = (uint8_t)(tx->seq << 5);
    out->data[1] = (uint8_t)tx->len;
    at = 2;
    room = 6;
  } else {
    out->data[0] = (uint8_t)(tx->seq << 5 | tx->frame);
    at = 1;
    room = 7;
  }
  size_t left = tx->len - tx->off;
  size_t n = left < room ? left : room;
  memcpy(out->data + at, tx->data + tx->off, n);
  tx->off += (uint16_t)n;
  tx->frame++;
  if (tx->off >= tx->len) tx->done = true;
  return true;
}

uint8_t n2k_seq_next(n2k_seq_t *s, uint32_t pgn, uint8_t dst) {
  uint8_t i;
  for (i = 0; i < s->used; i++)
    if (s->pgn[i] == pgn && s->dst[i] == dst) break;
  if (i == s->used) {
    if (s->used < 8) {
      i = s->used++;
    } else {
      i = s->victim;
      s->victim = (uint8_t)((s->victim + 1) & 7);
    }
    s->pgn[i] = pgn;
    s->dst[i] = dst;
    s->seq[i] = 0;
  }
  uint8_t v = s->seq[i];
  s->seq[i] = (uint8_t)((v + 1) & 7);
  return v;
}
