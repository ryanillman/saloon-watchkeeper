#include "ui_clock.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define STALE_MS (10 * 60 * 1000LL)

static struct { bool have; int64_t epoch_s, fed_ms; } c;
static struct { bool have; int64_t epoch_s, rx_ms; } offered;   // the previous offer, shown or not
#define OFFER_TOLERANCE_MS 5000

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm).
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

static bool two(const char *s, int *v) {
  if (!isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1])) return false;
  *v = (s[0] - '0') * 10 + (s[1] - '0');
  return true;
}

bool ui_clock_parse_iso(const char *iso, int64_t *epoch_s) {
  if (!iso || strlen(iso) < 20) return false;
  int Y, mo, d, h, mi, s, cc;
  if (!two(iso, &cc) || !two(iso + 2, &Y)) return false;
  Y += cc * 100;
  if (iso[4] != '-' || !two(iso + 5, &mo) || iso[7] != '-' || !two(iso + 8, &d)) return false;
  if (iso[10] != 'T' || !two(iso + 11, &h) || iso[13] != ':' || !two(iso + 14, &mi) || iso[16] != ':' || !two(iso + 17, &s))
    return false;
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) return false;
  const char *p = iso + 19;
  if (*p == '.') {   // fractional seconds: at least one digit
    p++;
    if (!isdigit((unsigned char)*p)) return false;
    while (isdigit((unsigned char)*p)) p++;
  }
  int off_s = 0;
  if (*p == 'Z') {
    p++;
  } else if (*p == '+' || *p == '-') {
    int oh, om;
    if (!two(p + 1, &oh) || p[3] != ':' || !two(p + 4, &om) || oh > 23 || om > 59) return false;
    off_s = (oh * 3600 + om * 60) * (*p == '+' ? 1 : -1);
    p += 6;
  } else {
    return false;
  }
  if (*p) return false;
  *epoch_s = days_from_civil(Y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + s - off_s;
  return true;
}

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
