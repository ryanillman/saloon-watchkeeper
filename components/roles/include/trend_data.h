#pragma once
// The numbers behind a trend chart, pure so both the Glance tile and the Depth page draw the same thing and it can be
// tested without a display: the history binned into points, the forecast after "now", and the range they are drawn in.
#include <stdbool.h>
#include <stddef.h>
#include "glance.h"

#define TREND_HIST_POINTS 144                          // 12 h at 5 min a point, without a forecast
#define TREND_FC_SLOTS (6 * 60 / GLANCE_FC_MINUTES)    // with a forecast the chart is 6 h back and 6 h on
#define TREND_MAX_POINTS (TREND_HIST_POINTS + GLANCE_FC_POINTS)

typedef struct {
  float v[TREND_MAX_POINTS];   // history points (NAN = no data in that slot), then the forecast; SI like the history
  size_t p, total;             // history points, and all of them
  uint16_t have;               // history points with data
  size_t minutes;              // how much history there is, in minutes
  bool fc;                     // a forecast follows the history
  float lo, hi;                // the range to plot: the data with a margin, widened to show a near warn or alarm zone
} trend_data_t;

// False when the path has less than two minutes of history. `allow_fc` is whether the chart has room for the forecast.
bool trend_build(const glance_tile_t *t, bool allow_fc, trend_data_t *out);

// Round [*lo, *hi] outward to multiples of `step` (> 0), keeping at least one step between them.
void trend_round_range(float *lo, float *hi, float step);
// The y offset and height, in a chart `h` high plotting [lo, hi], of the part of [zl, zh] inside the range.
void trend_band(float lo, float hi, float zl, float zh, int32_t h, int32_t *y, int32_t *height);
