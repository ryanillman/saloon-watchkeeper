#pragma once
// What the Anchor page and its Glance tile show, worked out from boatdata, the alert list and the history store.
// Pure: no LVGL, no I/O. The watch itself runs on the Signal K server (an anchor plugin); nothing here detects drift.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "boatdata.h"

typedef enum { AM_NOT_SET, AM_RESTORING, AM_HOLDING, AM_WARN, AM_DRAGGING, AM_NO_DATA, AM_NO_LIMIT } anchor_mode_t;

typedef struct { float x, y; } anchor_pt_t;   // metres east and north of the anchor; x is NAN for a gap in the track

typedef struct {
  bd_id_t position, max_radius, current_radius, bearing_true, rode_length, warning_radius, watch_zone;
} anchor_ids_t;

typedef struct {
  anchor_mode_t mode;
  double distance_m, limit_m, warn_m, bearing_rad, max_m, rode_m;   // NAN when unknown; bearing is vessel to anchor, true
  bool server_none;      // the live server explicitly reports no anchor (position null), whatever the mode: the boot check reads this
  bool circle;           // false for a sector or polygon watch zone (the radius cannot be adjusted)
  char zone[8];          // the zone's shape when published: "circle", "sector", "polygon"; "" when not
  int64_t set_ago_s;     // seconds from the anchor being set to the latest update (server clock); -1 unknown
  size_t track_n;        // points written to the caller's buffer
} anchor_view_t;

// Interns the paths under navigation.anchor. and has the history store track distance and bearing (2 of its slots).
void anchor_ids_bind(anchor_ids_t *ids);

// The ids to subscribe to for this role.
size_t anchor_sub_ids(const anchor_ids_t *ids, bd_id_t *out, size_t max);

// restoring: the watch record says an anchor was set and the server has not shown it back yet (anchor_watch).
// source_live: the Signal K connection is up. Not live (and not restoring) is NO DATA: nothing is known.
// Modes: no position while live is NOT SET; stale (90 s) distance is NO DATA; distance without a limit is NO LIMIT;
// otherwise the server's alert on navigation.anchor decides (alert/warn: WARN, alarm/emergency: DRAGGING).
// The track is history oldest first, then the live point, from when the anchor was set (the gap between the position's
// and the distance's Signal K timestamps), at most track_cap points (12 h). Unknown set time: all the history there is.
void anchor_view_build(const anchor_ids_t *ids, bool restoring, bool source_live, int64_t now_ms, anchor_view_t *out,
                       anchor_pt_t *track, size_t track_cap);
