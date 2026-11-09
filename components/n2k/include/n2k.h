// NMEA 2000 frame codec: CAN ID packing, fast-packet receive and transmit.
// Pure C, libc only, no allocation: callers hand in all storage.
// Every PGN number and byte layout here is from memory.  // verify: canboat
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define N2K_BROADCAST 255
#define N2K_FP_MAX 223  // fast-packet payload limit: 6 + 31 * 7

typedef struct {
  uint32_t id;  // 29-bit CAN identifier
  uint8_t len;
  uint8_t data[8];
} n2k_frame_t;

typedef struct {
  uint8_t prio;
  uint32_t pgn;
  uint8_t src;
  uint8_t dst;  // N2K_BROADCAST for broadcast and for every PDU2 PGN
} n2k_id_t;

uint32_t n2k_id_pack(uint8_t prio, uint32_t pgn, uint8_t src, uint8_t dst);
void n2k_id_unpack(uint32_t id, n2k_id_t *out);

// True when the PGN is sent as fast-packet (multi-frame) even if a given
// payload would fit in one frame.
bool n2k_pgn_is_fast(uint32_t pgn);

// ---- Receive ----------------------------------------------------------------

typedef struct {
  uint32_t pgn;
  uint8_t src, dst, prio;
  uint16_t len;
  const uint8_t *data;  // valid only during the callback
} n2k_msg_t;

typedef void (*n2k_msg_cb)(const n2k_msg_t *msg, void *ctx);

// One in-progress fast-packet message, keyed by (pgn, src). The caller owns the
// array (about 245 bytes each), so it can live in PSRAM.
typedef struct {
  bool used;
  uint32_t pgn;
  uint8_t src, seq, next_frame;
  uint16_t total, got;
  uint32_t last_ms;
  uint8_t buf[N2K_FP_MAX];
} n2k_fp_slot_t;

typedef struct {
  n2k_fp_slot_t *slots;
  size_t count;
  n2k_msg_cb cb;
  void *ctx;
} n2k_rx_t;

#define N2K_FP_TIMEOUT_MS 750

void n2k_rx_init(n2k_rx_t *rx, n2k_fp_slot_t *slots, size_t count, n2k_msg_cb cb, void *ctx);

// Raw 29-bit frames (CAN, and gateways that send frames).
void n2k_feed_frame(n2k_rx_t *rx, uint32_t can_id, const uint8_t *data, uint8_t len, uint32_t now_ms);

// Whole PGNs (gateways that send reassembled messages). Delivered as broadcast, priority 6.
void n2k_feed_pgn(n2k_rx_t *rx, uint32_t pgn, uint8_t src, const uint8_t *data, uint16_t len, uint32_t now_ms);
