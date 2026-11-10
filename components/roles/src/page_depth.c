// The Depth page and its Glance tile: one page, depth large with what it is measured from named above it, and
// the tide below when a tide plugin supplies one. The tide never raises an alarm: a forecast that enters a Signal K zone only colours the line and calms the tile to an amber LOW TIDE (advisory); alerts and alarms come from Signal K.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "alerts.h"
#include "depth_page.h"
#include "depth_state.h"
#include "history.h"
#include "platform.h"
#include "trend_data.h"
#include "ui_digits.h"
#include "ui_gauge.h"

// What the role knows now, shared by the page and the tile.
typedef struct {
  depth_ref_t ref;
  double depth_m;
  bd_id_t depth_id;           // the path shown; BD_NONE when there is none
  bool height_ok, high_ok, low_ok;
  double height_m, high_m, low_m;
  bool tide_seen;             // the tide plugin has sent anything
  bool tide_fail;             // the tide region shows TIDE FAIL: Signal K is down, or a tide that was coming has stopped
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
  // Tide comes only from a Signal K plugin; the bus has none. Quietly no tide when no plugin has ever spoken and the server
  // is up (a boat without one), and when Signal K is not in use at all (bus only). A FAIL flag when the server is
  // configured but down, or when a tide that was coming has stopped.
  bool sk_down = bd_source_state(BD_SRC_SK) != BD_SRC_NOT_CONFIGURED && !alerts_source_is_up();
  bool usable = v->height_ok || v->high_ok || v->low_ok || v->tide.dir != TIDE_UNKNOWN;
  v->tide_fail = sk_down || (v->tide_seen && !usable);
}

// At most "99.9" or "999": a sounder does not reach four digits, so a larger number is held at 999 rather than grown.
static void fmt_depth(const view_t *v, const cfg_units_t *u, char *out, size_t n) {
  double d = units_from_si(Q_DEPTH, v->depth_m, u);
  if (d >= 99.95) snprintf(out, n, "%.0f", d > 999 ? 999 : d);
  else snprintf(out, n, "%.1f", d < 0 ? 0 : d);
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
      tide_deltas(v.high_m, th, v.low_m, tl, v.height_ok ? v.height_m : NAN, now_s, GLANCE_FC_MINUTES * 60, out->fc, GLANCE_FC_POINTS)) {
    out->fc_n = GLANCE_FC_POINTS;
    for (int i = 0; i < GLANCE_FC_POINTS; i++) {
      out->fc[i] += (float)v.depth_m;
      if (out->fc[i] < 0) out->fc[i] = 0;
    }
    // Advisory only: a line that enters a zone is drawn in the zone's colour and an otherwise calm tile says LOW TIDE, in
    // amber even for an alarm zone. Only Signal K raises an alarm; its own state and word always win.
    out->line_state = depth_forecast_zone(v.depth_id, out->fc, out->fc_n);
    if (out->line_state && out->state == GT_OK) {
      glance_tile_state(out, GT_WARN);
      snprintf(out->word, sizeof out->word, "LOW TIDE");
    }
  }
}

// ---- the page ----
//
// Top to bottom: what the depth is measured from (and the tide's direction and height at the right), the depth in
// big digits, the chart (6 h of depth, then the tide's forecast of it, on a scale in the display unit), and the next
// high and low with their times.

#define AXIS_W 46             // the gutter for the scale's numbers
#define CHART_SCALE 1000
#define MAX_TICKS 7
#define CHART_REFRESH_MS 5000

PLATFORM_EXT_BSS static struct {
  lv_obj_t *ref, *tide_now, *digits, *unit, *fault, *fault_x, *row_high, *row_low;
  lv_obj_t *chart, *chart_fc, *now_line, *band[2], *tick[MAX_TICKS], *tick_label[MAX_TICKS], *x_left, *x_mid, *x_right;
  lv_chart_series_t *ser, *ser_fc;
  lv_point_precise_t now_pts[2];
  int64_t chart_ms;
  bd_id_t last_trend;
  bool last_fc;
  uint8_t last_line;
} w;

static glance_tile_t page_tile;   // what the chart is drawn from: the same call the Glance tile makes
static trend_data_t page_trend;

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *f, lv_color_t col, int32_t x, int32_t y, int32_t width) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, col, 0);
  if (width > 0) lv_obj_set_width(l, width);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, "");
  return l;
}

static void set_text(lv_obj_t *l, const char *txt) {
  if (strcmp(lv_label_get_text(l), txt) != 0) lv_label_set_text(l, txt);
}

static void show(lv_obj_t *o, bool on) {
  if (!o) return;
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *make_chart(lv_obj_t *area, int32_t x, int32_t y, int32_t cw, int32_t ch, bool forecast, lv_color_t col, const ui_colors_t *c) {
  lv_obj_t *ch_obj = lv_chart_create(area);
  lv_obj_remove_flag(ch_obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(ch_obj, cw, ch);
  lv_obj_set_pos(ch_obj, x, y);
  lv_chart_set_type(ch_obj, LV_CHART_TYPE_LINE);
  lv_chart_set_div_line_count(ch_obj, 0, 0);
  lv_chart_set_axis_range(ch_obj, LV_CHART_AXIS_PRIMARY_Y, 0, CHART_SCALE);
  lv_obj_set_style_bg_opa(ch_obj, forecast ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(ch_obj, c->bg, 0);
  lv_obj_set_style_border_width(ch_obj, 0, 0);
  lv_obj_set_style_pad_all(ch_obj, 2, 0);
  lv_obj_set_style_radius(ch_obj, 2, 0);
  lv_obj_set_style_size(ch_obj, 0, 0, LV_PART_INDICATOR);
  lv_obj_set_style_line_width(ch_obj, 3, LV_PART_ITEMS);
  if (forecast) {
    lv_obj_set_style_line_dash_width(ch_obj, 8, LV_PART_ITEMS);
    lv_obj_set_style_line_dash_gap(ch_obj, 6, LV_PART_ITEMS);
  }
  (void)col;
  return ch_obj;
}

void depth_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b) {
  (void)page;
  memset(&w, 0, sizeof w);
  lv_obj_update_layout(area);
  const ui_colors_t *c = ui_theme();
  bool wide = sc == SIZE_WIDE;
  int32_t W = lv_obj_get_width(area), H = lv_obj_get_height(area), pad = 8, iw = W - 2 * pad;

  w.ref = make_label(area, &lv_font_montserrat_20, c->accent, pad, 4, iw / 2);
  w.tide_now = make_label(area, &lv_font_montserrat_20, c->fg, pad + iw / 2, 4, iw / 2);
  lv_obj_set_style_text_align(w.tide_now, LV_TEXT_ALIGN_RIGHT, 0);

  // The digits: 3 characters and a point at most. The font's line is taller than its digits, so a box the height of
  // the digits holds the label and clips the empty space.
  const lv_font_t *df = wide ? &ui_font_digits_128 : &ui_font_digits_96;
  lv_font_glyph_dsc_t gd;   // where the digits sit in the font's line: the box is just their height
  lv_font_get_glyph_dsc(df, &gd, '0', 0);
  int32_t gtop = (int32_t)lv_font_get_line_height(df) - df->base_line - gd.ofs_y - (int32_t)gd.box_h;
  int32_t dh = (int32_t)gd.box_h + 6, dy = 28;
  lv_obj_t *dbox = lv_obj_create(area);
  lv_obj_remove_flag(dbox, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(dbox, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(dbox, 0, 0);
  lv_obj_set_style_pad_all(dbox, 0, 0);
  lv_obj_set_size(dbox, iw, dh);
  lv_obj_set_pos(dbox, pad, dy);
  w.digits = make_label(dbox, df, c->fg, 0, 3 - gtop, 0);
  lv_label_set_long_mode(w.digits, LV_LABEL_LONG_CLIP);
  w.unit = make_label(dbox, &lv_font_montserrat_28, c->dim, 0, 0, 0);
  w.fault_x = make_label(dbox, &lv_font_montserrat_28, c->fault_cross, 0, dh / 2 - 16, 0);   // the red cross, then the words
  lv_label_set_text(w.fault_x, LV_SYMBOL_CLOSE);
  w.fault = make_label(dbox, &lv_font_montserrat_28, c->fault_label, 34, dh / 2 - 16, 0);
  lv_label_set_text(w.fault, "NO DEPTH");
  show(w.fault, false);
  show(w.fault_x, false);

  // The bottom: the next high and low, side by side on a wide board and one over the other on a square one.
  int32_t rows_h = wide ? 26 : 52, xh = 18;
  int32_t cy = dy + dh + 10, bottom = H - rows_h - 6;
  int32_t ch = bottom - xh - cy;
  if (ch < 40) ch = 40;
  int32_t cx = pad + AXIS_W, cw = iw - AXIS_W;
  for (int i = 0; i < MAX_TICKS; i++) {   // the scale: a faint line and its number, placed when the range is known
    w.tick[i] = lv_obj_create(area);
    lv_obj_remove_flag(w.tick[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(w.tick[i], 0, 0);
    lv_obj_set_style_radius(w.tick[i], 0, 0);
    lv_obj_set_style_pad_all(w.tick[i], 0, 0);
    lv_obj_set_style_bg_color(w.tick[i], c->dim, 0);
    lv_obj_set_style_bg_opa(w.tick[i], LV_OPA_30, 0);
    lv_obj_set_size(w.tick[i], cw, 1);
    lv_obj_set_pos(w.tick[i], cx, cy);
    w.tick_label[i] = make_label(area, &lv_font_montserrat_14, c->dim, pad, cy, AXIS_W - 6);
    lv_obj_set_style_text_align(w.tick_label[i], LV_TEXT_ALIGN_RIGHT, 0);
    show(w.tick[i], false);
    show(w.tick_label[i], false);
  }
  for (int i = 0; i < 2; i++) {   // Signal K's shallow zones, behind the line
    w.band[i] = lv_obj_create(area);
    lv_obj_remove_flag(w.band[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(w.band[i], 0, 0);
    lv_obj_set_style_radius(w.band[i], 0, 0);
    lv_obj_set_style_bg_opa(w.band[i], LV_OPA_30, 0);
    lv_obj_set_size(w.band[i], cw, 10);
    lv_obj_set_pos(w.band[i], cx, cy);
    show(w.band[i], false);
  }
  w.chart = make_chart(area, cx, cy, cw, ch, false, c->accent, c);
  w.ser = lv_chart_add_series(w.chart, c->accent, LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_point_count(w.chart, 2);
  lv_chart_set_all_values(w.chart, w.ser, LV_CHART_POINT_NONE);
  lv_obj_set_style_bg_opa(w.chart, LV_OPA_TRANSP, 0);   // the ticks and zones are behind it
  w.chart_fc = make_chart(area, cx, cy, cw, ch, true, c->accent, c);
  w.ser_fc = lv_chart_add_series(w.chart_fc, c->accent, LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_point_count(w.chart_fc, 2);
  lv_chart_set_all_values(w.chart_fc, w.ser_fc, LV_CHART_POINT_NONE);
  w.now_line = lv_line_create(area);
  lv_line_set_points(w.now_line, w.now_pts, 2);
  lv_obj_set_style_line_width(w.now_line, 1, 0);
  lv_obj_set_style_line_color(w.now_line, c->dim, 0);
  lv_obj_set_style_line_dash_width(w.now_line, 4, 0);
  lv_obj_set_style_line_dash_gap(w.now_line, 4, 0);
  show(w.now_line, false);
  w.x_left = make_label(area, &lv_font_montserrat_14, c->dim, cx, cy + ch + 2, 0);
  w.x_mid = make_label(area, &lv_font_montserrat_14, c->dim, cx, cy + ch + 2, 0);
  w.x_right = make_label(area, &lv_font_montserrat_14, c->dim, cx, cy + ch + 2, 0);

  int32_t ry = H - rows_h;
  w.row_high = make_label(area, &lv_font_montserrat_20, c->fg, pad, ry, wide ? iw / 2 : iw);
  w.row_low = make_label(area, &lv_font_montserrat_20, c->fg, wide ? pad + iw / 2 : pad, wide ? ry : ry + 26, wide ? iw / 2 : iw);
  w.chart_ms = -2 * CHART_REFRESH_MS;
  w.last_trend = BD_NONE;
  (void)b;
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

// The unit sits at the foot of the digits, just right of them.
static void place_unit(void) {
  lv_obj_update_layout(w.digits);
  lv_obj_align_to(w.unit, w.digits, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, -((int32_t)lv_obj_get_height(lv_obj_get_parent(w.digits)) / 8));
}

static void tick_text(char *out, size_t n, double v, double step) {
  if (step < 1) snprintf(out, n, "%.1f", v);
  else snprintf(out, n, "%.0f", v);
}

// Draws the chart on its scale in the display unit: the range is rounded out to whole steps (a step of 1, 2, 5, 10 ...)
// so a tick falls on each round number, and the line's height is then the depth read off the scale.
static void update_chart(role_binding_t *b, int64_t now_ms) {
  memset(&page_tile, 0, sizeof page_tile);
  depth_tile(b, now_ms, &page_tile);
  // The throttle is for redrawing the same data; a new path, a forecast arriving or going, or the line entering a zone draws at once.
  bool fc_now = page_tile.fc_n > 0;
  if (now_ms - w.chart_ms < CHART_REFRESH_MS && page_tile.trend == w.last_trend && fc_now == w.last_fc &&
      page_tile.line_state == w.last_line)
    return;
  w.chart_ms = now_ms;
  w.last_trend = page_tile.trend;
  w.last_fc = fc_now;
  w.last_line = page_tile.line_state;
  bool have = page_tile.trend != BD_NONE && trend_build(&page_tile, true, &page_trend);
  show(w.chart, have);
  show(w.chart_fc, have);
  if (!have) {
    show(w.now_line, false);
    for (int i = 0; i < MAX_TICKS; i++) show(w.tick[i], false), show(w.tick_label[i], false);
    for (int i = 0; i < 2; i++) show(w.band[i], false);
    set_text(w.x_left, "");
    set_text(w.x_mid, "");
    set_text(w.x_right, "");
    return;
  }
  trend_data_t *d = &page_trend;
  double lo_d = units_from_si(Q_DEPTH, d->lo, b->units), hi_d = units_from_si(Q_DEPTH, d->hi, b->units);
  if (lo_d < 0) lo_d = 0;
  static const double steps[] = {0.5, 1, 2, 5, 10, 20, 50, 100, 200};
  double step = steps[0];
  for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
    step = steps[i];
    if ((hi_d - lo_d) / step <= MAX_TICKS - 2) break;
  }
  float flo = (float)lo_d, fhi = (float)hi_d;
  trend_round_range(&flo, &fhi, (float)step);
  lo_d = flo;
  hi_d = fhi;
  float lo = (float)units_to_si(Q_DEPTH, lo_d, b->units), hi = (float)units_to_si(Q_DEPTH, hi_d, b->units);
  int32_t ch = lv_obj_get_height(w.chart), cx = lv_obj_get_x(w.chart), cy = lv_obj_get_y(w.chart), cw = lv_obj_get_width(w.chart);
  int32_t inner = ch - 4;   // the plot area inside the chart's 2 px padding, where the series is scaled
  lv_chart_set_point_count(w.chart, (uint32_t)d->total);
  lv_chart_set_point_count(w.chart_fc, (uint32_t)d->total);
  for (size_t i = 0; i < d->total; i++) {
    int32_t y = isnan(d->v[i]) ? LV_CHART_POINT_NONE : (int32_t)((d->v[i] - lo) / (hi - lo) * CHART_SCALE);
    lv_chart_set_series_value_by_id(w.chart, w.ser, (uint32_t)i, i >= d->p ? LV_CHART_POINT_NONE : y);
    lv_chart_set_series_value_by_id(w.chart_fc, w.ser_fc, (uint32_t)i, d->fc && i + 1 >= d->p ? y : LV_CHART_POINT_NONE);
  }
  lv_color_t lc = ui_zone_line_color(page_tile.line_state, ui_theme()->accent);   // the whole line, history too
  lv_chart_set_series_color(w.chart, w.ser, lc);
  lv_chart_set_series_color(w.chart_fc, w.ser_fc, lc);
  lv_chart_refresh(w.chart);
  lv_chart_refresh(w.chart_fc);
  // the scale
  int nt = 0;
  for (double v = lo_d; v <= hi_d + step / 2 && nt < MAX_TICKS; v += step, nt++) {
    int32_t y = cy + 2 + (int32_t)((1.0 - (v - lo_d) / (hi_d - lo_d)) * (double)inner);
    char buf[12];
    tick_text(buf, sizeof buf, v, step);
    set_text(w.tick_label[nt], buf);
    lv_obj_set_y(w.tick[nt], y);
    lv_obj_set_y(w.tick_label[nt], y - 8);
    show(w.tick[nt], true);
    show(w.tick_label[nt], true);
  }
  for (int i = nt; i < MAX_TICKS; i++) show(w.tick[i], false), show(w.tick_label[i], false);
  // Signal K's warn and alarm zones the range touches
  bd_meta_t m;
  int nb = 0;
  if (bd_get_meta(page_tile.trend, &m))
    for (uint8_t z = 0; z < m.zone_count && nb < 2; z++) {
      if (m.zones[z].state < 2 || m.zones[z].upper <= lo || m.zones[z].lower >= hi) continue;
      int32_t by, bh;
      trend_band(lo, hi, (float)m.zones[z].lower, (float)m.zones[z].upper, inner, &by, &bh);
      lv_obj_set_y(w.band[nb], cy + 2 + by);
      lv_obj_set_height(w.band[nb], bh);
      lv_obj_set_style_bg_color(w.band[nb], m.zones[z].state >= 3 ? ui_theme()->alarm : ui_theme()->warn, 0);
      show(w.band[nb++], true);
    }
  for (int i = nb; i < 2; i++) show(w.band[i], false);
  // the time axis
  show(w.now_line, d->fc);
  if (d->fc) {
    int32_t nx = cx + 2 + (cw - 4) * (int32_t)(d->p - 1) / (int32_t)(d->total - 1);
    lv_obj_set_pos(w.now_line, nx, cy);
    w.now_pts[1].y = ch;
    lv_line_set_points(w.now_line, w.now_pts, 2);
    set_text(w.x_left, "-6 h");
    set_text(w.x_mid, "now");
    set_text(w.x_right, "+6 h");
    lv_obj_update_layout(w.x_mid);
    lv_obj_set_x(w.x_mid, nx - lv_obj_get_width(w.x_mid) / 2);
    lv_obj_update_layout(w.x_right);
    lv_obj_set_x(w.x_right, cx + cw - lv_obj_get_width(w.x_right));
  } else {
    char buf[16];
    if (d->minutes >= 120) snprintf(buf, sizeof buf, "-%u h", (unsigned)((d->minutes + 59) / 60));
    else snprintf(buf, sizeof buf, "-%u min", (unsigned)d->minutes);
    set_text(w.x_left, buf);
    set_text(w.x_mid, "");
    set_text(w.x_right, "now");
    lv_obj_update_layout(w.x_right);
    lv_obj_set_x(w.x_right, cx + cw - lv_obj_get_width(w.x_right));
  }
}

void depth_refresh(role_binding_t *b, int64_t now_ms) {
  if (!w.digits) return;
  view_t v;
  read_view(b, now_ms, &v);
  bool ok = v.ref != DEPTH_NONE;
  char line[48];
  if (ok) {
    fmt_depth(&v, b->units, line, sizeof line);
    set_text(w.digits, line);
  }
  show(w.digits, ok);
  show(w.unit, ok);
  show(w.fault, !ok);
  show(w.fault_x, !ok);
  if (ok) {
    set_text(w.unit, units_label(Q_DEPTH, b->units));
    place_unit();
  }
  uint8_t as = v.depth_id != BD_NONE && bd_path(v.depth_id) ? (uint8_t)alerts_for_path(bd_path(v.depth_id)) : 0;
  const ui_colors_t *c = ui_theme();
  lv_obj_set_style_text_color(w.digits, as >= 3 ? c->alarm : as >= 1 ? c->warn : c->fg, 0);
  set_text(w.ref, depth_ref_label(v.ref));

  tide_now(&v, b->units, line, sizeof line);
  set_text(w.tide_now, v.tide_fail ? "TIDE FAIL" : line[0] ? line : "NO TIDE DATA");
  lv_obj_set_style_text_color(w.tide_now, v.tide_fail ? c->fault_label : c->fg, 0);
  show(w.tide_now, v.tide_seen || v.tide_fail);
  show(w.row_high, v.tide_seen && !v.tide_fail);
  show(w.row_low, v.tide_seen && !v.tide_fail);
  tide_row("HIGH", &v.time_high, v.has_time_high, v.high_ok, v.high_m, b->units, line, sizeof line);
  set_text(w.row_high, line);
  tide_row("LOW ", &v.time_low, v.has_time_low, v.low_ok, v.low_m, b->units, line, sizeof line);
  set_text(w.row_low, line);
  update_chart(b, now_ms);
}
