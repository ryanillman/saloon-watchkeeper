#include "depth_state.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"

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

bool depth_parse_time(const char *iso, int64_t *epoch_s) {
  int64_t t;
  if (!platform_parse_iso8601(iso, &t) || t < 0) return false;   // zone offsets are honoured, as for the strip clock
  *epoch_s = t;
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

bool tide_deltas(double high_m, int64_t high_s, double low_m, int64_t low_s, double now_m, int64_t now_s, int32_t step_s, float *out, size_t n) {
  if (now_s <= 0 || high_s <= now_s || low_s <= now_s) return false;
  int64_t half = high_s > low_s ? high_s - low_s : low_s - high_s;
  if (half < 30 * 60 || half > 9 * 3600) return false;
  double mid = (high_m + low_m) / 2, amp = (high_m - low_m) / 2;
  // h(t) = mid + amp * cos(pi * (t - t_high) / half): a high at t_high, a low half a tide later or earlier.
  double h0 = mid + amp * cos(M_PI * (double)(now_s - high_s) / (double)half);
  // With the measured height the change is from that, not from the curve's idea of now: the curve is only a shape, and
  // the rise to the high must be the real high less the real height. The difference between the two shows up at
  // now and fades out by the next event, after which the curve's own heights are used.
  bool anchored = isfinite(now_m);
  int64_t next = high_s < low_s ? high_s : low_s;
  double gap = anchored ? h0 - now_m : 0;
  for (size_t i = 0; i < n; i++) {
    int64_t t = now_s + (int64_t)(i + 1) * step_s;
    double h = mid + amp * cos(M_PI * (double)(t - high_s) / (double)half);
    if (!anchored) out[i] = (float)(h - h0);
    else out[i] = (float)(h - now_m - (t < next ? gap * (double)(next - t) / (double)(next - now_s) : 0));
  }
  return true;
}

uint8_t depth_forecast_zone(bd_id_t id, const float *fc, size_t n) {
  bd_meta_t m;
  uint8_t worst = 0;
  if (id == BD_NONE || !bd_get_meta(id, &m)) return 0;
  for (uint8_t z = 0; z < m.zone_count; z++) {
    if (m.zones[z].state < 2 || m.zones[z].state <= worst) continue;
    for (size_t i = 0; i < n; i++)
      if (fc[i] >= m.zones[z].lower && fc[i] < m.zones[z].upper) {
        worst = m.zones[z].state >= 3 ? 3 : 2;
        break;
      }
  }
  return worst;
}
