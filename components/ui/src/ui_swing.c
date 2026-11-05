#include "ui_swing.h"
#include <math.h>
#include <string.h>
#include "ui_theme.h"

#define RING_FRACTION 0.8
#define EDGE_MARGIN 1.1

typedef struct { ui_swing_data_t d; } swing_t;

double ui_swing_scale(double limit_m, double max_dist_m, int32_t plot_px) {
  double avail = plot_px > 0 ? plot_px / 2.0 : 1.0;
  double reach = 0;
  if (isfinite(limit_m) && limit_m > 0) reach = limit_m / RING_FRACTION;
  if (isfinite(max_dist_m) && max_dist_m * EDGE_MARGIN > reach) reach = max_dist_m * EDGE_MARGIN;
  if (reach <= 0) reach = 10;   // nothing known: a 10 m view
  return avail / reach;
}

static lv_color_t state_color(ui_swing_state_t s) {
  const ui_colors_t *c = ui_theme();
  return s == UI_SWING_GREEN ? c->ok : s == UI_SWING_AMBER ? c->warn : s == UI_SWING_RED ? c->alarm : c->dim;
}

static void ring(lv_layer_t *layer, int32_t cx, int32_t cy, double r_px, int dash_deg, int step_deg, int32_t width,
                 lv_color_t color) {
  if (r_px < 2) return;
  lv_draw_arc_dsc_t arc;
  lv_draw_arc_dsc_init(&arc);
  arc.center.x = cx;
  arc.center.y = cy;
  arc.radius = (uint16_t)r_px;
  arc.width = (uint16_t)width;
  arc.color = color;
  for (int a = 0; a < 360; a += step_deg) {
    arc.start_angle = (lv_value_precise_t)a;
    arc.end_angle = (lv_value_precise_t)(a + dash_deg);
    lv_draw_arc(layer, &arc);
  }
}

static void segment(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t w, lv_color_t c,
                    lv_opa_t opa) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.p1.x = x1; d.p1.y = y1; d.p2.x = x2; d.p2.y = y2;
  d.width = w;
  d.color = c;
  d.opa = opa;
  d.round_start = d.round_end = 1;
  lv_draw_line(layer, &d);
}

static void dot(lv_layer_t *layer, int32_t x, int32_t y, int32_t r, lv_color_t fill, lv_color_t edge) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = fill;
  d.bg_opa = LV_OPA_COVER;
  d.radius = LV_RADIUS_CIRCLE;
  d.border_color = edge;
  d.border_width = 2;
  d.border_opa = LV_OPA_COVER;
  lv_area_t a = {x - r, y - r, x + r, y + r};
  lv_draw_rect(layer, &d, &a);
}

static void draw(lv_event_t *e) {
  lv_obj_t *o = lv_event_get_target(e);
  const swing_t *s = lv_obj_get_user_data(o);
  if (!s) return;
  lv_layer_t *layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  int32_t w = lv_area_get_width(&a), h = lv_area_get_height(&a);
  int32_t plot = w < h ? w : h;
  int32_t cx = a.x1 + w / 2, cy = a.y1 + h / 2;
  const ui_colors_t *c = ui_theme();
  lv_color_t state = state_color(s->d.state);
  bool big = plot > 300;

  double max_d = 0;
  for (size_t i = 0; i < s->d.track_n; i++) {
    if (isnan(s->d.track[i].x)) continue;
    double d = hypot(s->d.track[i].x, s->d.track[i].y);
    if (d > max_d) max_d = d;
  }
  double scale = ui_swing_scale(s->d.limit_m, max_d, plot);

  if (isfinite(s->d.warn_m) && s->d.warn_m > 0) ring(layer, cx, cy, s->d.warn_m * scale, 2, 5, 2, c->dim);   // dotted
  if (isfinite(s->d.limit_m) && s->d.limit_m > 0) ring(layer, cx, cy, s->d.limit_m * scale, 3, 6, big ? 4 : 3, state);   // dashed

  // The anchor.
  int32_t k = big ? 8 : 5;
  segment(layer, cx - k, cy, cx + k, cy, 2, c->fg, LV_OPA_COVER);
  segment(layer, cx, cy - k, cx, cy + k, 2, c->fg, LV_OPA_COVER);

  // The track fades in from its oldest point; a gap breaks the line.
  int32_t lx = 0, ly = 0, bx = 0, by = 0;
  bool have_prev = false, have_boat = false;
  for (size_t i = 0; i < s->d.track_n; i++) {
    if (isnan(s->d.track[i].x)) {
      have_prev = false;
      continue;
    }
    int32_t px = cx + (int32_t)lround(s->d.track[i].x * scale), py = cy - (int32_t)lround(s->d.track[i].y * scale);
    if (have_prev) {
      lv_opa_t opa = (lv_opa_t)(60 + 195 * (double)i / (double)(s->d.track_n > 1 ? s->d.track_n - 1 : 1));
      segment(layer, lx, ly, px, py, big ? 3 : 2, c->fg, opa);
    }
    lx = bx = px;
    ly = by = py;
    have_prev = have_boat = true;
  }
  if (have_boat) dot(layer, bx, by, big ? 8 : 6, state, c->fg);

  if (s->d.show_n) {
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    const lv_font_t *f = big ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    d.text = "N";
    d.font = f;
    d.color = c->dim;
    d.align = LV_TEXT_ALIGN_CENTER;
    int32_t fh = lv_font_get_line_height(f);
    lv_area_t la = {cx - 20, cy - plot / 2, cx + 20, cy - plot / 2 + fh};
    lv_draw_label(layer, &d, &la);
  }
}

static void on_delete(lv_event_t *e) {
  swing_t *s = lv_obj_get_user_data(lv_event_get_target(e));
  lv_free(s);
}

lv_obj_t *ui_swing_create(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  swing_t *s = lv_malloc_zeroed(sizeof *s);
  s->d.limit_m = s->d.warn_m = NAN;
  s->d.state = UI_SWING_GREY;
  lv_obj_set_user_data(o, s);
  lv_obj_add_event_cb(o, draw, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(o, on_delete, LV_EVENT_DELETE, NULL);
  return o;
}

void ui_swing_set(lv_obj_t *sw, const ui_swing_data_t *d) {
  swing_t *s = lv_obj_get_user_data(sw);
  if (!s) return;
  s->d = *d;
  lv_obj_invalidate(sw);
}
