// The Depth page and its Glance tile: one page, depth large with what it is measured from named above it, and
// the tide below when a tide plugin supplies one. Tide is information only: it never raises a state.
#include <stdio.h>
#include <string.h>
#include "depth_page.h"
#include "depth_state.h"
#include "history.h"
#include "platform.h"
#include "ui_gauge.h"

// What the role knows now, shared by the page and the tile.
typedef struct {
  depth_ref_t ref;
  double depth_m;
  bd_id_t depth_id;           // the path shown; BD_NONE when there is none
  bool height_ok, high_ok, low_ok;
  double height_m, high_m, low_m;
  bool tide_seen;             // the tide plugin has sent anything
  bd_sample_t time_high, time_low;
  bool has_time_high, has_time_low;
  tide_t tide;
} view_t;

static bool sample(const role_binding_t *b, uint8_t g, uint8_t k, bd_sample_t *out) {
  return b->g[g].ids[0][k] != BD_NONE && bd_get(b->g[g].ids[0][k], out);
}

static bool fresh_number(const role_binding_t *b, uint8_t k, int64_t now_ms, double *m) {
  bd_sample_t s;
  if (!sample(b, G_TIDE, k, &s) || s.kind != BD_NUMBER || !s.valid || !bd_is_fresh(&s, TIDE_HEIGHT_MAX_AGE_MS, now_ms)) return false;
  *m = s.num;
  return true;
}

static void read_view(const role_binding_t *b, int64_t now_ms, view_t *v) {
  memset(v, 0, sizeof *v);
  bd_sample_t d[3];
  const bd_sample_t *p[3];
  for (uint8_t k = 0; k < 3; k++) p[k] = sample(b, G_DEPTH, k, &d[k]) ? &d[k] : NULL;
  v->ref = depth_pick(p, now_ms);
  if (v->ref != DEPTH_NONE) {
    v->depth_m = d[v->ref].num;
    v->depth_id = b->g[G_DEPTH].ids[0][v->ref];
  }
  v->height_ok = fresh_number(b, T_HEIGHT, now_ms, &v->height_m);
  v->high_ok = fresh_number(b, T_HIGH, now_ms, &v->high_m);
  v->low_ok = fresh_number(b, T_LOW, now_ms, &v->low_m);
  v->has_time_high = sample(b, G_TIDE, T_TIME_HIGH, &v->time_high);
  v->has_time_low = sample(b, G_TIDE, T_TIME_LOW, &v->time_low);
  for (uint8_t k = 0; k < 5 && !v->tide_seen; k++) {
    bd_sample_t s;
    v->tide_seen = sample(b, G_TIDE, k, &s) && (s.valid || s.rx_ms != 0);
  }
  v->tide = tide_assess(v->has_time_high ? &v->time_high : NULL, v->has_time_low ? &v->time_low : NULL, bd_newest_source_time());
}

static void fmt_depth(const view_t *v, const cfg_units_t *u, char *out, size_t n) {
  double d = units_from_si(Q_DEPTH, v->depth_m, u);
  snprintf(out, n, d < 100 ? "%.1f" : "%.0f", d);
}

static void fmt_height(double m, const cfg_units_t *u, char *out, size_t n) {
  snprintf(out, n, "%.1f %s", units_from_si(Q_DEPTH, m, u), units_label(Q_DEPTH, u));
}

// "RISING 1.8 m to HW 14:32", "RISING to HW 14:32", "TIDE 1.8 m", or "" when nothing is known.
static void tide_line(const view_t *v, const cfg_units_t *u, char *out, size_t n) {
  char h[16] = "", hhmm[8];
  if (v->height_ok) fmt_height(v->height_m, u, h, sizeof h);
  if (v->tide.dir == TIDE_UNKNOWN) {
    snprintf(out, n, "%s%s", h[0] ? "TIDE " : "", h);
    return;
  }
  depth_format_hhmm(v->tide.next_s, depth_utc_offset(), hhmm);
  snprintf(out, n, "%s%s%s to %s %s", v->tide.dir == TIDE_RISING ? "RISING" : "FALLING", h[0] ? " " : "", h,
           v->tide.next_high ? "HW" : "LW", hhmm);
}

// The page's tide headline: "RISING  1.8 m", "1.8 m" when the direction is not known, "" when nothing is.
static void tide_now(const view_t *v, const cfg_units_t *u, char *out, size_t n) {
  char h[16] = "";
  if (v->height_ok) fmt_height(v->height_m, u, h, sizeof h);
  const char *d = v->tide.dir == TIDE_RISING ? "RISING" : v->tide.dir == TIDE_FALLING ? "FALLING" : "";
  snprintf(out, n, "%s%s%s", d, d[0] && h[0] ? "  " : "", h);
}

// The Glance tile. The state is the worse of the shown reading's zones and alerts and any alert on the other two
// (an alarm on the keel depth still counts while the transducer is shown). Only the shown reading is a number.
void depth_tile(const role_binding_t *b, int64_t now_ms, glance_tile_t *out) {
  out->title = "DEPTH";
  view_t v;
  read_view(b, now_ms, &v);
  for (uint8_t k = 0; k < 3; k++)   // the chart follows whichever reading is shown, so all three are recorded (the tile is asked at bind, before any data)
    if (b->g[G_DEPTH].ids[0][k] != BD_NONE) hist_track(b->g[G_DEPTH].ids[0][k]);
  gt_state_t st[3];
  for (uint8_t k = 0; k < 3; k++) {
    bd_id_t id = b->g[G_DEPTH].ids[0][k];
    st[k] = k == (uint8_t)v.ref ? glance_path_state(id, DEPTH_MAX_AGE_MS, now_ms) : glance_alert_state(id);
  }
  gt_state_t s = glance_worst(st, 3);
  if (v.ref == DEPTH_NONE && s < GT_NO_DATA) s = GT_NO_DATA;   // a missing sounder is never calm; an alarm still outranks it
  glance_tile_state(out, s);
  if (v.ref == DEPTH_NONE) return;
  fmt_depth(&v, b->units, out->headline, sizeof out->headline);
  snprintf(out->unit, sizeof out->unit, "%s", units_label(Q_DEPTH, b->units));
  snprintf(out->context[0], sizeof out->context[0], "%s", depth_ref_label(v.ref));
  tide_line(&v, b->units, out->context[1], sizeof out->context[1]);
  // The chart is the depth, and after now it follows the tide: the depth changes as the water does. Needs both the
  // next high and the next low (time and height); with less the chart simply stops at now.
  out->trend = v.depth_id;
  out->has_forecast = true;
  int64_t th, tl, now_s = bd_newest_source_time();
  if (v.high_ok && v.low_ok && v.has_time_high && v.has_time_low && v.time_high.valid && v.time_low.valid && v.time_high.kind == BD_TEXT && v.time_low.kind == BD_TEXT &&
      depth_parse_time(v.time_high.text, &th) && depth_parse_time(v.time_low.text, &tl) &&
      tide_deltas(v.high_m, th, v.low_m, tl, now_s, GLANCE_FC_MINUTES * 60, out->fc, GLANCE_FC_POINTS)) {
    out->fc_n = GLANCE_FC_POINTS;
    for (int i = 0; i < GLANCE_FC_POINTS; i++) {
      out->fc[i] += (float)v.depth_m;
      if (out->fc[i] < 0) out->fc[i] = 0;
    }
  }
}

// ---- the page ----

PLATFORM_EXT_BSS static struct {
  lv_obj_t *ref, *depth, *tide_head, *tide_now, *row_high, *row_low;
} w;

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *f, lv_color_t col, int32_t x, int32_t y, int32_t width) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, col, 0);
  lv_obj_set_width(l, width);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, "");
  return l;
}

static void set_text(lv_obj_t *l, const char *txt) {
  if (strcmp(lv_label_get_text(l), txt) != 0) lv_label_set_text(l, txt);
}

void depth_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b) {
  (void)page;
  memset(&w, 0, sizeof w);
  lv_obj_update_layout(area);
  const ui_colors_t *c = ui_theme();
  bool wide = sc == SIZE_WIDE;
  int32_t W = lv_obj_get_width(area), H = lv_obj_get_height(area), pad = 8, iw = W - 2 * pad;
  int32_t top_h = H * 9 / 20;

  w.ref = make_label(area, &lv_font_montserrat_20, c->accent, pad, 4, iw);
  w.depth = ui_readout_create(area, "", units_label(Q_DEPTH, b->units), 1);   // the line above names it
  ui_field_set_fault_text(w.depth, "NO DEPTH");
  lv_obj_set_pos(w.depth, pad, 30);
  lv_obj_set_size(w.depth, iw, top_h - 30);

  int32_t y = top_h + 6;
  w.tide_head = make_label(area, &lv_font_montserrat_20, c->accent, pad, y, iw);
  lv_label_set_text(w.tide_head, "TIDE");
  w.tide_now = make_label(area, wide ? &lv_font_montserrat_28 : &lv_font_montserrat_20, c->fg, pad, y + 28, iw);
  int32_t rows_y = y + (wide ? 70 : 60);
  w.row_high = make_label(area, &lv_font_montserrat_20, c->fg, pad, rows_y, iw);
  w.row_low = make_label(area, &lv_font_montserrat_20, c->fg, pad, rows_y + 30, iw);
}

// "HIGH 14:32  4.1 m"; the time or the height shows "--" when only the other is known; "" when neither is.
static void tide_row(const char *name, const bd_sample_t *when, bool has_when, bool h_ok, double h_m, const cfg_units_t *u,
                     char *out, size_t n) {
  int64_t t;
  bool t_ok = has_when && when->kind == BD_TEXT && when->valid && depth_parse_time(when->text, &t);
  if (!t_ok && !h_ok) {
    out[0] = 0;
    return;
  }
  char hhmm[8] = "--:--", h[16] = "--";
  if (t_ok) depth_format_hhmm(t, depth_utc_offset(), hhmm);
  if (h_ok) fmt_height(h_m, u, h, sizeof h);
  snprintf(out, n, "%s  %s  %s", name, hhmm, h);
}

void depth_refresh(role_binding_t *b, int64_t now_ms) {
  if (!w.depth) return;
  view_t v;
  read_view(b, now_ms, &v);
  bool ok = v.ref != DEPTH_NONE;
  ui_readout_set(w.depth, ok ? units_from_si(Q_DEPTH, v.depth_m, b->units) : 0, ok);
  role_field_alert(w.depth, v.depth_id);
  set_text(w.ref, depth_ref_label(v.ref));

  char line[48];
  if (lv_obj_has_flag(w.tide_head, LV_OBJ_FLAG_HIDDEN) == v.tide_seen) {   // the tide block shows only while a plugin supplies one
    lv_obj_t *all[] = {w.tide_head, w.tide_now, w.row_high, w.row_low};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
      if (v.tide_seen) lv_obj_remove_flag(all[i], LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(all[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  tide_now(&v, b->units, line, sizeof line);
  set_text(w.tide_now, line[0] ? line : "NO TIDE DATA");
  tide_row("HIGH", &v.time_high, v.has_time_high, v.high_ok, v.high_m, b->units, line, sizeof line);
  set_text(w.row_high, line);
  tide_row("LOW ", &v.time_low, v.has_time_low, v.low_ok, v.low_m, b->units, line, sizeof line);
  set_text(w.row_low, line);
}
