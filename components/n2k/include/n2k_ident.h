// Who is on the bus: address-claim NAME (60928), product information (126996) and the ISO request that asks for it.
// Pure C over n2k_msg_t. Layouts are from memory and unverified except where a test cites a capture.  // verify: canboat
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "n2k.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint64_t name; } n2k_claim_t;
// PGN 60928: exactly 8 bytes, the NAME little-endian. False for any other PGN or length.
bool n2k_decode_claim(const n2k_msg_t *m, n2k_claim_t *out);

typedef struct {
  char model_id[32], serial[32];
  uint16_t product_code;
} n2k_prodinfo_t;
// PGN 126996 (134 bytes): model id and serial, NUL/'@'/space padding trimmed.
bool n2k_decode_prodinfo(const n2k_msg_t *m, n2k_prodinfo_t *out);

// ISO Request (PGN 59904) payload asking for `pgn`: 3 bytes little-endian. False when pgn does not fit 24 bits.
bool n2k_encode_iso_request(uint32_t pgn, uint8_t *data, uint16_t *len);

#ifdef __cplusplus
}
#endif
