#pragma once
// Scale math for dials and strips. Pure; values in display units.
#include <stdint.h>

typedef struct {
  double lo, hi;
  double major[16];
  uint8_t major_count;   // majors that fall inside [lo, hi]
  double minor_step;
} ui_ticks_t;

// Nice (1-2-5) major step giving about target_majors marks between lo and hi.
void ui_scale_ticks(double lo, double hi, uint8_t target_majors, ui_ticks_t *out);

// Position of v on the scale, clamped to 0..1.
double ui_scale_fraction(double v, double lo, double hi);
