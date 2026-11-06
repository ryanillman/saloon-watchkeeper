#pragma once
#include "lvgl.h"

typedef enum { SIZE_SQUARE, SIZE_WIDE } size_class_t;
typedef enum { PALETTE_DAY, PALETTE_DUSK, PALETTE_NIGHT } palette_t;

typedef struct {
  lv_color_t bg, fg, dim, accent, strip_bg, warn, alarm, fault_cross, fault_label;
  lv_color_t ok;   // a healthy state (the Glance tile edge); night has no green, so it is the dim red there
  lv_color_t nav_mob;   // the man-overboard target on the Navigation page
  lv_color_t nav_course, nav_app, nav_true, nav_lubber;   // the Navigation page: desired course and COG, apparent and true wind, the lubber line
} ui_colors_t;

const ui_colors_t *ui_theme_colors(palette_t p);

// Signal K environment.mode to a palette: "night" is NIGHT, "dawn" and "dusk" are DUSK,
// anything else (day, sunrise, sunset, unknown, NULL) is DAY.
palette_t palette_from_env_mode(const char *mode);

// The colour of a trend line: `normal`, or (zone_state 2 warn, 3 alarm) the palette's warn or alarm colour lightened so it
// stands out over the zone's own band.
lv_color_t ui_zone_line_color(uint8_t zone_state, lv_color_t normal);

// The palette in use; the shell sets it, widgets draw with it.
void ui_theme_set_current(palette_t p);
const ui_colors_t *ui_theme(void);
