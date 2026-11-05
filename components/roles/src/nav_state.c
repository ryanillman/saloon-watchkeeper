#include "nav_state.h"
#include <math.h>

bool nav_true_wind(double awa_rad, double aws, double stw, double *twa_rad, double *tws) {
  if (!isfinite(awa_rad) || !isfinite(aws) || !isfinite(stw) || aws < 0 || stw < 0) return false;
  double y = aws * sin(awa_rad), x = aws * cos(awa_rad) - stw;
  double speed = hypot(x, y);
  *tws = speed;
  *twa_rad = speed > 0 ? atan2(y, x) : 0;   // atan2(0, -0) is pi: a calm is dead ahead
  return true;
}
