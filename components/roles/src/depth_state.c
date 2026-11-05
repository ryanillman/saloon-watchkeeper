#include "depth_state.h"
#include <stdio.h>
#include <string.h>

depth_ref_t depth_pick(const bd_sample_t *const s[3], int64_t now_ms) {
  for (int i = 0; i < 3; i++)
    if (s[i] && s[i]->kind == BD_NUMBER && s[i]->valid && bd_is_fresh(s[i], DEPTH_MAX_AGE_MS, now_ms)) return (depth_ref_t)i;
  return DEPTH_NONE;
}

const char *depth_ref_label(depth_ref_t r) {
  switch (r) {
    case DEPTH_BELOW_TRANSDUCER: return "BELOW TRANSDUCER";
    case DEPTH_BELOW_KEEL: return "BELOW KEEL";
    case DEPTH_BELOW_SURFACE: return "BELOW SURFACE";
    default: return "";
  }
}

// Days from 1970-01-01 to a civil date (proleptic Gregorian).
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

bool depth_parse_time(const char *iso, int64_t *epoch_s) {
  if (!iso) return false;
  int y, mo, d, h, mi, s, n = 0;
  if (sscanf(iso, "%4d-%2d-%2dT%2d:%2d:%2d%n", &y, &mo, &d, &h, &mi, &s, &n) != 6 || n != 19) return false;
  const char *rest = iso + n;
  if (*rest == '.') {   // fractional seconds
    rest++;
    if (*rest < '0' || *rest > '9') return false;
    while (*rest >= '0' && *rest <= '9') rest++;
  }
  if (rest[0] != 'Z' || rest[1] != '\0') return false;
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60 || y < 1970) return false;
  *epoch_s = days_from_civil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + s;
  return true;
}

void depth_format_hhmm(int64_t epoch_s, int16_t utc_offset_min, char *out) {
  int64_t local = epoch_s + (int64_t)utc_offset_min * 60;
  int64_t sod = ((local % 86400) + 86400) % 86400;
  snprintf(out, 6, "%02d:%02d", (int)(sod / 3600), (int)(sod % 3600 / 60));
}

static bool event_time(const bd_sample_t *s, int64_t now_s, int64_t *t) {
  return s && s->kind == BD_TEXT && s->valid && depth_parse_time(s->text, t) && *t > now_s;
}

tide_t tide_assess(const bd_sample_t *time_high, const bd_sample_t *time_low, int64_t now_s) {
  tide_t r = {TIDE_UNKNOWN, false, false, 0};
  if (now_s <= 0) return r;
  int64_t th = 0, tl = 0;
  bool hi = event_time(time_high, now_s, &th), lo = event_time(time_low, now_s, &tl);
  if (!hi && !lo) return r;
  r.has_next = true;
  r.next_high = hi && (!lo || th <= tl);
  r.next_s = r.next_high ? th : tl;
  r.dir = r.next_high ? TIDE_RISING : TIDE_FALLING;
  return r;
}

static int16_t utc_offset;
void depth_set_utc_offset(int16_t minutes) { utc_offset = minutes; }
int16_t depth_utc_offset(void) { return utc_offset; }
