#include "ui_clock.h"
#include "platform.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define STALE_MS (10 * 60 * 1000LL)

static struct { bool have; int64_t epoch_s, fed_ms; } c;
static struct { bool have; int64_t epoch_s, rx_ms; } offered;   // the previous offer, shown or not
#define OFFER_TOLERANCE_MS 5000

bool ui_clock_parse_iso(const char *iso, int64_t *epoch_s) { return platform_parse_iso8601(iso, epoch_s); }

void ui_clock_feed(int64_t epoch_s, int64_t now_ms) {
  c.have = true;
  c.epoch_s = epoch_s;
  c.fed_ms = now_ms;
}

void ui_clock_offer(int64_t epoch_s, int64_t rx_ms) {
  int64_t gap_ms = (epoch_s - offered.epoch_s) * 1000 - (rx_ms - offered.rx_ms);   // time that passed there minus here
  bool credible = offered.have && epoch_s > offered.epoch_s && gap_ms <= OFFER_TOLERANCE_MS && gap_ms >= -OFFER_TOLERANCE_MS;
  offered.have = true;
  offered.epoch_s = epoch_s;
  offered.rx_ms = rx_ms;
  if (credible) ui_clock_feed(epoch_s, rx_ms);
}

bool ui_clock_format(int64_t now_ms, int offset_min, bool source_up, char out[6]) {
  if (!c.have) return false;
  int64_t age = now_ms - c.fed_ms;
  if (age > STALE_MS && !source_up) return false;
  int64_t t = c.epoch_s + age / 1000 + offset_min * 60LL;
  int64_t sod = ((t % 86400) + 86400) % 86400;
  snprintf(out, 6, "%02d:%02d", (int)(sod / 3600), (int)(sod % 3600 / 60));
  return true;
}

void ui_clock_reset(void) {
  memset(&c, 0, sizeof c);
  memset(&offered, 0, sizeof offered);
}
