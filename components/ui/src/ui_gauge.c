// Dial, strip, and readout widgets. Each is one lv_obj drawn in its draw event,
// plus a fault overlay child that covers it when the field has no valid data.
#include "ui_gauge.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_scale.h"
#include "ui_theme.h"

typedef enum { W_DIAL, W_STRIP, W_READOUT } kind_t;

typedef struct {
  kind_t kind;
  const ui_gauge_spec_t *spec;   // dial and strip
  const char *name, *unit;       // readout
  uint8_t decimals;
  uint8_t n;                     // needles; 1 for strip and readout
  const char *needle_label[2];
  double value[2];
  bool valid[2];
  bool off[2];                   // engine off: shown dim, not as a fault
  bool vertical;
  uint8_t alert;                 // alert_state_t of the notification on this field
  bool text_mode;                // readout showing a string (ui_readout_set_text)
  char text[2][24];
  lv_obj_t *fault;               // overlay: red cross and "<LABEL> FAIL"
  lv_obj_t *fault_label;
} gauge_t;

static gauge_t *G(lv_obj_t *o) { return lv_obj_get_user_data(o); }

static const lv_font_t *font_for(int32_t px) {
  if (px >= 56) return &lv_font_montserrat_48;
  if (px >= 32) return &lv_font_montserrat_28;
  if (px >= 22) return &lv_font_montserrat_20;
  return &lv_font_montserrat_14;
}

static lv_color_t value_color(const gauge_t *g) {
  const ui_colors_t *c = ui_theme();
  return g->alert >= 3 ? c->alarm : g->alert >= 1 ? c->warn : c->fg;
}

static void text_at(lv_layer_t *layer, const char *txt, const lv_font_t *f, lv_color_t c,
                    int32_t x, int32_t y, lv_text_align_t align) {
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.text = txt;
  d.font = f;
  d.color = c;
  d.align = align;
  int32_t w = lv_text_get_width(txt, strlen(txt), f, 0) + 4;
  int32_t h = lv_font_get_line_height(f);
  int32_t x1 = align == LV_TEXT_ALIGN_CENTER ? x - w / 2 : align == LV_TEXT_ALIGN_RIGHT ? x - w : x;
  lv_area_t a = {x1, y - h / 2, x1 + w, y + h / 2};
  lv_draw_label(layer, &d, &a);
}

static void line(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t w, lv_color_t c) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.p1.x = x1; d.p1.y = y1; d.p2.x = x2; d.p2.y = y2;
  d.width = w;
  d.color = c;
  d.round_start = d.round_end = 1;
  lv_draw_line(layer, &d);
}

static void rect(lv_layer_t *layer, const lv_area_t *a, lv_color_t c, int32_t radius) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = c;
  d.bg_opa = LV_OPA_COVER;
  d.radius = radius;
  lv_draw_rect(layer, &d, a);
}

static void format(gauge_t *g, uint8_t i) {
  if (g->off[i]) {
    snprintf(g->text[i], sizeof g->text[i], "OFF");
    return;
  }
  if (!g->valid[i]) {
    g->text[i][0] = 0;
    return;
  }
  uint8_t dec = g->spec ? g->spec->decimals : g->decimals;
  snprintf(g->text[i], sizeof g->text[i], "%.*f", dec, g->value[i]);
}

// ---- fault overlay ----

static void draw_cross(lv_event_t *e) {
  lv_obj_t *o = lv_event_get_target(e);
  lv_layer_t *layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  int32_t w = lv_area_get_width(&a) > 300 ? 6 : 4;
  lv_color_t c = ui_theme()->fault_cross;
  line(layer, a.x1 + w, a.y1 + w, a.x2 - w, a.y2 - w, w, c);
  line(layer, a.x2 - w, a.y1 + w, a.x1 + w, a.y2 - w, w, c);
}

static void make_fault(lv_obj_t *field, gauge_t *g, const char *label) {
  g->fault = lv_obj_create(field);
  lv_obj_remove_style_all(g->fault);
  lv_obj_set_size(g->fault, LV_PCT(100), LV_PCT(100));
  lv_obj_remove_flag(g->fault, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(g->fault, draw_cross, LV_EVENT_DRAW_MAIN, NULL);
  g->fault_label = lv_label_create(g->fault);
  lv_label_set_text_fmt(g->fault_label, "%s FAIL", label);
  lv_obj_set_style_bg_opa(g->fault_label, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(g->fault_label, lv_color_black(), 0);
  lv_obj_set_style_pad_hor(g->fault_label, 6, 0);
  lv_obj_set_style_pad_ver(g->fault_label, 2, 0);
  lv_obj_center(g->fault_label);
}

// Font and colour of the FAIL label; only on create, resize, or palette change.
static void style_fault(lv_obj_t *o) {
  gauge_t *g = G(o);
  int32_t h = lv_obj_get_height(o), w = lv_obj_get_width(o);
  lv_obj_set_style_text_font(g->fault_label, font_for((w < h ? w : h) / 5), 0);
  lv_obj_set_style_text_color(g->fault_label, ui_theme()->fault_label, 0);
}

static void sync_fault(lv_obj_t *o) {
  gauge_t *g = G(o);
  bool any = false;   // the whole field is flagged only when nothing is live or off
  for (uint8_t i = 0; i < g->n; i++) any |= g->valid[i] || g->off[i];
  if (any) lv_obj_add_flag(g->fault, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(g->fault, LV_OBJ_FLAG_HIDDEN);
  lv_obj_invalidate(o);
}

// The refresh calls every setter 10 times a second. Redraw only on a change
// the glass can show: a different state, or different digits.
static bool unchanged(gauge_t *g, uint8_t i, double v, bool valid, bool off) {
  if (g->off[i] != off || g->valid[i] != valid) return false;
  if (off || !valid) return true;
  if (g->value[i] == v) return true;
  char next[24];
  uint8_t dec = g->spec ? g->spec->decimals : g->decimals;
  snprintf(next, sizeof next, "%.*f", dec, v);
  if (strcmp(next, g->text[i]) != 0) return false;
  if (g->kind == W_READOUT) return true;   // nothing else to move
  // Dial and strip: the pointer moves with the value even when the digits do not.
  double lo = g->spec->lo, hi = g->spec->hi;
  return fabs(ui_scale_fraction(v, lo, hi) - ui_scale_fraction(g->value[i], lo, hi)) < 0.002;
}

static void on_delete(lv_event_t *e) { free(lv_event_get_user_data(e)); }
static void on_resize(lv_event_t *e) {
  style_fault(lv_event_get_target(e));
  sync_fault(lv_event_get_target(e));
}

static lv_obj_t *base(lv_obj_t *parent, gauge_t *g, const char *fault_label, int32_t w, int32_t h,
                      lv_event_cb_t draw) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, w, h);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_user_data(o, g);
  lv_obj_add_event_cb(o, draw, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(o, on_delete, LV_EVENT_DELETE, g);
  lv_obj_add_event_cb(o, on_resize, LV_EVENT_SIZE_CHANGED, NULL);
  make_fault(o, g, fault_label);
  style_fault(o);
  sync_fault(o);
  return o;
}

// ---- dial ----

static double start_angle(ui_sweep_t s) { return s == SWEEP_FULL ? 90 : 90 + (360 - (double)s) / 2; }

static void polar(int32_t cx, int32_t cy, double r, double deg, int32_t *x, int32_t *y) {
  double a = deg * M_PI / 180.0;
  *x = cx + (int32_t)lround(cos(a) * r);
  *y = cy + (int32_t)lround(sin(a) * r);
}

static void draw_dial(lv_event_t *e) {
  lv_obj_t *o = lv_event_get_target(e);
  gauge_t *g = G(o);
  const ui_gauge_spec_t *s = g->spec;
  const ui_colors_t *c = ui_theme();
  lv_layer_t *layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  int32_t w = lv_area_get_width(&a), h = lv_area_get_height(&a);
  int32_t r = (w < h ? w : h) / 2 - 4;
  int32_t cx = a.x1 + w / 2, cy = a.y1 + h / 2;
  double a0 = start_angle(s->sweep), sweep = s->sweep;
  int32_t ring = r / 14 + 2;

  // Bands on the outer ring, over a dim track.
  lv_draw_arc_dsc_t arc;
  lv_draw_arc_dsc_init(&arc);
  arc.center.x = cx;
  arc.center.y = cy;
  arc.radius = r;
  arc.width = ring;
  arc.color = c->strip_bg;
  arc.start_angle = a0;
  arc.end_angle = a0 + sweep;
  lv_draw_arc(layer, &arc);
  for (uint8_t i = 0; i < s->band_count; i++) {
    double f0 = ui_scale_fraction(s->bands[i].lo, s->lo, s->hi);
    double f1 = ui_scale_fraction(s->bands[i].hi, s->lo, s->hi);
    if (f1 <= f0) continue;
    arc.color = s->bands[i].color;
    arc.start_angle = a0 + f0 * sweep;
    arc.end_angle = a0 + f1 * sweep;
    lv_draw_arc(layer, &arc);
  }

  // Ticks and numerals.
  ui_ticks_t t;
  ui_scale_ticks(s->lo, s->hi, 5, &t);
  const lv_font_t *num_font = font_for(r / 6);
  if (t.minor_step > 0)
    for (double v = s->lo; v <= s->hi + t.minor_step * 1e-6; v += t.minor_step) {
      int32_t x1, y1, x2, y2;
      double ang = a0 + ui_scale_fraction(v, s->lo, s->hi) * sweep;
      polar(cx, cy, r - ring, ang, &x1, &y1);
      polar(cx, cy, r - ring - r / 16, ang, &x2, &y2);
      line(layer, x1, y1, x2, y2, 2, c->dim);
    }
  for (uint8_t i = 0; i < t.major_count; i++) {
    int32_t x1, y1, x2, y2, tx, ty;
    double ang = a0 + ui_scale_fraction(t.major[i], s->lo, s->hi) * sweep;
    polar(cx, cy, r - ring, ang, &x1, &y1);
    polar(cx, cy, r - ring - r / 8, ang, &x2, &y2);
    line(layer, x1, y1, x2, y2, 3, c->fg);
    char num[16];
    double shown = t.major[i];
    if (s->hi >= 1000) shown /= 1000;   // RPM-style scales read "x1000"
    snprintf(num, sizeof num, "%g", shown);
    polar(cx, cy, r - ring - r / 8 - r / 7, ang, &tx, &ty);
    text_at(layer, num, num_font, c->fg, tx, ty, LV_TEXT_ALIGN_CENTER);
  }

  // Unit, under the hub.
  char unit[24];
  snprintf(unit, sizeof unit, "%s%s", s->unit, s->hi >= 1000 ? " x1000" : "");
  text_at(layer, unit, font_for(r / 8), c->dim, cx, cy - r / 3, LV_TEXT_ALIGN_CENTER);

  // Needles.
  static const int32_t NEEDLE_W[] = {6, 6};
  for (uint8_t i = 0; i < g->n; i++) {
    if (!g->valid[i]) continue;   // a hidden needle cannot be read as a value
    int32_t x, y, bx, by;
    double ang = a0 + ui_scale_fraction(g->value[i], s->lo, s->hi) * sweep;
    polar(cx, cy, r - ring - 4, ang, &x, &y);
    polar(cx, cy, r / 8, ang + 180, &bx, &by);
    line(layer, bx, by, x, y, NEEDLE_W[i], i == 0 ? c->fg : c->accent);
  }
  int32_t hub = r / 12 + 3;
  lv_area_t ha = {cx - hub, cy - hub, cx + hub, cy + hub};
  rect(layer, &ha, c->strip_bg, LV_RADIUS_CIRCLE);

  // Digits, one row per needle, in the lower part of the face.
  const lv_font_t *dig = font_for(g->n > 1 ? r / 5 : r / 3);
  int32_t row_h = lv_font_get_line_height(dig);
  int32_t y0 = cy + r * 2 / 5 + (g->n > 1 ? -row_h / 2 : 0);
  bool any = false;
  for (uint8_t i = 0; i < g->n; i++) any |= g->valid[i] || g->off[i];
  for (uint8_t i = 0; i < g->n && any; i++) {
    int32_t y = y0 + i * row_h;
    char row[48];
    lv_color_t col = i == 0 ? c->fg : c->accent;
    if (g->off[i]) {
      snprintf(row, sizeof row, "%s%sOFF", g->needle_label[i] ? g->needle_label[i] : "", g->needle_label[i] ? " " : "");
      text_at(layer, row, font_for(r / 7), c->dim, cx, y, LV_TEXT_ALIGN_CENTER);
    } else if (g->valid[i]) {
      snprintf(row, sizeof row, "%s%s%s", g->needle_label[i] ? g->needle_label[i] : "",
               g->needle_label[i] ? " " : "", g->text[i]);
      text_at(layer, row, dig, col, cx, y, LV_TEXT_ALIGN_CENTER);
    } else {   // this needle failed; its neighbour stays live
      snprintf(row, sizeof row, "%s%s%s FAIL", g->needle_label[i] ? g->needle_label[i] : "",
               g->needle_label[i] ? " " : "", s->label);
      // A cross wider and taller than the label box, so it reads as crossed out.
      const lv_font_t *ff = font_for(r / 7);
      int32_t half = lv_text_get_width(row, strlen(row), ff, 0) / 2 + 4;
      int32_t fh = lv_font_get_line_height(ff) / 2;
      int32_t cw = half + r / 5, ch = row_h * 3 / 4;
      line(layer, cx - cw, y - ch, cx + cw, y + ch, 4, c->fault_cross);
      line(layer, cx + cw, y - ch, cx - cw, y + ch, 4, c->fault_cross);
      lv_area_t box = {cx - half, y - fh, cx + half, y + fh};
      rect(layer, &box, lv_color_black(), 0);
      text_at(layer, row, ff, c->fault_label, cx, y, LV_TEXT_ALIGN_CENTER);
    }
  }
}

lv_obj_t *ui_dial_create(lv_obj_t *parent, const ui_gauge_spec_t *spec, uint8_t needles,
                         const char *const *labels) {
  gauge_t *g = calloc(1, sizeof *g);
  g->kind = W_DIAL;
  g->spec = spec;
  g->n = needles < 1 ? 1 : needles > 2 ? 2 : needles;
  for (uint8_t i = 0; labels && i < g->n; i++) g->needle_label[i] = labels[i];
  return base(parent, g, spec->label, 300, 300, draw_dial);
}

void ui_dial_set_off(lv_obj_t *o, uint8_t i) {
  gauge_t *g = G(o);
  if (i >= g->n || unchanged(g, i, 0, false, true)) return;
  g->valid[i] = false;
  g->off[i] = true;
  format(g, i);
  sync_fault(o);
}

void ui_dial_set(lv_obj_t *o, uint8_t i, double v, bool valid) {
  gauge_t *g = G(o);
  valid = valid && isfinite(v);
  if (i >= g->n || unchanged(g, i, v, valid, false)) return;
  g->off[i] = false;
  g->value[i] = v;
  g->valid[i] = valid && isfinite(v);
  format(g, i);
  sync_fault(o);
}

// ---- strip ----

static void draw_strip(lv_event_t *e) {
  lv_obj_t *o = lv_event_get_target(e);
  gauge_t *g = G(o);
  const ui_gauge_spec_t *s = g->spec;
  const ui_colors_t *c = ui_theme();
  lv_layer_t *layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  int32_t w = lv_area_get_width(&a), h = lv_area_get_height(&a);

  // Text: label (dim) and value; the value takes the largest font that fits.
  static const lv_font_t *const FONTS[] = {&lv_font_montserrat_48, &lv_font_montserrat_28,
                                           &lv_font_montserrat_20, &lv_font_montserrat_14};
  const lv_font_t *lf = (g->vertical ? w : h) >= 70 ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
  int32_t lh = lv_font_get_line_height(lf);
  int32_t lw = lv_text_get_width(s->label, strlen(s->label), lf, 0);
  char v[40];
  if (g->off[0]) snprintf(v, sizeof v, "OFF");
  else snprintf(v, sizeof v, "%s %s", g->valid[0] ? g->text[0] : "", s->unit);
  const lv_font_t *vf = &lv_font_montserrat_14;
  for (size_t i = 0; i < sizeof FONTS / sizeof FONTS[0]; i++) {
    int32_t fh = lv_font_get_line_height(FONTS[i]);
    int32_t fw = lv_text_get_width(v, strlen(v), FONTS[i], 0);
    bool fits = g->vertical ? (fw <= w - 4 && fh <= h / 4) : (fh <= h * 55 / 100 && fw <= w - lw - 16);
    if (fits) {
      vf = FONTS[i];
      break;
    }
  }
  int32_t vh = lv_font_get_line_height(vf);

  // Tape: horizontal runs left to right under the text row; vertical runs
  // bottom to top between the value (top) and the label (bottom).
  lv_area_t tape;
  if (g->vertical) {
    int32_t tw = w / 4;
    tape = (lv_area_t){a.x1 + w / 2 - tw / 2, a.y1 + vh + 8, a.x1 + w / 2 + tw / 2, a.y2 - lh - 6};
  } else {
    int32_t th = h - vh - 6;
    if (th > h / 3) th = h / 3;
    if (th < 6) th = 6;
    tape = (lv_area_t){a.x1 + 4, a.y2 - th - 2, a.x2 - 4, a.y2 - 2};
  }
  rect(layer, &tape, c->strip_bg, 2);
  int32_t len = g->vertical ? lv_area_get_height(&tape) : lv_area_get_width(&tape);
  for (uint8_t i = 0; i < s->band_count; i++) {
    double f0 = ui_scale_fraction(s->bands[i].lo, s->lo, s->hi), f1 = ui_scale_fraction(s->bands[i].hi, s->lo, s->hi);
    if (f1 <= f0) continue;
    lv_area_t b = tape;
    if (g->vertical) {
      b.y2 = tape.y2 - (int32_t)(f0 * len);
      b.y1 = tape.y2 - (int32_t)(f1 * len);
    } else {
      b.x1 = tape.x1 + (int32_t)(f0 * len);
      b.x2 = tape.x1 + (int32_t)(f1 * len);
    }
    rect(layer, &b, s->bands[i].color, 0);
  }
  ui_ticks_t t;
  ui_scale_ticks(s->lo, s->hi, 5, &t);
  for (uint8_t i = 0; i < t.major_count; i++) {
    int32_t p = (int32_t)(ui_scale_fraction(t.major[i], s->lo, s->hi) * len);
    if (g->vertical) line(layer, tape.x1, tape.y2 - p, tape.x1 + (tape.x2 - tape.x1) / 3, tape.y2 - p, 2, c->dim);
    else line(layer, tape.x1 + p, tape.y2, tape.x1 + p, tape.y2 - (tape.y2 - tape.y1) / 3, 2, c->dim);
  }

  if (g->vertical) {
    text_at(layer, s->label, lf, c->dim, a.x1 + w / 2, a.y2 - lh / 2, LV_TEXT_ALIGN_CENTER);
  } else {
    text_at(layer, s->label, lf, c->dim, a.x1 + 4, a.y1 + vh - lh / 2 - 2, LV_TEXT_ALIGN_LEFT);
  }
  if (g->off[0]) {   // quiet: label-sized and dim, so it never reads as a value or an alarm
    if (g->vertical) text_at(layer, "OFF", lf, c->dim, a.x1 + w / 2, a.y1 + vh / 2, LV_TEXT_ALIGN_CENTER);
    else text_at(layer, "OFF", lf, c->dim, a.x2 - 4, a.y1 + vh - lh / 2 - 2, LV_TEXT_ALIGN_RIGHT);
    return;
  }
  if (!g->valid[0]) return;   // the fault overlay covers the rest

  // Fill and pointer (clamped to the tape); digits show the true value.
  int32_t p = (int32_t)(ui_scale_fraction(g->value[0], s->lo, s->hi) * len);
  int32_t ps = g->vertical ? w / 8 : h / 6;
  if (g->vertical) {
    lv_area_t fill = {tape.x1 + 2, tape.y2 - p, tape.x2 - 2, tape.y2};
    rect(layer, &fill, s->has_color ? s->color : c->fg, 0);
    line(layer, tape.x2 + 2, tape.y2 - p, tape.x2 + ps + 2, tape.y2 - p, 4, c->fg);
  } else {
    lv_area_t mark = {tape.x1 + p - 2, tape.y1 - ps, tape.x1 + p + 2, tape.y2};
    rect(layer, &mark, c->fg, 0);
  }
  if (g->vertical) text_at(layer, v, vf, value_color(g), a.x1 + w / 2, a.y1 + vh / 2, LV_TEXT_ALIGN_CENTER);
  else text_at(layer, v, vf, value_color(g), a.x2 - 4, a.y1 + vh / 2, LV_TEXT_ALIGN_RIGHT);
}

lv_obj_t *ui_strip_create(lv_obj_t *parent, const ui_gauge_spec_t *spec, bool vertical) {
  gauge_t *g = calloc(1, sizeof *g);
  g->kind = W_STRIP;
  g->spec = spec;
  g->n = 1;
  g->vertical = vertical;
  return base(parent, g, spec->label, vertical ? 90 : 220, vertical ? 240 : 64, draw_strip);
}

void ui_strip_set_off(lv_obj_t *o) {
  gauge_t *g = G(o);
  if (unchanged(g, 0, 0, false, true)) return;
  g->valid[0] = false;
  g->off[0] = true;
  format(g, 0);
  sync_fault(o);
}

void ui_strip_set(lv_obj_t *o, double v, bool valid) {
  gauge_t *g = G(o);
  valid = valid && isfinite(v);
  if (unchanged(g, 0, v, valid, false)) return;
  g->off[0] = false;
  g->value[0] = v;
  g->valid[0] = valid && isfinite(v);
  format(g, 0);
  sync_fault(o);
}

// ---- readout ----

static void draw_readout(lv_event_t *e) {
  lv_obj_t *o = lv_event_get_target(e);
  gauge_t *g = G(o);
  const ui_colors_t *c = ui_theme();
  lv_layer_t *layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  int32_t h = lv_area_get_height(&a);
  const lv_font_t *lf = font_for(h / 4), *vf = font_for(h * 6 / 10);
  text_at(layer, g->name, lf, c->dim, a.x1 + 4, a.y1 + lv_font_get_line_height(lf) / 2, LV_TEXT_ALIGN_LEFT);
  if (!g->valid[0]) return;
  char v[48];
  if (g->text_mode) snprintf(v, sizeof v, "%s", g->text[0]);
  else snprintf(v, sizeof v, "%s %s", g->text[0], g->unit);
  // A value wider than the field steps down to a smaller font rather than running off it.
  static const lv_font_t *const steps[] = {&lv_font_montserrat_48, &lv_font_montserrat_28, &lv_font_montserrat_20,
                                           &lv_font_montserrat_14};
  size_t s0 = 0;
  while (s0 < 3 && steps[s0] != vf) s0++;
  for (size_t k = s0; k < 3 && lv_text_get_width(v, strlen(v), vf, 0) + 8 > lv_area_get_width(&a); k++) vf = steps[k + 1];
  text_at(layer, v, vf, value_color(g), a.x1 + 4, a.y2 - lv_font_get_line_height(vf) / 2, LV_TEXT_ALIGN_LEFT);
}

lv_obj_t *ui_readout_create(lv_obj_t *parent, const char *label, const char *unit, uint8_t decimals) {
  gauge_t *g = calloc(1, sizeof *g);
  g->kind = W_READOUT;
  g->name = label;
  g->unit = unit;
  g->decimals = decimals;
  g->n = 1;
  return base(parent, g, label, 180, 80, draw_readout);
}

void ui_readout_set(lv_obj_t *o, double v, bool valid) {
  gauge_t *g = G(o);
  valid = valid && isfinite(v);
  if (unchanged(g, 0, v, valid, false)) return;
  g->value[0] = v;
  g->valid[0] = valid && isfinite(v);
  format(g, 0);
  sync_fault(o);
}

void ui_readout_set_text(lv_obj_t *o, const char *text, bool valid) {
  gauge_t *g = G(o);
  valid = valid && text && text[0];
  char next[sizeof g->text[0]] = "";
  if (valid) snprintf(next, sizeof next, "%s", text);
  if (g->text_mode && g->valid[0] == valid && strcmp(next, g->text[0]) == 0) return;
  g->text_mode = true;
  g->valid[0] = valid;
  memcpy(g->text[0], next, sizeof next);
  sync_fault(o);
}

void ui_field_set_fault_text(lv_obj_t *o, const char *text) { lv_label_set_text(G(o)->fault_label, text); }

void ui_field_set_alert(lv_obj_t *o, uint8_t state) {
  gauge_t *g = G(o);
  if (state > 4) state = 4;
  if (g->alert == state) return;
  g->alert = state;
  lv_obj_invalidate(o);
}

lv_color_t ui_field_value_color(lv_obj_t *o) { return value_color(G(o)); }

// ---- queries ----

bool ui_field_faulted(lv_obj_t *o) { return !lv_obj_has_flag(G(o)->fault, LV_OBJ_FLAG_HIDDEN); }
const char *ui_field_fault_text(lv_obj_t *o) { return lv_label_get_text(G(o)->fault_label); }
bool ui_dial_needle_visible(lv_obj_t *o, uint8_t i) { return i < G(o)->n && G(o)->valid[i]; }
bool ui_dial_needle_faulted(lv_obj_t *o, uint8_t i) { return i < G(o)->n && !G(o)->valid[i] && !G(o)->off[i]; }
double ui_dial_needle_fraction(lv_obj_t *o, uint8_t i) {
  gauge_t *g = G(o);
  return ui_scale_fraction(g->value[i], g->spec->lo, g->spec->hi);
}
const char *ui_dial_text(lv_obj_t *o, uint8_t i) { return G(o)->text[i]; }
double ui_strip_fraction(lv_obj_t *o) {
  gauge_t *g = G(o);
  return ui_scale_fraction(g->value[0], g->spec->lo, g->spec->hi);
}
const char *ui_strip_text(lv_obj_t *o) { return G(o)->text[0]; }
const char *ui_readout_text(lv_obj_t *o) { return G(o)->text[0]; }
