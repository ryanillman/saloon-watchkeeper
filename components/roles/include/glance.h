#pragma once
// What the Glance page needs from each role: one tile's worth of state, a headline value, and
// (optionally) a trend path. The state rule is pure and shared; the device raises no thresholds of its
// own, so state comes only from Signal K alerts and meta.zones, and missing data is never a calm state.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "boatdata.h"

// Ascending severity, so the worst of several is the largest. An alarm outranks NO DATA so a missing
// neighbour can never hide it.
typedef enum { GT_OFF, GT_OK, GT_ALERT, GT_WARN, GT_NO_DATA, GT_ALARM } gt_state_t;

// bar_pct for a bar whose level is not known (a sender missing): drawn as FAIL, never as a number.
#define GT_PCT_UNKNOWN 255

#define GLANCE_FC_POINTS 72   // 6 h of forecast
#define GLANCE_FC_MINUTES 5

typedef struct {
  const char *title;       // "ENGINE"
  gt_state_t state;
  char word[16];           // "OK", "STOPPED", "NO DATA"
  char headline[16];       // the big value, display units, no unit; empty when there is none to show
  char unit[8];
  char context[2][40];     // the first is kept on every board; the second only on the larger ones
  bd_id_t trend;           // BD_NONE = no trend line. With bars it is only tracked in the history store, not drawn
  uint8_t bar_count;       // > 0: bars instead of a headline (Tanks)
  char bar_label[3];       // one letter each: W, F, S
  uint8_t bar_pct[3];
  bool has_swing;          // this role draws a plan view: the page keeps room for it, hidden while `swing` is NULL
  const void *swing;       // the Anchor's plan view (a ui_swing_data_t that outlives the next call); NULL = none. Drawn in place of a trend
  bool has_forecast;       // this role may draw a forecast after "now": the page keeps room for it
  uint8_t fc_n;            // forecast points that follow (0 = none known now); at most GLANCE_FC_POINTS
  uint8_t line_state;      // 0 normal; 2 or 3: the chart's forecast enters a warn or alarm zone, so the line is drawn in that zone's colour (the tile state is separate)
  float fc[GLANCE_FC_POINTS];   // the trend path's expected values (SI, as the history), GLANCE_FC_MINUTES apart from the first, which is that long after now
} glance_tile_t;

gt_state_t glance_worst(const gt_state_t *s, size_t n);   // GT_OFF when n == 0

// State of one data path now: the worse of its Signal K alert (looked up by the path) and, when
// the value is valid and fresh, the meta.zones entry holding the value (lower bound inclusive, upper
// exclusive). Missing, invalid or older than max_age_ms (0 = no limit) data is GT_NO_DATA.
gt_state_t glance_path_state(bd_id_t id, int32_t max_age_ms, int64_t now_ms);

// The Signal K alert on a path alone, ignoring the data: GT_OK when there is none. For a role whose data is
// not read while it is idle (a stopped engine) but whose alarms still count.
gt_state_t glance_alert_state(bd_id_t id);

const char *glance_state_word(gt_state_t s);   // "OFF", "OK", "ALERT", "WARN", "NO DATA", "ALARM"

// Sets the tile's state and its default word.
void glance_tile_state(glance_tile_t *t, gt_state_t s);
