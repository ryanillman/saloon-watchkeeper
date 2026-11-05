#include <math.h>
#include <stdio.h>
#include <string.h>
#include "alerts.h"
#include "anchor_ctl.h"
#include "anchor_page.h"
#include "platform.h"
#include "ui_swing.h"

// The Glance tile reads the same view as the page, into its own buffers (the tile is built and refreshed on the UI task).
PLATFORM_EXT_BSS static anchor_pt_t tile_track[720];
static ui_swing_data_t tile_swing;
_Static_assert(sizeof(anchor_pt_t) == sizeof(ui_swing_pt_t), "the plot reads the track in place");

static ui_swing_state_t swing_state(anchor_mode_t m) {
  switch (m) {
    case AM_HOLDING: return UI_SWING_GREEN;
    case AM_WARN:
    case AM_NO_LIMIT:
    case AM_RESTORING: return UI_SWING_AMBER;
    case AM_DRAGGING: return UI_SWING_RED;
    default: return UI_SWING_GREY;
  }
}

static void word(glance_tile_t *t, const char *w) { snprintf(t->word, sizeof t->word, "%s", w); }

static void anchor_tile(const role_binding_t *b, int64_t now_ms, glance_tile_t *out) {
  anchor_view_t v;
  anchor_build_view(now_ms, &v, tile_track, sizeof tile_track / sizeof tile_track[0]);
  out->title = b->def->title;
  out->has_swing = true;
  const cfg_units_t *u = b->units;

  switch (v.mode) {
    case AM_HOLDING: glance_tile_state(out, GT_OK); word(out, "HOLDING"); break;
    case AM_WARN: glance_tile_state(out, GT_WARN); word(out, "WARN"); break;
    case AM_DRAGGING: glance_tile_state(out, GT_ALARM); word(out, "DRAGGING"); break;
    case AM_NOT_SET: glance_tile_state(out, GT_OFF); word(out, "NOT SET"); break;
    case AM_RESTORING: glance_tile_state(out, GT_NO_DATA); word(out, "RESTORING"); break;
    case AM_NO_LIMIT: glance_tile_state(out, GT_NO_DATA); word(out, "NO LIMIT"); break;
    default: glance_tile_state(out, GT_NO_DATA); break;   // NO DATA
  }
  // An alarm outranks missing data: the server's alarm and the display's own (watch lost, not restored).
  if (alerts_for_path("navigation.anchor") >= AL_ALARM || alerts_for_path(ANCHOR_WATCH_ALERT_ID) >= AL_ALARM) {
    glance_tile_state(out, GT_ALARM);
    word(out, alerts_for_path(ANCHOR_WATCH_ALERT_ID) >= AL_ALARM && v.mode != AM_DRAGGING ? "WATCH!" : "DRAGGING");
  }

  if (!isnan(v.distance_m) && v.mode != AM_NOT_SET) {
    anchor_format_distance(out->headline, sizeof out->headline, v.distance_m, u, false);
    snprintf(out->unit, sizeof out->unit, "%s", anchor_distance_unit(u));
  }
  char a[24], b2[24];
  if (!isnan(v.limit_m) && v.mode != AM_NOT_SET) {
    anchor_format_distance(a, sizeof a, v.limit_m, u, true);
    if (!isnan(v.rode_m)) {
      anchor_format_distance(b2, sizeof b2, v.rode_m, u, true);
      snprintf(out->context[0], sizeof out->context[0], "limit %.12s, chain %.12s", a, b2);
    } else {
      snprintf(out->context[0], sizeof out->context[0], "limit %s", a);
    }
  }
  if (!isnan(v.max_m)) {
    anchor_format_distance(a, sizeof a, v.max_m, u, true);
    snprintf(out->context[1], sizeof out->context[1], "max tonight %s", a);   // the page has the set time
  }

  if (v.mode == AM_NOT_SET || v.mode == AM_RESTORING) return;   // nothing to plot
  tile_swing = (ui_swing_data_t){.track = (const ui_swing_pt_t *)tile_track, .track_n = v.track_n,
                                 .limit_m = v.limit_m, .warn_m = v.warn_m, .state = swing_state(v.mode), .show_n = false};
  out->swing = &tile_swing;
}

const role_def_t role_anchor = {
  .name = "anchor",
  .title = "ANCHOR",
  .groups = NULL,
  .group_count = 0,
  .page_count = 1,
  .build_page = anchor_build_page,
  .refresh = anchor_refresh,
  .tile = anchor_tile,
};
