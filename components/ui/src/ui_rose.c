#include "ui_rose.h"
#include <math.h>
#include "ui_theme.h"

#define MARGIN_PX 38        // room outside the rim for the wind markers and the autopilot bug
#define DOT_FRACTION 0.21   // dot spacing, as a fraction of the radius
#define DOT_M 185.2         // one dot is 0.1 nm
#define DEG(rad) ((rad) * 180.0 / M_PI)

typedef struct { ui_rose_data_t d; } rose_t;

int32_t ui_rose_radius_px(int32_t side_px) {
  int32_t r = side_px / 2 - MARGIN_PX;
  return r > 0 ? r : 0;
}

double ui_rose_dot_px(double radius_px) { return DOT_FRACTION * radius_px; }

double ui_rose_xte_shift_px(double xte_m, double radius_px) {
  if (isnan(xte_m)) return 0;
  double dot = ui_rose_dot_px(radius_px);
  double shift = -xte_m / DOT_M * dot;
  if (shift > 2 * dot) shift = 2 * dot;
  if (shift < -2 * dot) shift = -2 * dot;
  return shift;
}

typedef struct { lv_layer_t *layer; int32_t cx, cy; double r, k; } ctx_t;

static bool ok(double x) { return isfinite(x); }

// A point at radius r (px) and angle `deg` clockwise from the top of the screen.
static lv_point_precise_t at(const ctx_t *c, double r, double deg) {
  double a = deg * M_PI / 180.0;
  return (lv_point_precise_t){(lv_value_precise_t)(c->cx + r * sin(a)), (lv_value_precise_t)(c->cy - r * cos(a))};
}

static void line(const ctx_t *c, lv_point_precise_t a, lv_point_precise_t b, int32_t w, lv_color_t col, bool round) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.p1 = a;
  d.p2 = b;
  d.width = w;
  d.color = col;
  d.round_start = d.round_end = round;
  lv_draw_line(c->layer, &d);
}

static void tri(const ctx_t *c, lv_point_precise_t a, lv_point_precise_t b, lv_point_precise_t p, lv_color_t col) {
  lv_draw_triangle_dsc_t d;
  lv_draw_triangle_dsc_init(&d);
  d.p[0] = a;
  d.p[1] = b;
  d.p[2] = p;
  d.color = col;
  d.opa = LV_OPA_COVER;
  lv_draw_triangle(c->layer, &d);
}

static void outline(const ctx_t *c, lv_point_precise_t a, lv_point_precise_t b, lv_point_precise_t p, int32_t w, lv_color_t col) {
  line(c, a, b, w, col, true);
  line(c, b, p, w, col, true);
  line(c, p, a, w, col, true);
}

static void ring_dot(const ctx_t *c, lv_point_precise_t p, int32_t r, lv_color_t col) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_opa = LV_OPA_TRANSP;
  d.radius = LV_RADIUS_CIRCLE;
  d.border_color = col;
  d.border_width = 2;
  d.border_opa = LV_OPA_COVER;
  lv_area_t a = {(int32_t)p.x - r, (int32_t)p.y - r, (int32_t)p.x + r, (int32_t)p.y + r};
  lv_draw_rect(c->layer, &d, &a);
}

static void numeral(const ctx_t *c, const char *text, const lv_font_t *f, lv_point_precise_t p, lv_color_t col) {
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.text = text;   // always a string literal: the draw task reads it after this call returns
  d.font = f;
  d.color = col;
  d.align = LV_TEXT_ALIGN_CENTER;
  int32_t fh = lv_font_get_line_height(f);
  lv_area_t a = {(int32_t)p.x - 30, (int32_t)p.y - fh / 2, (int32_t)p.x + 30, (int32_t)p.y + fh / 2};
  lv_draw_label(c->layer, &d, &a);
}

static void rim_ring(const ctx_t *c, lv_color_t col, lv_opa_t opa) {
  lv_draw_arc_dsc_t arc;
  lv_draw_arc_dsc_init(&arc);
  arc.center.x = c->cx;
  arc.center.y = c->cy;
  arc.radius = (uint16_t)c->r;
  arc.width = 2;
  arc.color = col;
  arc.opa = opa;
  arc.start_angle = 0;
  arc.end_angle = 360;
  lv_draw_arc(c->layer, &arc);
}

static void boat(const ctx_t *c, const ui_colors_t *col) {
  double s = c->k > 0.95 ? 1.0 : 0.9;
  int32_t x = c->cx, y = c->cy;
  tri(c, (lv_point_precise_t){x, y - 34 * s}, (lv_point_precise_t){x - 13 * s, y - 2 * s}, (lv_point_precise_t){x + 13 * s, y - 2 * s}, col->fg);
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = col->fg;
  d.bg_opa = LV_OPA_COVER;
  d.radius = (int32_t)(5 * s);
  lv_area_t a = {x - (int32_t)(13 * s), y - (int32_t)(2 * s), x + (int32_t)(13 * s), y + (int32_t)(32 * s)};
  lv_draw_rect(c->layer, &d, &a);
  line(c, (lv_point_precise_t){x, y - 6 * s}, (lv_point_precise_t){x, y + 14 * s}, 3, col->bg, false);
}

static void wind_marker(const ctx_t *c, double rel_rad, lv_color_t col, bool hollow) {
  double a = DEG(rel_rad), half = 5.5 * c->k;
  lv_point_precise_t tip = at(c, c->r + 4 * c->k, a), l = at(c, c->r + 34 * c->k, a - half), r = at(c, c->r + 34 * c->k, a + half);
  if (hollow) outline(c, tip, l, r, 3, col);
  else tri(c, tip, l, r, col);
}

static void draw(lv_event_t *e) {
  lv_obj_t *o = lv_event_get_target(e);
  const rose_t *s = lv_obj_get_user_data(o);
  if (!s) return;
  const ui_rose_data_t *d = &s->d;
  lv_area_t area;
  lv_obj_get_coords(o, &area);
  int32_t w = lv_area_get_width(&area), h = lv_area_get_height(&area);
  int32_t radius = ui_rose_radius_px(w < h ? w : h);
  if (radius < 10) return;
  const ui_colors_t *col = ui_theme();
  ctx_t c = {lv_event_get_layer(e), area.x1 + w / 2, area.y1 + h / 2, radius, radius / 172.0};
  bool hdg = d->have_heading && ok(d->heading);
  double hd = hdg ? DEG(d->heading) : 0;
  lv_color_t ink = hdg ? col->fg : col->dim;

  rim_ring(&c, ink, hdg ? LV_OPA_50 : LV_OPA_COVER);

  if (hdg) {
    static const char *const names[12] = {"N", "3", "6", "E", "12", "15", "S", "21", "24", "W", "30", "33"};
    const lv_font_t *nf = radius > 120 ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    const lv_font_t *cf = radius > 120 ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
    for (int b = 0; b < 360; b += 5) {
      double a = b - hd;
      double len = (b % 30 == 0 ? 22 : b % 10 == 0 ? 14 : 8) * c.k;
      line(&c, at(&c, c.r, a), at(&c, c.r - len, a), b % 30 == 0 ? 3 : 2, ink, false);
      if (b % 30 == 0) numeral(&c, names[b / 30], b % 90 == 0 ? cf : nf, at(&c, c.r - 44 * c.k, a), ink);
    }

    if (d->course && ok(d->dtk)) {
      double cr = d->dtk - d->heading, ux = sin(cr), uy = -cos(cr), vx = cos(cr), vy = sin(cr);
      double shift = ui_rose_xte_shift_px(d->xte_m, c.r), dot = ui_rose_dot_px(c.r);
#define P(sv, off) ((lv_point_precise_t){c.cx + ux * (sv) + vx * (off), c.cy + uy * (sv) + vy * (off)})
      for (int k = -2; k <= 2; k++)
        if (k) ring_dot(&c, P(0, k * dot), 5, ink);
      double r1 = 0.5 * c.r, r2 = c.r - 52 * c.k;
      int32_t lw = c.k > 0.9 ? 7 : 6;
      line(&c, P(r1, 0), P(r2, 0), lw, col->nav_course, false);
      line(&c, P(-r1, 0), P(-(r2 - 14 * c.k), 0), lw, col->nav_course, false);
      line(&c, P(r1, shift), P(-r1, shift), lw, col->nav_course, false);
      tri(&c, P(r2 + 14 * c.k, 0), P(r2 - 12 * c.k, -14 * c.k), P(r2 - 12 * c.k, 14 * c.k), col->nav_course);
#undef P
    }

    if (d->have_cog && ok(d->cog)) {
      double a = DEG(d->cog) - hd, half = 4.5;
      tri(&c, at(&c, c.r - 1, a), at(&c, c.r - 17 * c.k, a - half), at(&c, c.r - 17 * c.k, a + half), col->nav_course);
    }

    if (d->ap_bug && ok(d->ap_target)) {   // a notched rectangle outside the rim; the notch opens outward, its point toward the ring
      double a = DEG(d->ap_target) - hd, w4 = 4.5, wn = 2.6;
      double in = c.r + 1, out = c.r + 19 * c.k, apex = c.r + 10 * c.k;
      lv_point_precise_t A = at(&c, in, a - w4), B = at(&c, in, a + w4), C = at(&c, out, a + w4), D = at(&c, out, a + wn),
                         E = at(&c, apex, a), F = at(&c, out, a - wn), G = at(&c, out, a - w4);
      tri(&c, A, B, E, col->accent);
      tri(&c, B, C, D, col->accent);
      tri(&c, B, D, E, col->accent);
      tri(&c, A, E, F, col->accent);
      tri(&c, A, F, G, col->accent);
    }

    line(&c, at(&c, c.r + 4 * c.k, 0), at(&c, c.r - 26 * c.k, 0), 5, col->nav_lubber, false);
  }

  boat(&c, col);

  if (d->have_twa && ok(d->twa)) wind_marker(&c, d->twa, col->nav_true, true);
  if (d->have_awa && ok(d->awa)) wind_marker(&c, d->awa, col->nav_app, false);
}

static void on_delete(lv_event_t *e) {
  rose_t *s = lv_obj_get_user_data(lv_event_get_target(e));
  lv_free(s);
}

lv_obj_t *ui_rose_create(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  rose_t *s = lv_malloc_zeroed(sizeof *s);
  s->d.xte_m = NAN;
  lv_obj_set_user_data(o, s);
  lv_obj_add_event_cb(o, draw, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(o, on_delete, LV_EVENT_DELETE, NULL);
  return o;
}

void ui_rose_set(lv_obj_t *rose, const ui_rose_data_t *d) {
  rose_t *s = lv_obj_get_user_data(rose);
  if (!s) return;
  s->d = *d;
  lv_obj_invalidate(rose);
}
