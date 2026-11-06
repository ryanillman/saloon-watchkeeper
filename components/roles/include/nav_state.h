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

typedef enum { NAV_TW_NONE, NAV_TW_SERVER, NAV_TW_COMPUTED } nav_tw_src_t;

typedef struct {
  bd_id_t heading_true, heading_mag, variation, cog, sog, stw, awa, aws, tw_angle, tw_speed,
          dtk, xte, dtw, ap_state, ap_target_true, ap_target_mag, set_true, drift, position;
} nav_ids_t;

// Signal K's sign for crossTrackError is to be confirmed on the bench; this is the one place it is decided.
// nav_view_t.xte is always "boat right of track > 0".
#define NAV_XTE_RIGHT_POSITIVE 1

typedef struct {
  bool have_heading;  double heading;     // rad true, [0, 2pi)
  bool have_cog;      double cog;         // rad true, [0, 2pi)
  bool have_sog;      double sog;         // m/s
  bool have_stw;      double stw;         // m/s
  bool have_awa, have_aws;  double awa, aws;   // awa rad in (-pi, pi], positive to starboard; aws m/s
  nav_tw_src_t tw_src;  double twa, tws;  // twa rad in (-pi, pi]; only meaningful when tw_src != NAV_TW_NONE
  bool have_course;   double dtk;         // rad true: the plotter's desired track, fresh
  double xte, dtw;                        // m; NAN when not fresh. xte > 0: boat right of track
  bool ap_engaged, have_ap_target;  double ap_target;   // rad true
  bool have_current;  double set, drift;  // rad true, m/s; both or neither
  bool have_pos;      double lat, lon;    // degrees; a fix no older than 10 s
} nav_view_t;

// Interns every path the page reads, and has the history store track apparent and server true wind speed.
void nav_ids_bind(nav_ids_t *ids);

// The ids to subscribe to: every id above plus the parent `environment.current` (the parser flattens it to set and drift).
size_t nav_sub_ids(const nav_ids_t *ids, bd_id_t *out, size_t max);

// Maximum ages: position 10 s; heading, wind, SOG, COG, STW 5 s; course values, autopilot 10 s; set and drift 30 s; variation 60 s.
// A stale or invalid value is missing. Heading is true, else magnetic plus a fresh variation, else none. True wind is the
// server's when both its angle and speed are fresh, else computed from apparent wind and STW, else none (SOG is never used).
// An autopilot is engaged when its fresh state is not standby, off, disabled or empty; its target counts only when engaged.
void nav_view_build(const nav_ids_t *ids, int64_t now_ms, nav_view_t *out);
