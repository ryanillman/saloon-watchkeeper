#pragma once
// The anchor plan view: the anchor at the centre, a dashed ring at the alarm radius, the boat's track and the boat.
// North is up. One custom-drawn object (no per-point LVGL objects), so a 720-point track costs no RAM beyond the
// caller's buffer. Used by the Anchor page and the Glance Anchor tile.
#include <stddef.h>
#include "lvgl.h"

typedef struct { float x, y; } ui_swing_pt_t;   // metres east and north of the anchor; x NAN = a gap in the track
typedef enum { UI_SWING_GREEN, UI_SWING_AMBER, UI_SWING_RED, UI_SWING_GREY } ui_swing_state_t;

typedef struct {
  const ui_swing_pt_t *track;   // oldest first; the last valid point is the boat. NULL/0: nothing to plot
  size_t track_n;
  double limit_m, warn_m;       // NAN or 0: no ring
  ui_swing_state_t state;       // ring and boat colour: green holding, amber warn, red dragging, grey no data
  bool show_n;                  // the N marker (the Glance tile is too small for it)
} ui_swing_data_t;

lv_obj_t *ui_swing_create(lv_obj_t *parent);   // set its size afterwards; it draws a square in the middle of it

// The data (and its track) must stay valid until the next ui_swing_set: it is read at every redraw.
void ui_swing_set(lv_obj_t *sw, const ui_swing_data_t *d);

// Pixels per metre for a plot plot_px across. The limit ring takes about 80% of the way to the edge, and the scale
// shrinks so the farthest point (with 10% margin) still fits when the boat is outside the ring. NAN or 0 limit: the
// farthest point alone decides. Always finite and positive.
double ui_swing_scale(double limit_m, double max_dist_m, int32_t plot_px);
