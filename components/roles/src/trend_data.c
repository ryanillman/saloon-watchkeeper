#include <math.h>
#include "history.h"
#include "platform.h"
#include "trend_data.h"

// How far past the data's range a warn or alarm zone is drawn: a sliver of it shows even when the data is nowhere
// near, so the chart says where trouble is. Not when the zone is so far away it would flatten the line.
#define ZONE_SLIVER 0.15f
#define ZONE_REACH 2.0f

PLATFORM_EXT_BSS static float series[HIST_MINUTES];

bool trend_build(const glance_tile_t *t, bool allow_fc, trend_data_t *o) {
  size_t n = hist_read(t->trend, HIST_MINUTES, series);
  if (n < 2) return false;
  bool fc = allow_fc && t->fc_n > 0;
  size_t p = fc ? TREND_FC_SLOTS : (n < TREND_HIST_POINTS ? n : TREND_HIST_POINTS);
  float lo = INFINITY, hi = -INFINITY;
  uint16_t pts = 0;
  for (size_t i = 0; i < p; i++) {   // each point is the mean of its slice of the minutes
    long a, b;
    if (fc) {   // 6 h ending now, a slot every GLANCE_FC_MINUTES; slots before the first record are empty
      a = (long)n - (long)(TREND_FC_SLOTS - i) * GLANCE_FC_MINUTES;
      b = a + GLANCE_FC_MINUTES;
      if (a < 0) a = 0;
      if (b < 0) b = 0;
    } else {
      a = (long)(i * n / p);
      b = (long)((i + 1) * n / p);
    }
    double sum = 0;
    size_t cnt = 0;
    for (long k = a; k < b; k++)
      if (!isnan(series[k])) sum += series[k], cnt++;
    o->v[i] = cnt ? (float)(sum / (double)cnt) : NAN;
    if (cnt) {
      if (o->v[i] < lo) lo = o->v[i];
      if (o->v[i] > hi) hi = o->v[i];
      pts++;
    }
  }
  size_t total = fc ? p + t->fc_n : p;
  for (size_t i = p; i < total; i++) {
    o->v[i] = t->fc[i - p];
    if (o->v[i] < lo) lo = o->v[i];
    if (o->v[i] > hi) hi = o->v[i];
  }
  o->p = p;
  o->total = total;
  o->have = pts;
  o->minutes = n;
  o->fc = fc;
  if (hi - lo < 1e-6f) {   // flat: centre it rather than divide by nothing
    float mid = pts ? lo : 0;
    lo = mid - 0.5f;
    hi = mid + 0.5f;
  } else {
    float pad = (hi - lo) * 0.1f;
    lo -= pad;
    hi += pad;
  }
  bd_meta_t m;
  if (bd_get_meta(t->trend, &m)) {
    float span = hi - lo, nlo = lo, nhi = hi;
    for (uint8_t z = 0; z < m.zone_count; z++) {
      if (m.zones[z].state < 2) continue;
      if (isfinite(m.zones[z].upper) && m.zones[z].upper <= lo && lo - (float)m.zones[z].upper <= ZONE_REACH * span &&
          (float)m.zones[z].upper - ZONE_SLIVER * span < nlo)
        nlo = (float)m.zones[z].upper - ZONE_SLIVER * span;
      if (isfinite(m.zones[z].lower) && m.zones[z].lower >= hi && (float)m.zones[z].lower - hi <= ZONE_REACH * span &&
          (float)m.zones[z].lower + ZONE_SLIVER * span > nhi)
        nhi = (float)m.zones[z].lower + ZONE_SLIVER * span;
    }
    lo = nlo;
    hi = nhi;
  }
  o->lo = lo;
  o->hi = hi;
  return true;
}

void trend_round_range(float *lo, float *hi, float step) {
  float l = floorf(*lo / step) * step, h = ceilf(*hi / step) * step;
  if (h - l < step) h = l + step;
  *lo = l;
  *hi = h;
}

void trend_band(float lo, float hi, float zl, float zh, int32_t h, int32_t *y, int32_t *height) {
  if (zl < lo) zl = lo;
  if (zh > hi) zh = hi;
  int32_t ytop = (int32_t)((1.0f - (zh - lo) / (hi - lo)) * (float)h), ybot = (int32_t)((1.0f - (zl - lo) / (hi - lo)) * (float)h);
  *y = ytop;
  *height = ybot - ytop > 2 ? ybot - ytop : 2;
}
