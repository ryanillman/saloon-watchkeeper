#include "ui_scale.h"
#include <math.h>
#include <string.h>

void ui_scale_ticks(double lo, double hi, uint8_t target, ui_ticks_t *t) {
  memset(t, 0, sizeof *t);
  t->lo = lo;
  t->hi = hi;
  if (!(hi > lo) || target < 2) return;
  double raw = (hi - lo) / (target - 1);
  double mag = pow(10, floor(log10(raw)));
  double norm = raw / mag;
  double nice = norm < 1.5 ? 1 : norm < 3 ? 2 : norm < 7 ? 5 : 10;
  double step = nice * mag;
  t->minor_step = step / (nice == 2 ? 4 : 5);
  double eps = step * 1e-9;
  for (double v = ceil(lo / step - 1e-9) * step; v <= hi + eps && t->major_count < 16; v += step)
    t->major[t->major_count++] = fabs(v) < eps ? 0 : v;
}

double ui_scale_fraction(double v, double lo, double hi) {
  if (!(hi > lo)) return 0;
  double f = (v - lo) / (hi - lo);
  return f < 0 ? 0 : f > 1 ? 1 : f;
}
