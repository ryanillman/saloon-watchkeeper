#include "n2k.h"

uint32_t n2k_id_pack(uint8_t prio, uint32_t pgn, uint8_t src, uint8_t dst) {
  uint32_t dp = (pgn >> 16) & 1;
  uint32_t pf = (pgn >> 8) & 0xFF;
  uint32_t ps = pf < 240 ? dst : (pgn & 0xFF);  // PDU1 carries the destination, PDU2 the PGN's low byte
  return ((uint32_t)(prio & 7) << 26) | (dp << 24) | (pf << 16) | (ps << 8) | src;
}

void n2k_id_unpack(uint32_t id, n2k_id_t *out) {
  uint32_t dp = (id >> 24) & 1;
  uint32_t pf = (id >> 16) & 0xFF;
  uint32_t ps = (id >> 8) & 0xFF;
  out->prio = (id >> 26) & 7;
  out->src = id & 0xFF;
  if (pf < 240) {
    out->pgn = (dp << 16) | (pf << 8);
    out->dst = (uint8_t)ps;
  } else {
    out->pgn = (dp << 16) | (pf << 8) | ps;
    out->dst = N2K_BROADCAST;
  }
}

// The PGNs this library's tests and the controller's alerts need, plus 127489.
// The decoder plan adds the rest (127505 and others) when it needs them.
static const uint32_t fast_pgns[] = {
  126208,  // NMEA command/request/acknowledge group function  // verify: canboat
  126720,  // proprietary fast-packet
  126983, 126984, 126985, 126986, 126987, 126988,  // alerts
  126996,  // product information
  126998,  // configuration information
  127489,  // engine parameters, dynamic
  127506,  // DC detailed status  // verify: canboat
  129029,  // GNSS position data  // verify: canboat
};

bool n2k_pgn_is_fast(uint32_t pgn) {
  if (pgn >= 130816 && pgn <= 131071) return true;  // proprietary fast-packet range
  for (size_t i = 0; i < sizeof fast_pgns / sizeof *fast_pgns; i++)
    if (fast_pgns[i] == pgn) return true;
  return false;
}
