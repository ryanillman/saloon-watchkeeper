#include <string.h>
#include "n2k.h"

void n2k_rx_init(n2k_rx_t *rx, n2k_fp_slot_t *slots, size_t count, n2k_msg_cb cb, void *ctx) {
  rx->slots = slots;
  rx->count = count;
  rx->cb = cb;
  rx->ctx = ctx;
  for (size_t i = 0; i < count; i++) slots[i].used = false;
}

static bool stale(const n2k_fp_slot_t *s, uint32_t now_ms) {
  return (uint32_t)(now_ms - s->last_ms) > N2K_FP_TIMEOUT_MS;  // unsigned: survives the 49-day wrap
}

static n2k_fp_slot_t *find(n2k_rx_t *rx, uint32_t pgn, uint8_t src) {
  for (size_t i = 0; i < rx->count; i++)
    if (rx->slots[i].used && rx->slots[i].pgn == pgn && rx->slots[i].src == src) return &rx->slots[i];
  return NULL;
}

static n2k_fp_slot_t *take(n2k_rx_t *rx, uint32_t now_ms) {
  for (size_t i = 0; i < rx->count; i++)
    if (!rx->slots[i].used || stale(&rx->slots[i], now_ms)) return &rx->slots[i];
  return NULL;
}

static void deliver(n2k_rx_t *rx, const n2k_id_t *id, const uint8_t *data, uint16_t len) {
  n2k_msg_t m = {id->pgn, id->src, id->dst, id->prio, len, data};
  if (rx->cb) rx->cb(&m, rx->ctx);
}

// Copy up to `avail` bytes into the slot, never past its total. Delivers and frees when complete.
static void append(n2k_rx_t *rx, n2k_fp_slot_t *s, const n2k_id_t *id, const uint8_t *d, size_t avail) {
  size_t room = s->total - s->got;
  size_t n = avail < room ? avail : room;
  memcpy(s->buf + s->got, d, n);
  s->got += (uint16_t)n;
  if (s->got >= s->total) {
    s->used = false;
    deliver(rx, id, s->buf, s->total);
  }
}

void n2k_feed_frame(n2k_rx_t *rx, uint32_t can_id, const uint8_t *data, uint8_t len, uint32_t now_ms) {
  n2k_id_t id;
  n2k_id_unpack(can_id, &id);
  if (len > 8) len = 8;
  if (!n2k_pgn_is_fast(id.pgn)) {
    deliver(rx, &id, data, len);
    return;
  }
  if (len < 1) return;
  uint8_t seq = data[0] >> 5, counter = data[0] & 0x1F;
  n2k_fp_slot_t *s = find(rx, id.pgn, id.src);
  if (counter == 0) {
    if (len < 2 || data[1] == 0 || data[1] > N2K_FP_MAX) return;
    if (!s) s = take(rx, now_ms);
    if (!s) return;  // all slots busy with live messages
    s->used = true;
    s->pgn = id.pgn;
    s->src = id.src;
    s->seq = seq;
    s->next_frame = 1;
    s->total = data[1];
    s->got = 0;
    s->last_ms = now_ms;
    append(rx, s, &id, data + 2, (size_t)len - 2 < 6 ? (size_t)len - 2 : 6);
    return;
  }
  if (!s) return;  // continuation with no open message
  if (stale(s, now_ms)) {
    s->used = false;
    return;
  }
  if (seq != s->seq) return;
  if (counter == s->next_frame - 1) return;  // duplicate
  if (counter != s->next_frame) {
    s->used = false;  // a frame was lost or reordered
    return;
  }
  s->next_frame++;
  s->last_ms = now_ms;
  append(rx, s, &id, data + 1, (size_t)len - 1 < 7 ? (size_t)len - 1 : 7);
}

void n2k_feed_pgn(n2k_rx_t *rx, uint32_t pgn, uint8_t src, const uint8_t *data, uint16_t len, uint32_t now_ms) {
  (void)now_ms;
  n2k_id_t id = {6, pgn, src, N2K_BROADCAST};
  deliver(rx, &id, data, len);
}
