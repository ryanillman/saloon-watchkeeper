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
