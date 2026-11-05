// The Glance page: one tile per role that has one. A tile is an edge in the state colour, a title, a state word,
// a headline (or tank bars), up to two context lines and a trend drawn from the history store. A tile with
// missing data takes the fault style: no number, a red cross where it would be, and NO DATA.
// The page knows nothing about any role: everything comes from the role's glance_tile_t.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "glance_page.h"
#include "history.h"
#include "platform.h"
#include "ui_swing.h"
#include "ui_shell.h"

#define MAX_SOURCES 12
#define CHART_POINTS 144          // 12 h at 5 min a point
#define CHART_REFRESH_MS 5000
#define CHART_SCALE 1000          // the chart plots each series scaled to 0..CHART_SCALE
#define MAX_BANDS 2
#define FC_HIST_SLOTS (6 * 60 / GLANCE_FC_MINUTES)   // with a forecast the chart is 6 h back and 6 h on

typedef struct {
  uint8_t source;               // index into g.src
  glance_tile_t last;           // what the widgets show now
  bool big_headline;
  int64_t chart_ms;             // when the chart was last drawn
  uint16_t points;
  lv_obj_t *tile, *edge, *title, *word, *headline, *headline_b, *unit, *ctx[2], *fault[2];
  lv_obj_t *chart, *chart_fc, *now_line, *band[MAX_BANDS], *axis_l, *axis_r, *swing;
  lv_chart_series_t *ser, *ser_fc;
  lv_point_precise_t now_pts[2];   // the vertical at "now"
  lv_obj_t *bar[3], *bar_label[3], *bar_pct[3];
} tile_w_t;

PLATFORM_EXT_BSS static struct {
  glance_source_t src[MAX_SOURCES];
  uint8_t n;
  tile_w_t t[GLANCE_MAX_TILES];
  uint8_t shown;
  size_t picked[GLANCE_MAX_TILES];
  size_class_t sc;
  int32_t chart_h;
} g;

PLATFORM_EXT_BSS static float series[HIST_MINUTES];

void glance_set_sources(const glance_source_t *src, uint8_t n) {
  g.n = n > MAX_SOURCES ? MAX_SOURCES : n;
  memcpy(g.src, src, g.n * sizeof g.src[0]);
}

// ---- which tiles fit ----

static int rank_of(const char *role) {
  static const char *const order[] = {"anchor", "depth", "navigation", "power", "engine", "tanks"};
  for (size_t i = 0; i < sizeof order / sizeof order[0]; i++)
    if (!strcmp(order[i], role)) return (int)i;
  return (int)(sizeof order / sizeof order[0]);   // any other role ranks last
}

size_t glance_pick(const char *const *role_names, const gt_state_t *states, size_t n, size_t max, size_t *out) {
  bool keep[MAX_SOURCES];
  size_t count = n > MAX_SOURCES ? MAX_SOURCES : n;
  for (size_t i = 0; i < count; i++) keep[i] = true;
  size_t left = count;
  while (left > max) {
    // Drop the lowest-priority tile in the quietest class that has one: quiet, then alerting, then ALARM.
    int victim = -1, worst_class = -1, worst_rank = -1;
    for (size_t i = 0; i < count; i++) {
      if (!keep[i]) continue;
      int cls = states[i] == GT_ALARM ? 0 : states[i] > GT_OK ? 1 : 2;   // higher = quieter = dropped first
      int rk = rank_of(role_names[i]);
      if (cls > worst_class || (cls == worst_class && rk >= worst_rank)) victim = (int)i, worst_class = cls, worst_rank = rk;
    }
    keep[victim] = false;
    left--;
  }
  size_t k = 0;
  for (size_t i = 0; i < count; i++)
    if (keep[i]) out[k++] = i;
  return k;
}

// ---- drawing ----

static lv_color_t state_color(gt_state_t s) {
  const ui_colors_t *c = ui_theme();
  switch (s) {
    case GT_OFF: return c->dim;
    case GT_OK: return c->ok;
    case GT_ALERT:
    case GT_WARN: return c->warn;
    case GT_NO_DATA: return c->fault_label;
    default: return c->alarm;
  }
}

static void set_text(lv_obj_t *label, const char *txt) {
  if (strcmp(lv_label_get_text(label), txt) != 0) lv_label_set_text(label, txt);
}
static void show(lv_obj_t *o, bool on) {
  if (!o || on != lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;   // no change: no invalidation
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *f, lv_color_t col, int32_t x, int32_t y) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, col, 0);
  lv_label_set_text(l, "");
  lv_obj_set_pos(l, x, y);
  return l;
}

static lv_obj_t *box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t col) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(o, w, h);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_style_bg_color(o, col, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

static void goto_role(void *role) { ui_shell_goto((uint8_t)(uintptr_t)role, 0); }
static void on_tile(lv_event_t *e) {
  // The shell rebuilds this page on a move, deleting the tile whose event this is: move after the event.
  lv_async_call(goto_role, lv_event_get_user_data(e));
}

static void build_tile(lv_obj_t *area, tile_w_t *w, uint8_t slot, int32_t x, int32_t y, int32_t tw, int32_t th) {
  const ui_colors_t *c = ui_theme();
  bool wide = g.sc == SIZE_WIDE;
  const glance_source_t *s = &g.src[w->source];
  glance_tile_t t = {0};
  s->b->def->tile(s->b, platform_millis(), &t);
  (void)slot;

  w->tile = lv_obj_create(area);
  lv_obj_remove_style_all(w->tile);
  lv_obj_remove_flag(w->tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(w->tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(w->tile, tw, th);
  lv_obj_set_pos(w->tile, x, y);
  lv_obj_set_style_bg_color(w->tile, c->strip_bg, 0);
  lv_obj_set_style_bg_opa(w->tile, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(w->tile, 8, 0);
  lv_obj_add_event_cb(w->tile, on_tile, LV_EVENT_CLICKED, (void *)(uintptr_t)s->role_index);

  w->edge = box(w->tile, 0, 0, 6, th, c->ok);
  lv_obj_set_style_radius(w->edge, 3, 0);
  int32_t pad = wide ? 16 : 14;
  w->title = label(w->tile, &lv_font_montserrat_14, c->dim, pad, wide ? 10 : 8);
  w->word = label(w->tile, &lv_font_montserrat_14, c->ok, 0, wide ? 10 : 8);
  lv_obj_align(w->word, LV_ALIGN_TOP_RIGHT, -12, wide ? 10 : 8);

  int32_t head_y = wide ? 30 : 28;
  // A tile with a plan view puts the plan view in a square at the left and its text in a column to the right.
  int32_t swing_side = t.has_swing ? (tw * 2 / 5 < th - head_y - 10 ? tw * 2 / 5 : th - head_y - 10) : 0;
  int32_t text_x = t.has_swing ? pad + swing_side + 10 : pad;
  int32_t text_w = t.has_swing ? tw - text_x - pad : 0;
  int32_t ctx_y = wide ? 92 : 66;
  int32_t chart_top = wide ? 130 : 86;
  if (t.bar_count) {
    int32_t rows_h = wide ? (th - head_y - 46) / 3 : 26;
    if (rows_h > 46) rows_h = 46;
    static const lv_color_t none = {0};
    (void)none;
    for (uint8_t i = 0; i < 3 && i < 3; i++) {
      int32_t ry = head_y + i * rows_h;
      w->bar_label[i] = label(w->tile, &lv_font_montserrat_14, c->dim, pad, ry + (rows_h - 18) / 2);
      w->bar[i] = lv_bar_create(w->tile);
      lv_obj_remove_flag(w->bar[i], LV_OBJ_FLAG_CLICKABLE);
      lv_bar_set_range(w->bar[i], 0, 100);
      lv_obj_set_size(w->bar[i], tw - pad - 36 - (wide ? 70 : 56), rows_h > 30 ? 20 : 16);
      lv_obj_set_pos(w->bar[i], pad + 22, ry + (rows_h - (rows_h > 30 ? 20 : 16)) / 2);
      lv_obj_set_style_bg_color(w->bar[i], c->bg, 0);
      lv_obj_set_style_bg_opa(w->bar[i], LV_OPA_COVER, 0);
      lv_obj_set_style_radius(w->bar[i], 2, 0);
      lv_obj_set_style_radius(w->bar[i], 2, LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(w->bar[i], i == 0 ? c->accent : i == 1 ? c->fg : c->dim, LV_PART_INDICATOR);
      lv_obj_set_style_bg_opa(w->bar[i], LV_OPA_COVER, LV_PART_INDICATOR);
      w->bar_pct[i] = label(w->tile, wide ? &lv_font_montserrat_20 : &lv_font_montserrat_14, c->fg, 0, 0);
      lv_obj_align(w->bar_pct[i], LV_ALIGN_TOP_RIGHT, -12, ry + (rows_h - (wide ? 24 : 18)) / 2);
    }
    ctx_y = head_y + 3 * rows_h + 6;
  } else {
    // No bold face is built in: the headline is drawn twice, a pixel apart, which thickens every stroke.
    w->headline_b = label(w->tile, wide ? &lv_font_montserrat_48 : &lv_font_montserrat_28, c->fg, text_x + 1, head_y);
    w->headline = label(w->tile, wide ? &lv_font_montserrat_48 : &lv_font_montserrat_28, c->fg, text_x, head_y);
    w->unit = label(w->tile, wide ? &lv_font_montserrat_20 : &lv_font_montserrat_14, c->dim, text_x, head_y);
    for (int i = 0; i < 2; i++) {   // the red cross over the value area, shown only while the data is missing
      static const lv_point_precise_t down[] = {{0, 0}, {70, 40}}, up[] = {{0, 40}, {70, 0}}, sdown[] = {{0, 0}, {50, 28}}, sup[] = {{0, 28}, {50, 0}};
      w->fault[i] = lv_line_create(w->tile);
      lv_line_set_points(w->fault[i], wide ? (i ? up : down) : (i ? sup : sdown), 2);
      lv_obj_set_style_line_width(w->fault[i], 4, 0);
      lv_obj_set_style_line_color(w->fault[i], c->fault_cross, 0);
      lv_obj_set_pos(w->fault[i], pad, head_y + (wide ? 8 : 2));
      lv_obj_add_flag(w->fault[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (t.has_swing) ctx_y = head_y + (wide ? 58 : 34);
  w->ctx[0] = label(w->tile, &lv_font_montserrat_14, c->fg, text_x, ctx_y);
  if (wide) w->ctx[1] = label(w->tile, &lv_font_montserrat_14, c->dim, text_x, ctx_y + (t.has_swing ? 38 : 19));
  for (int i = 0; i < 2 && t.has_swing; i++) {
    if (!w->ctx[i]) continue;
    lv_obj_set_width(w->ctx[i], text_w);
    lv_label_set_long_mode(w->ctx[i], LV_LABEL_LONG_WRAP);
  }

  if (t.trend != BD_NONE && !t.bar_count) {
    int32_t axis_h = wide ? 22 : 0;
    int32_t ch = th - chart_top - axis_h - (wide ? 4 : 8);
    if (ch > (wide ? 120 : 80)) ch = wide ? 120 : 80;
    if (ch < 24) ch = 24;
    int32_t cy = th - axis_h - (wide ? 4 : 8) - ch;
    int32_t cw = tw - 2 * pad;
    for (int i = 0; i < MAX_BANDS; i++) {
      w->band[i] = box(w->tile, pad, cy, cw, 10, c->warn);
      lv_obj_set_style_bg_opa(w->band[i], LV_OPA_30, 0);
      lv_obj_add_flag(w->band[i], LV_OBJ_FLAG_HIDDEN);
    }
    w->chart = lv_chart_create(w->tile);
    lv_obj_remove_flag(w->chart, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(w->chart, cw, ch);
    lv_obj_set_pos(w->chart, pad, cy);
    lv_chart_set_type(w->chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(w->chart, 0, 0);
    lv_chart_set_axis_range(w->chart, LV_CHART_AXIS_PRIMARY_Y, 0, CHART_SCALE);
    lv_obj_set_style_bg_color(w->chart, c->bg, 0);
    lv_obj_set_style_bg_opa(w->chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(w->chart, 0, 0);
    lv_obj_set_style_pad_all(w->chart, 2, 0);
    lv_obj_set_style_radius(w->chart, 2, 0);
    lv_obj_set_style_size(w->chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(w->chart, 2, LV_PART_ITEMS);
    w->ser = lv_chart_add_series(w->chart, c->accent, LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_point_count(w->chart, 2);
    lv_chart_set_all_values(w->chart, w->ser, LV_CHART_POINT_NONE);
    if (t.has_forecast) {   // the forecast is a second chart over the first, dashed; it shares the range and the x scale
      w->chart_fc = lv_chart_create(w->tile);
      lv_obj_remove_flag(w->chart_fc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_size(w->chart_fc, cw, ch);
      lv_obj_set_pos(w->chart_fc, pad, cy);
      lv_chart_set_type(w->chart_fc, LV_CHART_TYPE_LINE);
      lv_chart_set_div_line_count(w->chart_fc, 0, 0);
      lv_chart_set_axis_range(w->chart_fc, LV_CHART_AXIS_PRIMARY_Y, 0, CHART_SCALE);
      lv_obj_set_style_bg_opa(w->chart_fc, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(w->chart_fc, 0, 0);
      lv_obj_set_style_pad_all(w->chart_fc, 2, 0);
      lv_obj_set_style_size(w->chart_fc, 0, 0, LV_PART_INDICATOR);
      lv_obj_set_style_line_width(w->chart_fc, 2, LV_PART_ITEMS);
      lv_obj_set_style_line_dash_width(w->chart_fc, 6, LV_PART_ITEMS);
      lv_obj_set_style_line_dash_gap(w->chart_fc, 5, LV_PART_ITEMS);
      w->ser_fc = lv_chart_add_series(w->chart_fc, c->accent, LV_CHART_AXIS_PRIMARY_Y);
      lv_chart_set_point_count(w->chart_fc, 2);
      lv_chart_set_all_values(w->chart_fc, w->ser_fc, LV_CHART_POINT_NONE);
      w->now_line = lv_line_create(w->tile);   // a dashed vertical at "now", the middle of the chart
      lv_line_set_points(w->now_line, w->now_pts, 2);
      lv_obj_set_style_line_width(w->now_line, 1, 0);
      lv_obj_set_style_line_color(w->now_line, c->dim, 0);
      lv_obj_set_style_line_dash_width(w->now_line, 4, 0);
      lv_obj_set_style_line_dash_gap(w->now_line, 4, 0);
      lv_obj_add_flag(w->now_line, LV_OBJ_FLAG_HIDDEN);
    }
    if (wide) {
      w->axis_l = label(w->tile, &lv_font_montserrat_14, c->dim, pad, th - 20);
      w->axis_r = label(w->tile, &lv_font_montserrat_14, c->dim, 0, 0);
      lv_obj_align(w->axis_r, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
      lv_label_set_text(w->axis_r, t.has_forecast ? "now " LV_SYMBOL_RIGHT " +6 h" : "now");
    }
  }
  if (t.has_swing) {   // the Anchor's plan view takes the trend's place: a square in the middle of it, hidden until it has data
    int32_t side = swing_side < 24 ? 24 : swing_side;
    w->swing = ui_swing_create(w->tile);
    lv_obj_set_size(w->swing, side, side);
    lv_obj_set_pos(w->swing, pad - 4, head_y + (th - head_y - side) / 2);
    lv_obj_add_flag(w->swing, LV_OBJ_FLAG_HIDDEN);
  }
  w->chart_ms = -2 * CHART_REFRESH_MS;   // draws on the first update
  memset(&w->last, 0, sizeof w->last);
  w->last.state = (gt_state_t)255;   // nothing drawn yet: the first update draws everything
  w->big_headline = false;
}

static void clear_chart(tile_w_t *w) {
  lv_chart_set_point_count(w->chart, 2);
  lv_chart_set_all_values(w->chart, w->ser, LV_CHART_POINT_NONE);
  if (w->chart_fc) {
    lv_chart_set_point_count(w->chart_fc, 2);
    lv_chart_set_all_values(w->chart_fc, w->ser_fc, LV_CHART_POINT_NONE);
    show(w->now_line, false);
  }
  w->points = 0;
  for (int i = 0; i < MAX_BANDS; i++) show(w->band[i], false);
  if (w->axis_l) set_text(w->axis_l, "");
}

// How far past the data's range a warn or alarm zone is drawn: a sliver of it shows even when the data is nowhere
// near, so the chart says where trouble is. Not when the zone is so far away it would flatten the line.
#define ZONE_SLIVER 0.15f
#define ZONE_REACH 2.0f

static void update_chart(tile_w_t *w, const glance_tile_t *t, int64_t now_ms) {
  if (!w->chart || now_ms - w->chart_ms < CHART_REFRESH_MS) return;
  w->chart_ms = now_ms;
  size_t n = hist_read(t->trend, HIST_MINUTES, series);
  if (n < 2) {
    clear_chart(w);
    return;
  }
  bool fc = w->chart_fc && t->fc_n > 0;
  size_t p = fc ? FC_HIST_SLOTS : (n < CHART_POINTS ? n : CHART_POINTS);
  size_t total = fc ? p + t->fc_n : p;
  float lo = INFINITY, hi = -INFINITY;
  static float avg[CHART_POINTS + GLANCE_FC_POINTS];
  uint16_t pts = 0;
  for (size_t i = 0; i < p; i++) {   // each point is the mean of its slice of the minutes
    long a, b;
    if (fc) {   // 6 h ending now, a slot every GLANCE_FC_MINUTES; slots before the first record are empty
      a = (long)n - (long)(FC_HIST_SLOTS - i) * GLANCE_FC_MINUTES;
      b = a + GLANCE_FC_MINUTES;
      if (a < 0) a = 0;
      if (b < 0) b = 0;
    } else {
      a = (long)(i * n / p);
      b = (long)((i + 1) * n / p);
    }
    double sum = 0;
    size_t cnt = 0;
    for (long k = a; k < b; k++)
      if (!isnan(series[k])) sum += series[k], cnt++;
    avg[i] = cnt ? (float)(sum / (double)cnt) : NAN;
    if (cnt) {
      if (avg[i] < lo) lo = avg[i];
      if (avg[i] > hi) hi = avg[i];
      pts++;
    }
  }
  for (size_t i = p; i < total; i++) {
    avg[i] = t->fc[i - p];
    if (avg[i] < lo) lo = avg[i];
    if (avg[i] > hi) hi = avg[i];
  }
  w->points = pts;
  if (hi - lo < 1e-6f) {   // flat: centre it rather than divide by nothing
    float mid = pts ? lo : 0;
    lo = mid - 0.5f;
    hi = mid + 0.5f;
  } else {
    float pad = (hi - lo) * 0.1f;
    lo -= pad;
    hi += pad;
  }
  bd_meta_t m;
  bool has_meta = bd_get_meta(t->trend, &m);
  if (has_meta) {
    float span = hi - lo, nlo = lo, nhi = hi;
    for (uint8_t z = 0; z < m.zone_count; z++) {
      if (m.zones[z].state < 2) continue;
      if (isfinite(m.zones[z].upper) && m.zones[z].upper <= lo && lo - (float)m.zones[z].upper <= ZONE_REACH * span &&
          (float)m.zones[z].upper - ZONE_SLIVER * span < nlo)
        nlo = (float)m.zones[z].upper - ZONE_SLIVER * span;
      if (isfinite(m.zones[z].lower) && m.zones[z].lower >= hi && (float)m.zones[z].lower - hi <= ZONE_REACH * span &&
          (float)m.zones[z].lower + ZONE_SLIVER * span > nhi)
        nhi = (float)m.zones[z].lower + ZONE_SLIVER * span;
    }
    lo = nlo;
    hi = nhi;
  }
  lv_chart_set_point_count(w->chart, (uint32_t)total);
  for (size_t i = 0; i < total; i++)
    lv_chart_set_series_value_by_id(w->chart, w->ser, (uint32_t)i,
                                    i >= p || isnan(avg[i]) ? LV_CHART_POINT_NONE : (int32_t)((avg[i] - lo) / (hi - lo) * CHART_SCALE));
  lv_chart_refresh(w->chart);
  if (w->chart_fc) {
    lv_chart_set_point_count(w->chart_fc, (uint32_t)total);
    for (size_t i = 0; i < total; i++) {   // from the last point before now, so the dashes join the line
      bool on = fc && (i + 1 >= p) && !isnan(avg[i]);
      lv_chart_set_series_value_by_id(w->chart_fc, w->ser_fc, (uint32_t)i,
                                      on ? (int32_t)((avg[i] - lo) / (hi - lo) * CHART_SCALE) : LV_CHART_POINT_NONE);
    }
    lv_chart_refresh(w->chart_fc);
    show(w->now_line, fc);
    if (fc) {
      int32_t cx = lv_obj_get_x(w->chart) + 2 + (lv_obj_get_width(w->chart) - 4) * (int32_t)(p - 1) / (int32_t)(total - 1);
      lv_obj_set_pos(w->now_line, cx, lv_obj_get_y(w->chart));
      w->now_pts[1].y = lv_obj_get_height(w->chart);
      lv_line_set_points(w->now_line, w->now_pts, 2);
    }
  }
  // Signal K's warn and alarm zones that the plotted range touches, behind the line.
  int nb = 0;
  int32_t ch = lv_obj_get_height(w->chart);
  if (has_meta)
    for (uint8_t z = 0; z < m.zone_count && nb < MAX_BANDS; z++) {
      if (m.zones[z].state < 2 || m.zones[z].upper <= lo || m.zones[z].lower >= hi) continue;
      float zl = m.zones[z].lower < lo ? lo : (float)m.zones[z].lower, zh = m.zones[z].upper > hi ? hi : (float)m.zones[z].upper;
      int32_t ytop = (int32_t)((1.0f - (zh - lo) / (hi - lo)) * (float)ch), ybot = (int32_t)((1.0f - (zl - lo) / (hi - lo)) * (float)ch);
      lv_obj_set_y(w->band[nb], lv_obj_get_y(w->chart) + ytop);
      lv_obj_set_height(w->band[nb], ybot - ytop > 2 ? ybot - ytop : 2);
      lv_obj_set_style_bg_color(w->band[nb], m.zones[z].state >= 3 ? ui_theme()->alarm : ui_theme()->warn, 0);
      show(w->band[nb], true);
      nb++;
    }
  for (int i = nb; i < MAX_BANDS; i++) show(w->band[i], false);
  if (w->axis_l) {
    char buf[16];
    if (fc) snprintf(buf, sizeof buf, "-6 h");
    else if (n >= 120) snprintf(buf, sizeof buf, "-%u h", (unsigned)((n + 59) / 60));
    else snprintf(buf, sizeof buf, "-%u min", (unsigned)n);
    set_text(w->axis_l, buf);
  }
}

static void update_tile(tile_w_t *w, int64_t now_ms) {
  const glance_source_t *s = &g.src[w->source];
  glance_tile_t t = {0};
  s->b->def->tile(s->b, now_ms, &t);
  lv_color_t col = state_color(t.state);
  if (t.state != w->last.state) {
    lv_obj_set_style_bg_color(w->edge, col, 0);
    lv_obj_set_style_text_color(w->word, col, 0);
  }
  set_text(w->title, t.title ? t.title : "");
  if (strcmp(lv_label_get_text(w->word), t.word) != 0) {
    lv_label_set_text(w->word, t.word);
    lv_obj_align(w->word, LV_ALIGN_TOP_RIGHT, -12, g.sc == SIZE_WIDE ? 10 : 8);   // its width changed
  }
  if (w->headline) {
    bool missing = t.state == GT_NO_DATA && !t.headline[0];
    bool big = g.sc == SIZE_WIDE && strlen(t.headline) <= (w->swing ? 3 : 5);   // a plan view leaves a narrow text column
    if (big != w->big_headline || w->last.state == (gt_state_t)255) {
      const lv_font_t *hf = g.sc == SIZE_WIDE && big ? &lv_font_montserrat_48 : &lv_font_montserrat_28;
      lv_obj_set_style_text_font(w->headline, hf, 0);
      lv_obj_set_style_text_font(w->headline_b, hf, 0);
      w->big_headline = big;
    }
    set_text(w->headline, t.headline);
    set_text(w->headline_b, t.headline);
    set_text(w->unit, t.unit);
    if (strcmp(t.headline, w->last.headline) || strcmp(t.unit, w->last.unit) || w->last.state == (gt_state_t)255) {
      lv_obj_update_layout(w->tile);
      lv_obj_align_to(w->unit, w->headline, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, g.sc == SIZE_WIDE ? -6 : -3);
    }
    show(w->headline, !missing);
    show(w->headline_b, !missing);
    show(w->unit, !missing && t.unit[0]);
    for (int i = 0; i < 2; i++) show(w->fault[i], missing);
  }
  for (uint8_t i = 0; i < 3 && w->bar[i]; i++) {
    bool on = i < t.bar_count;
    show(w->bar[i], on);
    show(w->bar_label[i], on);
    show(w->bar_pct[i], on);
    if (!on) continue;
    char ch[2] = {t.bar_label[i], 0}, pct[8];
    bool unknown = t.bar_pct[i] == GT_PCT_UNKNOWN;
    snprintf(pct, sizeof pct, unknown ? "FAIL" : "%u%%", t.bar_pct[i]);
    set_text(w->bar_label[i], ch);
    set_text(w->bar_pct[i], pct);
    if (unknown != (w->last.bar_pct[i] == GT_PCT_UNKNOWN))   // the fault colour only while it is a fault
      lv_obj_set_style_text_color(w->bar_pct[i], unknown ? ui_theme()->fault_label : ui_theme()->fg, 0);
    uint8_t fill = unknown ? 0 : t.bar_pct[i];   // an unknown level draws no fill at all
    if (lv_bar_get_value(w->bar[i]) != fill) lv_bar_set_value(w->bar[i], fill, LV_ANIM_OFF);
  }
  set_text(w->ctx[0], t.context[0]);
  if (w->ctx[1]) set_text(w->ctx[1], t.context[1]);
  update_chart(w, &t, now_ms);
  if (w->swing) {
    show(w->swing, t.swing != NULL);
    if (t.swing) ui_swing_set(w->swing, t.swing);
  }
  w->last = t;
}

void glance_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b) {
  (void)page;
  (void)b;
  const ui_colors_t *c = ui_theme();
  memset(g.t, 0, sizeof g.t);
  g.sc = sc;
  g.shown = 0;
  lv_obj_update_layout(area);
  int32_t aw = lv_obj_get_width(area), ah = lv_obj_get_height(area);

  const char *names[MAX_SOURCES];
  gt_state_t states[MAX_SOURCES];
  for (uint8_t i = 0; i < g.n; i++) {
    glance_tile_t t = {0};
    g.src[i].b->def->tile(g.src[i].b, platform_millis(), &t);
    names[i] = g.src[i].b->def->name;
    states[i] = t.state;
  }
  size_t k = glance_pick(names, states, g.n, GLANCE_MAX_TILES, g.picked);
  if (k == 0) {
    lv_obj_t *l = lv_label_create(area);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(l, c->dim, 0);
    lv_label_set_text(l, "NOTHING TO SHOW");
    lv_obj_center(l);
    return;
  }
  // The grid is 3 x 2 on wide boards and 2 x 3 on square ones. Tiles keep their grid-cell size however few there
  // are, and the block is centred, so three tiles are not stretched into tall strips.
  int cols = sc == SIZE_WIDE ? 3 : 2, cell_rows = sc == SIZE_WIDE ? 2 : 3;
  int rows = ((int)k + cols - 1) / cols;
  int32_t gap = sc == SIZE_WIDE ? 10 : 8;
  int32_t tw = (aw - (cols + 1) * gap) / cols, th = (ah - (cell_rows + 1) * gap) / cell_rows;
  int32_t top = (ah - (rows * th + (rows + 1) * gap)) / 2;
  for (size_t i = 0; i < k; i++) {
    tile_w_t *w = &g.t[i];
    w->source = (uint8_t)g.picked[i];
    int32_t x = gap + (int32_t)(i % (size_t)cols) * (tw + gap), y = top + gap + (int32_t)(i / (size_t)cols) * (th + gap);
    build_tile(area, w, (uint8_t)i, x, y, tw, th);
    g.shown++;
  }
  lv_obj_update_layout(area);
  for (uint8_t i = 0; i < g.shown; i++) update_tile(&g.t[i], platform_millis());
}

void glance_refresh(role_binding_t *b, int64_t now_ms) {
  (void)b;
  if (g.shown == 0) return;
  const char *names[MAX_SOURCES];
  gt_state_t states[MAX_SOURCES];
  for (uint8_t i = 0; i < g.n; i++) {
    glance_tile_t t = {0};
    g.src[i].b->def->tile(g.src[i].b, now_ms, &t);
    names[i] = g.src[i].b->def->name;
    states[i] = t.state;
  }
  size_t picked[GLANCE_MAX_TILES];
  size_t k = glance_pick(names, states, g.n, GLANCE_MAX_TILES, picked);
  if (k != g.shown || memcmp(picked, g.picked, k * sizeof picked[0]) != 0) {   // a quiet tile made way for an alerting one
    ui_shell_rebuild();
    return;
  }
  for (uint8_t i = 0; i < g.shown; i++) update_tile(&g.t[i], now_ms);
}

// ---- test access ----

uint8_t glance_tile_count(void) { return g.shown; }

lv_obj_t *glance_part(uint8_t tile, glance_part_t part) {
  if (tile >= g.shown) return NULL;
  tile_w_t *w = &g.t[tile];
  switch (part) {
    case GP_TILE: return w->tile;
    case GP_EDGE: return w->edge;
    case GP_TITLE: return w->title;
    case GP_WORD: return w->word;
    case GP_HEADLINE: return w->headline;
    case GP_UNIT: return w->unit;
    case GP_CONTEXT0: return w->ctx[0];
    case GP_CONTEXT1: return w->ctx[1];
    case GP_FAULT: return w->fault[0];
    case GP_CHART: return w->chart;
    case GP_SWING: return w->swing;
  }
  return NULL;
}

lv_obj_t *glance_bar_pct(uint8_t tile, uint8_t bar) { return tile < g.shown && bar < 3 ? g.t[tile].bar_pct[bar] : NULL; }

uint16_t glance_trend_points(uint8_t tile) { return tile < g.shown ? g.t[tile].points : 0; }
