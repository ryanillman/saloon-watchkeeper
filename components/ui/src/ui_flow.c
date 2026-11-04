#include "ui_flow.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_theme.h"

// ---- drawing helpers ----

static void line(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t w, lv_color_t c,
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

static void box(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, lv_color_t fill, lv_opa_t fill_opa,
                lv_color_t border, int32_t bw, int32_t radius) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = fill;
  d.bg_opa = fill_opa;
  d.border_color = border;
  d.border_width = bw;
  d.border_opa = bw ? LV_OPA_COVER : LV_OPA_TRANSP;
  d.radius = radius;
  lv_area_t a = {x1, y1, x2, y2};
  lv_draw_rect(layer, &d, &a);
}

static void tri(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3,
                lv_color_t c) {
  lv_draw_triangle_dsc_t d;
  lv_draw_triangle_dsc_init(&d);
  d.p[0].x = x1; d.p[0].y = y1;
  d.p[1].x = x2; d.p[1].y = y2;
  d.p[2].x = x3; d.p[2].y = y3;
  d.color = c;
  d.opa = LV_OPA_COVER;
  lv_draw_triangle(layer, &d);
}

// ---- icons ----

typedef struct {
  ui_icon_t kind;
  bool active;
  bool fill_valid;
  double fill;
  lv_color_t fill_color;
} icon_t;

#define IC_X(a, f) ((a)->x1 + (int32_t)lroundf((f) * (float)(w)))
#define IC_Y(a, f) ((a)->y1 + (int32_t)lroundf((f) * (float)(h)))

static void draw_icon(lv_event_t *e) {
  lv_obj_t *o = lv_event_get_target_obj(e);
  lv_layer_t *layer = lv_event_get_layer(e);
  const icon_t *ic = lv_obj_get_user_data(o);
  const ui_colors_t *c = ui_theme();
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  int32_t w = lv_area_get_width(&a), h = lv_area_get_height(&a);
  int32_t s = w < h ? w : h;
  a.x1 += (w - s) / 2;  a.y1 += (h - s) / 2;  a.x2 = a.x1 + s - 1;  a.y2 = a.y1 + s - 1;
  w = h = s;
  int32_t t = s / 16 < 2 ? 2 : s / 16;   // stroke
  lv_color_t fg = ic->active ? c->fg : c->dim;
  lv_color_t hi = ic->active ? c->accent : c->dim;

  switch (ic->kind) {
    case ICON_SOLAR: {
      // Tilted panel (a parallelogram of two triangles), a 3 x 2 cell grid, a stand, and a sun.
      int32_t ax = IC_X(&a, 0.30f), ay = IC_Y(&a, 0.18f), bx = IC_X(&a, 0.92f), by = IC_Y(&a, 0.18f);
      int32_t cx = IC_X(&a, 0.70f), cy = IC_Y(&a, 0.72f), dx = IC_X(&a, 0.08f), dy = IC_Y(&a, 0.72f);
      tri(layer, ax, ay, bx, by, cx, cy, c->strip_bg);
      tri(layer, ax, ay, cx, cy, dx, dy, c->strip_bg);
      for (int k = 1; k < 3; k++) {   // vertical cell lines, parallel to the slanted sides
        int32_t tx = ax + (bx - ax) * k / 3, bx2 = dx + (cx - dx) * k / 3;
        line(layer, tx, ay, bx2, dy, 1, fg, LV_OPA_60);
      }
      line(layer, (ax + dx) / 2, (ay + dy) / 2, (bx + cx) / 2, (by + cy) / 2, 1, fg, LV_OPA_60);
      line(layer, ax, ay, bx, by, t, fg, LV_OPA_COVER);
      line(layer, bx, by, cx, cy, t, fg, LV_OPA_COVER);
      line(layer, cx, cy, dx, dy, t, fg, LV_OPA_COVER);
      line(layer, dx, dy, ax, ay, t, fg, LV_OPA_COVER);
      line(layer, IC_X(&a, 0.39f), dy, IC_X(&a, 0.39f), IC_Y(&a, 0.92f), t, fg, LV_OPA_COVER);
      line(layer, IC_X(&a, 0.22f), IC_Y(&a, 0.92f), IC_X(&a, 0.56f), IC_Y(&a, 0.92f), t, fg, LV_OPA_COVER);
      int32_t r = s / 9;
      box(layer, IC_X(&a, 0.14f) - r, IC_Y(&a, 0.12f) - r, IC_X(&a, 0.14f) + r, IC_Y(&a, 0.12f) + r, hi, LV_OPA_COVER,
          hi, 0, LV_RADIUS_CIRCLE);
      break;
    }
    case ICON_SHORE: {
      // A mains plug: two prongs, a rounded body, a cord leaving the bottom.
      int32_t px1 = IC_X(&a, 0.36f), px2 = IC_X(&a, 0.64f);
      line(layer, px1, IC_Y(&a, 0.08f), px1, IC_Y(&a, 0.30f), t + 1, hi, LV_OPA_COVER);
      line(layer, px2, IC_Y(&a, 0.08f), px2, IC_Y(&a, 0.30f), t + 1, hi, LV_OPA_COVER);
      box(layer, IC_X(&a, 0.24f), IC_Y(&a, 0.30f), IC_X(&a, 0.76f), IC_Y(&a, 0.62f), c->strip_bg, LV_OPA_COVER, fg, t,
          s / 8);
      line(layer, IC_X(&a, 0.50f), IC_Y(&a, 0.62f), IC_X(&a, 0.50f), IC_Y(&a, 0.74f), t + 1, fg, LV_OPA_COVER);
      line(layer, IC_X(&a, 0.50f), IC_Y(&a, 0.74f), IC_X(&a, 0.74f), IC_Y(&a, 0.92f), t + 1, fg, LV_OPA_COVER);
      break;
    }
    case ICON_ENGINE: {
      // An alternator: a ring with a lightning bolt in it.
      int32_t m = s / 12;
      box(layer, a.x1 + m, a.y1 + m, a.x2 - m, a.y2 - m, c->strip_bg, LV_OPA_COVER, fg, t, LV_RADIUS_CIRCLE);
      int32_t x1 = IC_X(&a, 0.56f), y1 = IC_Y(&a, 0.18f), x2 = IC_X(&a, 0.34f), y2 = IC_Y(&a, 0.54f);
      int32_t x3 = IC_X(&a, 0.52f), y3 = IC_Y(&a, 0.54f), x4 = IC_X(&a, 0.44f), y4 = IC_Y(&a, 0.84f);
      int32_t x5 = IC_X(&a, 0.68f), y5 = IC_Y(&a, 0.44f), x6 = IC_X(&a, 0.50f), y6 = IC_Y(&a, 0.44f);
      tri(layer, x1, y1, x2, y2, x3, y3, hi);
      tri(layer, x1, y1, x3, y3, x6, y6, hi);
      tri(layer, x6, y6, x5, y5, x4, y4, hi);
      tri(layer, x3, y3, x6, y6, x4, y4, hi);
      break;
    }
    case ICON_BATTERY: {
      // A battery seen from the front: body, two terminals, and the charge as a fill from the bottom.
      int32_t bx1 = IC_X(&a, 0.06f), bx2 = IC_X(&a, 0.94f), by1 = IC_Y(&a, 0.24f), by2 = IC_Y(&a, 0.90f);
      int32_t tw = s / 8, th = s / 10;
      box(layer, IC_X(&a, 0.20f), by1 - th, IC_X(&a, 0.20f) + tw, by1, fg, LV_OPA_COVER, fg, 0, 0);
      box(layer, IC_X(&a, 0.80f) - tw, by1 - th, IC_X(&a, 0.80f), by1, fg, LV_OPA_COVER, fg, 0, 0);
      box(layer, bx1, by1, bx2, by2, c->strip_bg, LV_OPA_COVER, fg, t, s / 14);
      if (ic->fill_valid && ic->fill > 0) {
        double f = ic->fill > 1 ? 1 : ic->fill;
        int32_t in = t + 2, top = by2 - in - (int32_t)lround((double)(by2 - by1 - 2 * in) * f);
        box(layer, bx1 + in, top, bx2 - in, by2 - in, ic->fill_color, LV_OPA_COVER, ic->fill_color, 0, s / 24);
      }
      break;
    }
    case ICON_LOAD: {
      // A light bulb: a glass globe, a screw base.
      int32_t r = s * 3 / 10, cx = a.x1 + s / 2, cy = IC_Y(&a, 0.36f);
      box(layer, cx - r, cy - r, cx + r, cy + r, ic->active ? c->accent : c->strip_bg, LV_OPA_COVER, fg, t,
          LV_RADIUS_CIRCLE);
      tri(layer, cx - r * 6 / 10, cy + r * 8 / 10, cx + r * 6 / 10, cy + r * 8 / 10, cx, cy + r * 15 / 10,
          ic->active ? c->accent : c->strip_bg);
      int32_t by = IC_Y(&a, 0.72f);
      for (int k = 0; k < 3; k++) line(layer, cx - s / 7, by + k * s / 11, cx + s / 7, by + k * s / 11, t, fg, LV_OPA_COVER);
      break;
    }
  }
}

static void free_user_data(lv_event_t *e) { free(lv_event_get_user_data(e)); }

lv_obj_t *ui_icon_create(lv_obj_t *parent, ui_icon_t kind) {
  icon_t *ic = calloc(1, sizeof *ic);
  ic->kind = kind;
  ic->active = true;
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(o, 64, 64);
  lv_obj_set_user_data(o, ic);
  lv_obj_add_event_cb(o, draw_icon, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(o, free_user_data, LV_EVENT_DELETE, ic);
  return o;
}

void ui_icon_set_active(lv_obj_t *o, bool active) {
  icon_t *ic = lv_obj_get_user_data(o);
  if (ic->active == active) return;
  ic->active = active;
  lv_obj_invalidate(o);
}

void ui_icon_set_fill(lv_obj_t *o, double fraction, bool valid, lv_color_t color) {
  icon_t *ic = lv_obj_get_user_data(o);
  valid = valid && isfinite(fraction);
  if (ic->fill_valid == valid && (!valid || (ic->fill == fraction && lv_color_eq(ic->fill_color, color)))) return;
  ic->fill_valid = valid;
  ic->fill = valid ? fraction : 0;
  ic->fill_color = color;
  lv_obj_invalidate(o);
}

// ---- flow arrows ----

enum {
  SEGMENTS = 24,        // the curve is drawn as this many straight pieces
  CHEVRON_SPACING = 44, // pixels between chevrons
  PHASE_STEPS = 16,     // the animation redraws this many times per cycle: smooth enough, and cheap
  LABEL_W = 150,
  LABEL_H = 22,
  LABEL_GAP = 14,       // from the curve to the label's centre
};

typedef struct {
  float x0, y0, c1x, c1y, c2x, c2y, x1, y1;   // a cubic Bezier, relative to the object's top-left
  int32_t lx, ly;                 // label centre, likewise
  uint8_t level;
  bool valid;
  bool animating;
  uint8_t step;                   // 0 .. PHASE_STEPS-1
  char label[28];
} flow_t;

static void bez(const flow_t *f, float t, float *x, float *y) {
  float u = 1 - t, a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
  *x = a * f->x0 + b * f->c1x + c * f->c2x + d * f->x1;
  *y = a * f->y0 + b * f->c1y + c * f->c2y + d * f->y1;
}

static void bez_tangent(const flow_t *f, float t, float *dx, float *dy) {
  float u = 1 - t;
  *dx = 3 * u * u * (f->c1x - f->x0) + 6 * u * t * (f->c2x - f->c1x) + 3 * t * t * (f->x1 - f->c2x);
  *dy = 3 * u * u * (f->c1y - f->y0) + 6 * u * t * (f->c2y - f->c1y) + 3 * t * t * (f->y1 - f->c2y);
  float n = sqrtf(*dx * *dx + *dy * *dy);
  if (n < 1e-3f) { *dx = 1; *dy = 0; return; }
  *dx /= n;
  *dy /= n;
}

static float flow_length(const flow_t *f) {
  float len = 0, px = f->x0, py = f->y0;
  for (int i = 1; i <= SEGMENTS; i++) {
    float x, y;
    bez(f, (float)i / SEGMENTS, &x, &y);
    len += hypotf(x - px, y - py);
    px = x;
    py = y;
  }
  return len;
}

static void draw_flow(lv_event_t *e) {
  lv_obj_t *o = lv_event_get_target_obj(e);
  lv_layer_t *layer = lv_event_get_layer(e);
  const flow_t *f = lv_obj_get_user_data(o);
  if (!f->valid) return;
  const ui_colors_t *c = ui_theme();
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  flow_t g = *f;   // draw in screen coordinates
  g.x0 += a.x1; g.y0 += a.y1; g.c1x += a.x1; g.c1y += a.y1; g.c2x += a.x1; g.c2y += a.y1;
  g.x1 += a.x1; g.y1 += a.y1;
  int32_t tw = f->level ? 2 + f->level : 2;

  // Mixed with the background rather than drawn translucent: overlapping round caps would show as beads.
  lv_color_t track = f->level ? lv_color_mix(c->accent, c->bg, 90) : lv_color_mix(c->dim, c->bg, 150);
  // The track: the whole curve, bright while the flow is moving and dim when it is not.
  float px = g.x0, py = g.y0;
  for (int i = 1; i <= SEGMENTS; i++) {
    float x, y;
    bez(&g, (float)i / SEGMENTS, &x, &y);
    line(layer, (int32_t)lroundf(px), (int32_t)lroundf(py), (int32_t)lroundf(x), (int32_t)lroundf(y), tw, track,
         LV_OPA_COVER);
    px = x;
    py = y;
  }

  if (f->level) {
    // Chevrons, evenly spaced and sliding from start to end; each fades in and out along the curve,
    // so the flow reads as a wave rather than a row of sticks.
    float len = flow_length(&g);
    int k_n = (int)ceilf(len / CHEVRON_SPACING);
    if (k_n < 1) k_n = 1;
    float phase = (float)f->step / PHASE_STEPS;
    float sz = 5.0f + 2.0f * f->level;
    for (int k = 0; k < k_n; k++) {
      float t = ((float)k + phase) / (float)k_n;
      float x, y, dx, dy;
      bez(&g, t, &x, &y);
      bez_tangent(&g, t, &dx, &dy);
      lv_opa_t opa = (lv_opa_t)lroundf(255.0f * sinf((float)M_PI * t));
      float bx = x - dx * sz, by = y - dy * sz, nx = -dy * sz * 0.8f, ny = dx * sz * 0.8f;
      line(layer, (int32_t)lroundf(x), (int32_t)lroundf(y), (int32_t)lroundf(bx + nx), (int32_t)lroundf(by + ny), tw,
           c->accent, opa);
      line(layer, (int32_t)lroundf(x), (int32_t)lroundf(y), (int32_t)lroundf(bx - nx), (int32_t)lroundf(by - ny), tw,
           c->accent, opa);
    }
  }

  if (f->label[0]) {
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = f->label;
    d.font = &lv_font_montserrat_14;
    d.color = f->level ? c->fg : c->dim;
    d.align = LV_TEXT_ALIGN_CENTER;
    int32_t lh = lv_font_get_line_height(d.font);
    lv_area_t la = {a.x1 + f->lx - LABEL_W / 2, a.y1 + f->ly - lh / 2, a.x1 + f->lx + LABEL_W / 2,
                    a.y1 + f->ly + lh / 2};
    lv_draw_label(layer, &d, &la);
  }
}

static void anim_cb(void *var, int32_t v) {
  lv_obj_t *o = var;
  flow_t *f = lv_obj_get_user_data(o);
  uint8_t step = (uint8_t)(v * PHASE_STEPS / 1000);
  if (step >= PHASE_STEPS) step = PHASE_STEPS - 1;
  if (step == f->step) return;
  f->step = step;
  lv_obj_invalidate(o);
}

static void anim_stop(lv_obj_t *o) {
  flow_t *f = lv_obj_get_user_data(o);
  lv_anim_delete(o, anim_cb);
  f->animating = false;
}

static void anim_start(lv_obj_t *o, uint8_t level) {
  static const uint32_t CYCLE_MS[] = {0, 2800, 1800, 1100};   // one chevron spacing of travel
  flow_t *f = lv_obj_get_user_data(o);
  lv_anim_delete(o, anim_cb);
  lv_anim_t an;
  lv_anim_init(&an);
  lv_anim_set_var(&an, o);
  lv_anim_set_exec_cb(&an, anim_cb);
  lv_anim_set_values(&an, 0, 1000);
  lv_anim_set_duration(&an, CYCLE_MS[level]);
  lv_anim_set_repeat_count(&an, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&an, lv_anim_path_linear);
  lv_anim_start(&an);
  f->animating = true;
}

lv_obj_t *ui_flow_create(lv_obj_t *parent) {
  flow_t *f = calloc(1, sizeof *f);
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(o, 1, 1);
  lv_obj_set_user_data(o, f);
  lv_obj_add_event_cb(o, draw_flow, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(o, free_user_data, LV_EVENT_DELETE, f);
  return o;
}

// Stores a cubic given in the parent's coordinates: sizes the object around the curve and the label,
// and puts the label on the `side` of the curve (+1 left of the direction of travel, -1 right, 0 above).
static void set_curve(lv_obj_t *o, float x0, float y0, float c1x, float c1y, float c2x, float c2y, float x1, float y1,
                      int side) {
  flow_t *f = lv_obj_get_user_data(o);
  flow_t g = {.x0 = x0, .y0 = y0, .c1x = c1x, .c1y = c1y, .c2x = c2x, .c2y = c2y, .x1 = x1, .y1 = y1};
  float mx, my, tx, ty;
  bez(&g, 0.5f, &mx, &my);
  bez_tangent(&g, 0.5f, &tx, &ty);
  float ox = side * ty, oy = -side * tx;   // left of travel on a y-down screen
  if (!side) { ox = 0; oy = -1; }
  // Clear of the curve by the label's own half-extent along the offset direction.
  float away = LABEL_GAP + fabsf(ox) * LABEL_W / 2.0f + fabsf(oy) * LABEL_H / 2.0f - LABEL_H / 2.0f;
  float lx = mx + ox * away, ly = my + oy * away;

  const float pad = 12;
  float minx = fminf(fminf(fminf(x0, x1), fminf(c1x, c2x)), lx - LABEL_W / 2.0f) - pad;
  float maxx = fmaxf(fmaxf(fmaxf(x0, x1), fmaxf(c1x, c2x)), lx + LABEL_W / 2.0f) + pad;
  float miny = fminf(fminf(fminf(y0, y1), fminf(c1y, c2y)), ly - LABEL_H / 2.0f) - pad;
  float maxy = fmaxf(fmaxf(fmaxf(y0, y1), fmaxf(c1y, c2y)), ly + LABEL_H / 2.0f) + pad;
  int32_t ox0 = (int32_t)floorf(minx), oy0 = (int32_t)floorf(miny);
  lv_obj_set_pos(o, ox0, oy0);
  lv_obj_set_size(o, (int32_t)ceilf(maxx) - ox0, (int32_t)ceilf(maxy) - oy0);
  f->x0 = x0 - (float)ox0;   f->y0 = y0 - (float)oy0;
  f->c1x = c1x - (float)ox0; f->c1y = c1y - (float)oy0;
  f->c2x = c2x - (float)ox0; f->c2y = c2y - (float)oy0;
  f->x1 = x1 - (float)ox0;   f->y1 = y1 - (float)oy0;
  f->lx = (int32_t)lroundf(lx) - ox0;
  f->ly = (int32_t)lroundf(ly) - oy0;
  lv_obj_invalidate(o);
}

void ui_flow_set_path(lv_obj_t *o, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t bend) {
  float dx = (float)(x1 - x0), dy = (float)(y1 - y0), n = hypotf(dx, dy);
  if (n < 1) n = 1;
  // A quadratic curve bulges only half its control offset, so the control point goes out twice as far;
  // raised to a cubic, each control sits two thirds of the way from its end to that point.
  float qx = (float)(x0 + x1) / 2 + dy / n * 2 * (float)bend, qy = (float)(y0 + y1) / 2 - dx / n * 2 * (float)bend;
  set_curve(o, (float)x0, (float)y0, (float)x0 + 2.0f / 3 * (qx - (float)x0), (float)y0 + 2.0f / 3 * (qy - (float)y0),
            (float)x1 + 2.0f / 3 * (qx - (float)x1), (float)y1 + 2.0f / 3 * (qy - (float)y1), (float)x1, (float)y1,
            bend > 0 ? 1 : bend < 0 ? -1 : 0);
}

void ui_flow_set_route(lv_obj_t *o, int32_t x0, int32_t y0, int32_t x1, int32_t y1, ui_axis_t leave, ui_axis_t arrive,
                       int side) {
  float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
  set_curve(o, (float)x0, (float)y0, (float)x0 + (leave == AXIS_H ? dx / 2 : 0), (float)y0 + (leave == AXIS_V ? dy / 2 : 0),
            (float)x1 - (arrive == AXIS_H ? dx / 2 : 0), (float)y1 - (arrive == AXIS_V ? dy / 2 : 0), (float)x1,
            (float)y1, side);
}

void ui_flow_set(lv_obj_t *o, uint8_t level, bool valid, const char *label) {
  flow_t *f = lv_obj_get_user_data(o);
  if (level > 3) level = 3;
  if (!valid) level = 0;
  char next[sizeof f->label] = "";
  if (valid && label) snprintf(next, sizeof next, "%s", label);
  if (f->valid == valid && f->level == level && strcmp(f->label, next) == 0) return;
  bool level_changed = f->level != level || f->valid != valid;
  f->valid = valid;
  f->level = level;
  memcpy(f->label, next, sizeof next);
  if (level_changed) {
    if (level) anim_start(o, level);
    else anim_stop(o);
    f->step = 0;
  }
  lv_obj_invalidate(o);
}

uint8_t ui_flow_level_for(double watts, double ref_watts) {
  if (!(watts >= 0.5)) return 0;
  double r = watts / (ref_watts > watts ? ref_watts : watts);
  return r >= 0.6 ? 3 : r >= 0.25 ? 2 : 1;
}

uint8_t ui_flow_level(lv_obj_t *o) { return ((flow_t *)lv_obj_get_user_data(o))->level; }
bool ui_flow_valid(lv_obj_t *o) { return ((flow_t *)lv_obj_get_user_data(o))->valid; }
const char *ui_flow_label(lv_obj_t *o) { return ((flow_t *)lv_obj_get_user_data(o))->label; }
bool ui_flow_animating(lv_obj_t *o) { return ((flow_t *)lv_obj_get_user_data(o))->animating; }
