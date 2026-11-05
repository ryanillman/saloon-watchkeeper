#include "anchor_state.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "alerts.h"
#include "history.h"
#include "platform.h"

#define STALE_MS 90000
#define ANCHOR_ALERT_ID "navigation.anchor"

// History reads are 2 x 720 floats: too much for the UI task's stack, and only the UI task calls here.
PLATFORM_EXT_BSS static float hist_dist[HIST_MINUTES], hist_bearing[HIST_MINUTES];

void anchor_ids_bind(anchor_ids_t *ids) {
  ids->position = bd_intern("navigation.anchor.position");
  ids->max_radius = bd_intern("navigation.anchor.maxRadius");
  ids->current_radius = bd_intern("navigation.anchor.currentRadius");
  ids->bearing_true = bd_intern("navigation.anchor.bearingTrue");
  ids->rode_length = bd_intern("navigation.anchor.rodeLength");
  ids->warning_radius = bd_intern("navigation.anchor.warningRadius");
  ids->watch_zone = bd_intern("navigation.anchor.watchZone");
  hist_track(ids->current_radius);
  hist_track(ids->bearing_true);
}

size_t anchor_sub_ids(const anchor_ids_t *ids, bd_id_t *out, size_t max) {
  const bd_id_t all[] = {ids->position, ids->max_radius, ids->current_radius, ids->bearing_true,
                         ids->rode_length, ids->warning_radius, ids->watch_zone};
  size_t n = 0;
  for (size_t i = 0; i < sizeof all / sizeof all[0]; i++)
    if (all[i] != BD_NONE && n < max) out[n++] = all[i];
  return n;
}

static bool number(bd_id_t id, bd_sample_t *s) {
  return id != BD_NONE && bd_get(id, s) && s->valid && s->kind == BD_NUMBER;
}
static double value_or_nan(bd_id_t id) {
  bd_sample_t s;
  return number(id, &s) ? s.num : NAN;
}

// Fresh when received recently AND, once the server has timestamped it, not behind the server's own clock: on a reconnect
// Signal K replays the last value of every path with its old timestamp, and a dead plugin's value must not look live.
static bool fresh(const bd_sample_t *s, int64_t now_ms) {
  if (!bd_is_fresh(s, STALE_MS, now_ms)) return false;
  int64_t newest = bd_newest_source_time();
  return s->src_s == 0 || newest == 0 || newest - s->src_s <= STALE_MS / 1000;
}

// The boat's place relative to the anchor: the anchor lies `dist` away on `bearing` (radians, true), so the boat
// lies on the reciprocal.
static anchor_pt_t place(double dist, double bearing) {
  return (anchor_pt_t){(float)(-dist * sin(bearing)), (float)(-dist * cos(bearing))};
}

void anchor_view_build(const anchor_ids_t *ids, bool restoring, bool source_live, int64_t now_ms, anchor_view_t *out,
                       anchor_pt_t *track, size_t track_cap) {
  memset(out, 0, sizeof *out);
  out->distance_m = out->limit_m = out->warn_m = out->bearing_rad = out->max_m = out->rode_m = NAN;
  out->set_ago_s = -1;
  out->circle = true;

  bd_sample_t pos = {0}, dist = {0}, bearing = {0}, limit = {0}, zone = {0};
  bool have_pos = ids->position != BD_NONE && bd_get(ids->position, &pos) && pos.valid && pos.kind == BD_POSITION;
  bool have_dist = number(ids->current_radius, &dist) && fresh(&dist, now_ms);
  bool have_bearing = number(ids->bearing_true, &bearing) && fresh(&bearing, now_ms);
  bool have_limit = number(ids->max_radius, &limit);
  if (ids->watch_zone != BD_NONE && bd_get(ids->watch_zone, &zone) && zone.valid && zone.kind == BD_TEXT) {
    snprintf(out->zone, sizeof out->zone, "%.7s", zone.text);   // circle, sector, polygon
    out->circle = !strcmp(zone.text, "circle");
  }
  // The live server explicitly sent null: not a sample invalidated when the connection dropped (nothing resent yet).
  out->server_none = source_live && !have_pos && pos.rx_ms != 0 && !pos.invalidated;
  if (have_dist) out->distance_m = dist.num;
  if (have_bearing) out->bearing_rad = bearing.num;
  if (have_limit) out->limit_m = limit.num;
  out->warn_m = value_or_nan(ids->warning_radius);
  out->rode_m = value_or_nan(ids->rode_length);

  if (restoring && !(have_pos && have_dist && have_limit)) out->mode = AM_RESTORING;
  else if (!source_live) out->mode = AM_NO_DATA;
  else if (!have_pos) out->mode = pos.invalidated ? AM_NO_DATA : AM_NOT_SET;   // invalidated: the reconnected server has not said yet
  else if (!have_dist) out->mode = AM_NO_DATA;
  else if (!have_limit) out->mode = AM_NO_LIMIT;
  else {
    alert_state_t a = alerts_for_path(ANCHOR_ALERT_ID);
    out->mode = a >= AL_ALARM ? AM_DRAGGING : a >= AL_ALERT ? AM_WARN : AM_HOLDING;
  }
  if (out->mode == AM_NOT_SET || out->mode == AM_RESTORING || track_cap == 0) return;

  // Minutes of history to draw: since the anchor was set when both server timestamps are known, else all there is.
  size_t minutes = HIST_MINUTES < track_cap - 1 ? HIST_MINUTES : track_cap - 1;
  if (pos.src_s > 0 && dist.src_s >= pos.src_s) {
    out->set_ago_s = dist.src_s - pos.src_s;
    int64_t since = out->set_ago_s / 60 + 1;
    if (since < (int64_t)minutes) minutes = (size_t)since;
  }
  size_t nd = hist_read(ids->current_radius, minutes, hist_dist);
  size_t nb = hist_read(ids->bearing_true, minutes, hist_bearing);
  size_t n = nd < nb ? nd : nb;   // both end at the current minute: align from the end
  double max = NAN;
  for (size_t i = 0; i < n; i++) {
    float d = hist_dist[nd - n + i], b = hist_bearing[nb - n + i];
    if (isnan(d) || isnan(b)) {
      track[out->track_n++] = (anchor_pt_t){NAN, NAN};
      continue;
    }
    track[out->track_n++] = place(d, b);
    if (isnan(max) || d > max) max = d;
  }
  if (have_dist && have_bearing) {
    track[out->track_n++] = place(dist.num, bearing.num);
    if (isnan(max) || dist.num > max) max = dist.num;
  }
  out->max_m = max;
}
