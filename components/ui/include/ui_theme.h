#pragma once
#include "lvgl.h"

typedef enum { SIZE_SQUARE, SIZE_WIDE } size_class_t;
typedef enum { PALETTE_DAY, PALETTE_DUSK, PALETTE_NIGHT } palette_t;

typedef struct {
  lv_color_t bg, fg, dim, accent, strip_bg, warn, alarm, fault_cross, fault_label;
  lv_color_t ok;   // a healthy state (the Glance tile edge); night has no green, so it is the dim red there
} ui_colors_t;

const ui_colors_t *ui_theme_colors(palette_t p);

// Signal K environment.mode to a palette: "night" is NIGHT, "dawn" and "dusk" are DUSK,
// anything else (day, sunrise, sunset, unknown, NULL) is DAY.
palette_t palette_from_env_mode(const char *mode);

// The palette in use; the shell sets it, widgets draw with it.
void ui_theme_set_current(palette_t p);
const ui_colors_t *ui_theme(void);
