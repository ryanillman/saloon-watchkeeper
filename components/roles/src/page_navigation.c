// The Navigation page: an HSI-style compass (ui_rose) with the wind, course and autopilot readouts around it. What is
// known, and how fresh, is decided in nav_state.c; this draws it and flags what is missing.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "nav_state.h"
#include "navigation_page.h"
#include "ui_gauge.h"
#include "units.h"

static struct {
  lv_obj_t *rose, *heading, *ap_badge, *awa, *aws, *twa, *tws, *sog, *course, *xte, *dtw, *ap_hdg, *set_drift;
  ui_rose_data_t rd;
  nav_ids_t ids;
  bool built, wide;
} w;

static const cfg_units_t default_units = {"C", "bar", "L", "kn", "nm"};

static lv_obj_t *field(lv_obj_t *area, const char *label, lv_color_t col, bool tint, const char *fail, int32_t x, int32_t y,
                       int32_t fw, int32_t fh) {
  lv_obj_t *r = ui_readout_create(area, label, "", 0);
  lv_obj_set_size(r, fw, fh);
  lv_obj_set_pos(r, x, y);
  if (tint) ui_readout_set_color(r, col);
  ui_field_set_fault_text(r, fail);
  return r;
}

void navigation_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b) {
  (void)page;
  (void)b;
  memset(&w, 0, sizeof w);
  nav_ids_bind(&w.ids);
  lv_obj_update_layout(area);
  int32_t W = lv_obj_get_width(area), H = lv_obj_get_height(area);
  const ui_colors_t *c = ui_theme();
  w.wide = sc == SIZE_WIDE;
  int32_t pad = 4;

  // The wide rose sits 30 px down: the heading box above it must not cover the autopilot bug on the rim.
  int32_t side = w.wide ? H - 30 : 376, colw, rx;
  w.rose = ui_rose_create(area);
  lv_obj_set_size(w.rose, side, side);
  rx = (W - side) / 2;
  lv_obj_set_pos(w.rose, rx, w.wide ? 30 : 36);
  colw = w.wide ? rx - 2 * pad : 108;
  int32_t rcol = W - pad - colw;

  int32_t big = w.wide ? 88 : 64, mid = w.wide ? 56 : 44;
  w.awa = field(area, "APPARENT", c->nav_app, true, "AWA FAIL", pad, pad, colw, big);
  w.aws = field(area, "AWS", c->nav_app, true, "AWS FAIL", pad, pad + big + 4, colw, mid);
  int32_t bottom = H - pad - (w.wide ? 56 : 40);
  w.sog = field(area, "SOG", c->fg, false, "SOG FAIL", pad, bottom, colw, w.wide ? 56 : 40);
  w.set_drift = lv_label_create(area);
  lv_obj_set_style_text_font(w.set_drift, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(w.set_drift, c->dim, 0);
  lv_label_set_long_mode(w.set_drift, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(w.set_drift, colw);
  lv_obj_set_pos(w.set_drift, pad, bottom - (w.wide ? 22 : 38));
  int32_t true_y = w.wide ? 164 : bottom - 34 - mid - 4 - big - 4;
  w.twa = field(area, "TRUE", c->nav_true, true, "TWA FAIL", pad, true_y, colw, big);
  w.tws = field(area, "TWS", c->nav_true, true, "TWS FAIL", pad, true_y + big + 4, colw, mid);

  w.course = field(area, "COURSE", c->nav_course, true, "CRS FAIL", rcol, pad, colw, big);
  w.xte = field(area, "XTE", c->nav_course, true, "XTE FAIL", rcol, pad + big + 4, colw, mid);
  w.dtw = field(area, "DTW", c->nav_course, true, "DTW FAIL", rcol, pad + big + mid + 8, colw, mid);
  w.ap_hdg = field(area, "AP HDG", c->accent, true, "AP FAIL", rcol, bottom, colw, w.wide ? 56 : 40);
  lv_obj_set_style_border_color(w.ap_hdg, c->accent, 0);
  lv_obj_set_style_border_width(w.ap_hdg, 2, 0);

  w.heading = field(area, "HDG", c->fg, false, "HDG FAIL", (W - 104) / 2, 0, 104, 40);
  lv_obj_set_style_bg_color(w.heading, c->bg, 0);
  lv_obj_set_style_bg_opa(w.heading, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(w.heading, c->fg, 0);
  lv_obj_set_style_border_width(w.heading, 2, 0);
  ui_readout_set_centered(w.heading, true);

  w.ap_badge = lv_obj_create(area);
  lv_obj_remove_style_all(w.ap_badge);
  lv_obj_set_size(w.ap_badge, 56, 32);
  lv_obj_set_pos(w.ap_badge, (W - 104) / 2 + 104 + 8, 4);
  lv_obj_set_style_bg_color(w.ap_badge, c->ok, 0);
  lv_obj_set_style_bg_opa(w.ap_badge, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(w.ap_badge, 6, 0);
  lv_obj_t *l = lv_label_create(w.ap_badge);
  lv_label_set_text(l, "AP");
  lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(l, c->bg, 0);
  lv_obj_center(l);
  w.built = true;
}

lv_obj_t *navigation_page_part(const char *name) {
  if (!w.built) return NULL;
  static const char *const names[] = {"rose", "heading", "ap_badge", "awa", "aws", "twa", "tws", "sog", "course", "xte", "dtw", "ap_hdg", "set_drift"};
  lv_obj_t *const parts[] = {w.rose, w.heading, w.ap_badge, w.awa, w.aws, w.twa, w.tws, w.sog, w.course, w.xte, w.dtw, w.ap_hdg, w.set_drift};
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
    if (!strcmp(names[i], name)) return parts[i];
  return NULL;
}

const ui_rose_data_t *navigation_page_rose_data(void) { return &w.rd; }

static void show(lv_obj_t *o, bool on) {
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static int degrees(double rad) {
  int d = (int)lround(rad * 180.0 / M_PI) % 360;
  return d < 0 ? d + 360 : d;
}

static void bearing_text(char *o, size_t n, double rad) { snprintf(o, n, "%03d\xC2\xB0", degrees(rad)); }

static void angle_text(char *o, size_t n, double rad) {
  int deg = (int)lround(fabs(rad) * 180.0 / M_PI);
  snprintf(o, n, "%d\xC2\xB0%s", deg, deg == 0 || deg == 180 ? "" : rad < 0 ? " PORT" : " STBD");
}

static void speed_text(char *o, size_t n, double si, const cfg_units_t *u) {
  snprintf(o, n, "%.1f %s", units_from_si(Q_SPEED, si, u), units_label(Q_SPEED, u));
}

// Distances: nautical miles show decimals (0.12 nm across the track, 7.4 nm to go); metres and feet are whole numbers.
static void distance_text(char *o, size_t n, double si, int nm_decimals, const cfg_units_t *u) {
  const char *unit = units_label(Q_DISTANCE, u);
  snprintf(o, n, "%.*f %s", !strcmp(unit, "nm") ? nm_decimals : 0, units_from_si(Q_DISTANCE, si, u), unit);
}

void navigation_refresh(role_binding_t *b, int64_t now_ms) {
  if (!w.built) return;
  const cfg_units_t *u = b && b->units ? b->units : &default_units;
  nav_view_t v;
  nav_view_build(&w.ids, now_ms, &v);
  char t[40];

  if (v.have_heading) bearing_text(t, sizeof t, v.heading);
  ui_readout_set_text(w.heading, v.have_heading ? t : "", v.have_heading);

  angle_text(t, sizeof t, v.awa);
  ui_readout_set_text(w.awa, v.have_awa ? t : "", v.have_awa);
  speed_text(t, sizeof t, v.aws, u);
  ui_readout_set_text(w.aws, v.have_aws ? t : "", v.have_aws);
  bool tw = v.tw_src != NAV_TW_NONE;
  angle_text(t, sizeof t, v.twa);
  ui_readout_set_text(w.twa, tw ? t : "", tw);
  speed_text(t, sizeof t, v.tws, u);
  ui_readout_set_text(w.tws, tw ? t : "", tw);
  show(w.twa, tw);
  show(w.tws, tw);
  speed_text(t, sizeof t, v.sog, u);
  ui_readout_set_text(w.sog, v.have_sog ? t : "", v.have_sog);

  show(w.course, v.have_course);
  if (v.have_course) bearing_text(t, sizeof t, v.dtk);
  ui_readout_set_text(w.course, v.have_course ? t : "", v.have_course);
  bool have_xte = v.have_course && !isnan(v.xte), have_dtw = v.have_course && !isnan(v.dtw) && w.wide;
  show(w.xte, have_xte);
  if (have_xte) {
    char d[24];
    distance_text(d, sizeof d, fabs(v.xte), 2, u);
    snprintf(t, sizeof t, "%s %c", d, v.xte >= 0 ? 'R' : 'L');
    ui_readout_set_text(w.xte, t, true);
  }
  show(w.dtw, have_dtw);
  if (have_dtw) {
    distance_text(t, sizeof t, v.dtw, 1, u);
    ui_readout_set_text(w.dtw, t, true);
  }

  show(w.ap_badge, v.ap_engaged);
  show(w.ap_hdg, v.have_ap_target);
  if (v.have_ap_target) {
    bearing_text(t, sizeof t, v.ap_target);
    ui_readout_set_text(w.ap_hdg, t, true);
  }

  show(w.set_drift, v.have_current);
  if (v.have_current) {
    char sp[16];
    speed_text(sp, sizeof sp, v.drift, u);
    snprintf(t, sizeof t, "SET %03d\xC2\xB0 DRIFT %s", degrees(v.set), sp);
    lv_label_set_text(w.set_drift, t);
  }

  ui_rose_data_t rd;
  memset(&rd, 0, sizeof rd);
  rd.xte_m = NAN;
  rd.have_heading = v.have_heading;
  rd.heading = v.heading;
  rd.course = v.have_course;
  rd.dtk = v.dtk;
  rd.xte_m = v.xte;
  rd.have_cog = v.have_cog;
  rd.cog = v.cog;
  rd.ap_bug = v.have_ap_target;
  rd.ap_target = v.ap_target;
  rd.have_awa = v.have_awa;
  rd.awa = v.awa;
  rd.have_twa = tw;
  rd.twa = v.twa;
  if (memcmp(&rd, &w.rd, sizeof rd)) {
    w.rd = rd;
    ui_rose_set(w.rose, &rd);
  }
}
