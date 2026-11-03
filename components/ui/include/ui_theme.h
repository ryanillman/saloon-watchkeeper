#pragma once
#include "lvgl.h"

typedef enum { SIZE_SQUARE, SIZE_WIDE } size_class_t;
typedef enum { PALETTE_DAY, PALETTE_DUSK, PALETTE_NIGHT } palette_t;

typedef struct {
  lv_color_t bg, fg, dim, accent, strip_bg, warn, alarm, fault_cross, fault_label;
} ui_colors_t;

const ui_colors_t *ui_theme_colors(palette_t p);
