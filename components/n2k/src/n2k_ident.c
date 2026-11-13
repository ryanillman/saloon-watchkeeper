#include "n2k_ident.h"
#include <string.h>

bool n2k_decode_claim(const n2k_msg_t *m, n2k_claim_t *out) {
  if (m->pgn != 60928 || m->len != 8) return false;
  uint64_t v = 0;
  for (int i = 7; i >= 0; i--) v = v << 8 | m->data[i];
  out->name = v;
  return true;
}

// A fixed-width field padded with NUL, '@' or spaces.
static void text(const uint8_t *src, char *dst) {
  size_t n = 0;
  for (; n < 31 && src[n] && src[n] != 0xFF; n++) dst[n] = (char)src[n];
  while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == '@')) n--;
  dst[n] = 0;
}

bool n2k_decode_prodinfo(const n2k_msg_t *m, n2k_prodinfo_t *out) {
  if (m->pgn != 126996 || m->len < 134) return false;
  const uint8_t *d = m->data;
  out->product_code = (uint16_t)(d[2] | d[3] << 8);
  text(d + 4, out->model_id);
  text(d + 100, out->serial);
  return true;
}

bool n2k_encode_iso_request(uint32_t pgn, uint8_t *data, uint16_t *len) {
  if (pgn > 0xFFFFFF) return false;
  data[0] = (uint8_t)pgn;
  data[1] = (uint8_t)(pgn >> 8);
  data[2] = (uint8_t)(pgn >> 16);
  *len = 3;
  return true;
}
