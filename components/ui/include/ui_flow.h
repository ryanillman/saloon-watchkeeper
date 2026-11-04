#pragma once
// Power-flow diagram parts: node icons and animated flow arrows. Both are drawn from primitives
// in the current palette, so they scale to any size and need no image assets.
#include <stdbool.h>
#include "lvgl.h"

// ICON_CONVERTER: a battery with an arrow feeding into it, a DC-DC link to another bank. Fills like ICON_BATTERY.
typedef enum { ICON_SOLAR, ICON_SHORE, ICON_ENGINE, ICON_BATTERY, ICON_LOAD, ICON_CONVERTER } ui_icon_t;

// A square icon; set its size with lv_obj_set_size. Drawn in the foreground colour when active,
// dim when not (a source making no power, a load drawing none).
lv_obj_t *ui_icon_create(lv_obj_t *parent, ui_icon_t kind);
void ui_icon_set_active(lv_obj_t *icon, bool active);
// Battery and converter only: fills the body from the bottom to `fraction` (0..1) in `color`; !valid draws it empty.
void ui_icon_set_fill(lv_obj_t *icon, double fraction, bool valid, lv_color_t color);

// An arrow along a quadratic curve from (x0,y0) to (x1,y1), both in the parent's coordinates, bent
// sideways by `bend` pixels (positive turns left of the direction of travel, 0 is straight). The object
// sizes and places itself around the curve and its label. Chevrons travel from start to end while
// the flow is active; the speed and weight follow the level.
lv_obj_t *ui_flow_create(lv_obj_t *parent);
void ui_flow_set_path(lv_obj_t *flow, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t bend);

// The same, as an S-curve that leaves the start along `leave` and arrives along `arrive` (so several
// arrows can merge into one point, or fork out of it, without crossing at odd angles). `side` puts the
// label on the left of the direction of travel (+1), the right (-1), or above (0).
typedef enum { AXIS_H, AXIS_V } ui_axis_t;
void ui_flow_set_route(lv_obj_t *flow, int32_t x0, int32_t y0, int32_t x1, int32_t y1, ui_axis_t leave,
                       ui_axis_t arrive, int side);

// level 0: no flow, a dim static stub. 1..3: moving chevrons, slow and thin to fast and heavy.
// `valid` false hides the arrow entirely (the value behind it is unknown). `label` (may be NULL or "")
// is drawn beside the curve, e.g. "203 W to battery".
void ui_flow_set(lv_obj_t *flow, uint8_t level, bool valid, const char *label);

// Level for a flow of `watts` when the largest flow on the page is `ref_watts`: 0 below half a watt,
// then 1 (under a quarter of ref), 2 (under 60 %), 3.
uint8_t ui_flow_level_for(double watts, double ref_watts);

// Queries, for tests.
uint8_t ui_flow_level(lv_obj_t *flow);
bool ui_flow_valid(lv_obj_t *flow);
const char *ui_flow_label(lv_obj_t *flow);
bool ui_flow_animating(lv_obj_t *flow);
