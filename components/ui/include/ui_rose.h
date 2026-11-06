#pragma once
// The Navigation page's heading-up compass rose: ring and numerals turning with the heading, the desired-course line
// with its cross-track shift, the COG triangle, the autopilot bug, and the wind markers. One custom-drawn object.
// Everything is given as numbers (radians, metres); the page decides what is known.
#include <stdbool.h>
#include "lvgl.h"

typedef struct {
  bool have_heading;   double heading;     // rad true. False: dimmed ring, boat and wind only (no rose-frame markers)
  bool course;         double dtk;         // the desired-course line, arrowhead and dots, at this bearing
  double xte_m;                            // boat right of track > 0; NAN = line unshifted
  bool have_cog;       double cog;
  bool ap_bug;         double ap_target;
  bool have_awa;       double awa;         // rad off the bow, positive to starboard
  bool have_twa;       double twa;
  bool have_mob;       double mob;         // the man-overboard target's bearing, rad true; needs a heading like the other rose-frame markers
} ui_rose_data_t;

// Size the object afterwards. The rose is a circle of ui_rose_radius_px(min(w, h)); the margin around it holds the wind
// markers and the autopilot bug.
lv_obj_t *ui_rose_create(lv_obj_t *parent);

// The data is copied.
void ui_rose_set(lv_obj_t *rose, const ui_rose_data_t *d);

int32_t ui_rose_radius_px(int32_t side_px);

// Dot spacing across the course line: 0.21 of the radius, standing for 0.1 nm.
double ui_rose_dot_px(double radius_px);

// How far the middle segment of the course line moves along the course's right-hand axis: -xte / 185.2 m x dot spacing.
// A boat right of track moves it left (negative). NAN is 0; clamped to two dots either way.
double ui_rose_xte_shift_px(double xte_m, double radius_px);
