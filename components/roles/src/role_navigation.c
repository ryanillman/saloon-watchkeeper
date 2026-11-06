#include <math.h>
#include <stdio.h>
#include "history.h"
#include "emergency.h"
#include "emergency_page.h"
#include "nav_state.h"
#include "navigation_page.h"
#include "units.h"

// The page is in page_navigation.c.

// The Glance tile: wind speed with its last hour. True wind speed when the server supplies it (it has a history); else
// apparent wind speed, labelled APP: a true wind this display computes has no history to trend or compare.
void navigation_tile(const role_binding_t *b, int64_t now_ms, glance_tile_t *out) {
  out->title = b->def->title;
  nav_ids_t ids;
  nav_ids_bind(&ids);
  nav_view_t v;
  nav_view_build(&ids, now_ms, &v);
  bool server = v.tw_src == NAV_TW_SERVER;
  bd_id_t id = server ? ids.tw_speed : ids.aws;
  out->trend = id;   // named even with no wind yet: the page builds the chart only if the tile has a trend at that moment
  if (!server && !v.have_aws) {
    glance_tile_state(out, GT_NO_DATA);
    return;
  }
  glance_tile_state(out, glance_path_state(id, 5000, now_ms));
  const cfg_units_t *u = b->units;
  snprintf(out->headline, sizeof out->headline, "%.0f", units_from_si(Q_SPEED, server ? v.tws : v.aws, u));
  snprintf(out->unit, sizeof out->unit, "%s", units_label(Q_SPEED, u));

  char change[24] = "";
  float delta;
  if (hist_change(id, 60, &delta)) snprintf(change, sizeof change, "%+.0f %s / 1 h", units_from_si(Q_SPEED, delta, u), units_label(Q_SPEED, u));
  if (server) snprintf(out->context[0], sizeof out->context[0], "%s", change);
  else snprintf(out->context[0], sizeof out->context[0], "APP%s%s", change[0] ? "  " : "", change);

  float hour[61];
  size_t n = hist_read(id, 61, hour);
  float peak = NAN;
  for (size_t i = 0; i < n; i++)
    if (!isnan(hour[i]) && (isnan(peak) || hour[i] > peak)) peak = hour[i];
  if (!isnan(peak)) snprintf(out->context[1], sizeof out->context[1], "max %.0f %s", units_from_si(Q_SPEED, peak, u), units_label(Q_SPEED, u));
}

// Page 0 is the compass. Page 1 is the emergency script (built in emergency_page.c).
static void nav_build(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b) {
  if (page == 0) {
    emergency_page_reset();   // the script's widgets are gone with the cleaned area
    navigation_build_page(area, 0, sc, b);
    return;
  }
  navigation_page_reset();    // and the compass's
  emergency_page_build(area, sc, b);
}

static void nav_refresh(role_binding_t *b, int64_t now_ms) {   // each is inert when its page is not the one on screen
  navigation_refresh(b, now_ms);
  emergency_page_refresh(b, now_ms);
}

// Which page shows an alert: a MOB the compass (it carries the MOB target), every other declared emergency the script.
static uint8_t nav_page_for(const role_binding_t *b, size_class_t sc, const char *path) {
  (void)b;
  (void)sc;
  em_nature_t n = em_nature_from_path(path);
  return n == EM_NONE || n == EM_MOB ? 0 : 1;
}

const role_def_t role_navigation = {
  .name = "navigation",
  .title = "NAVIGATION",
  .groups = NULL,
  .group_count = 0,
  .page_count = 2,
  .page_for = nav_page_for,
  .build_page = nav_build,
  .refresh = nav_refresh,
  .tile = navigation_tile,
};
