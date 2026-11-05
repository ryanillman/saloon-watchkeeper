#pragma once
// What the Navigation page and its Glance tile show, worked out from boatdata. Pure: no LVGL, no I/O.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "boatdata.h"

// True wind through the water from apparent wind (angle off the bow, positive to starboard, radians) and speeds
// (m/s): TWA = atan2(S sin A, S cos A - B), TWS = hypot(S sin A, S cos A - B). False, outputs untouched, when an input is
// not finite or a speed is negative. No wind at all (both speeds zero) is TWA 0, TWS 0.
bool nav_true_wind(double awa_rad, double aws, double stw, double *twa_rad, double *tws);
