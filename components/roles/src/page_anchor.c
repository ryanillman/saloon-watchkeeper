// The Anchor page: a plan view of the boat around the anchor, distance and bearing in the top corners, the limit
// control and the DROP / RAISE button at the bottom. The behaviour is in anchor_ctl.c; this draws it and forwards touches.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "anchor_ctl.h"
#include "anchor_page.h"
#include "platform.h"
#include "ui_swing.h"

PLATFORM_EXT_BSS static anchor_pt_t track[720];
_Static_assert(sizeof(anchor_pt_t) == sizeof(ui_swing_pt_t), "the plot reads the track in place");

static struct {
  lv_obj_t *plot, *dist_val, *dist_cap, *bear_val, *bear_cap, *mode, *limit, *minus, *plus, *main, *main_lbl, *bar,
      *info, *nosd;
  bool wide;
} w;

static bool feet(const role_binding_t *b) { return b && b->units && !strcmp(b->units->distance, "ft"); }

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *f, lv_color_t col, lv_text_align_t align, int32_t x,
                            int32_t y, int32_t width) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, col, 0);
  lv_obj_set_style_text_align(l, align, 0);
  lv_obj_set_width(l, width);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, "");
  return l;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, const lv_font_t *f, int32_t x, int32_t y, int32_t bw,
                             int32_t bh, lv_obj_t **label_out) {
  const ui_colors_t *c = ui_theme();
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_size(b, bw, bh);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_style_bg_color(b, c->strip_bg, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(b, c->dim, 0);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_radius(b, 12, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_opa(b, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_t *l = lv_label_create(b);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c->fg, 0);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  if (label_out) *label_out = l;
  return b;
}

static void on_step(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_PRESSED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;   // hold repeats
  anchor_ctl_step((int)(intptr_t)lv_event_get_user_data(e), platform_millis());
}

static void on_main(lv_event_t *e) {
  switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED: anchor_ctl_hold_begin(platform_millis()); break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST: anchor_ctl_hold_end(platform_millis()); break;
    case LV_EVENT_CLICKED: anchor_ctl_tap_main(platform_millis()); break;
    default: break;
  }
}

void anchor_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b) {
  (void)page;
  (void)b;
  memset(&w, 0, sizeof w);
  lv_obj_update_layout(area);
  int32_t W = lv_obj_get_width(area), H = lv_obj_get_height(area);
  const ui_colors_t *c = ui_theme();
  w.wide = sc == SIZE_WIDE;
  const lv_font_t *big = w.wide ? &lv_font_montserrat_48 : &lv_font_montserrat_28;
  const lv_font_t *mid = w.wide ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
  const lv_font_t *small = &lv_font_montserrat_14;

  int32_t pad = 8, bh = w.wide ? 72 : 60, strip = w.wide ? 0 : bh + 2 * pad;
  int32_t side = w.wide ? H - 2 * pad : (W < H - strip ? W : H - strip);
  int32_t px = (W - side) / 2, py = w.wide ? pad : 0;
  w.plot = ui_swing_create(area);
  lv_obj_set_size(w.plot, side, side);
  lv_obj_set_pos(w.plot, px, py);

  // Distance top-left, bearing top-right: over the plot on the square board, in the side columns on the wide ones.
  int32_t colw = w.wide ? px - 2 * pad : W / 3;
  w.dist_val = make_label(area, big, c->fg, LV_TEXT_ALIGN_LEFT, pad, pad, colw);
  w.dist_cap = make_label(area, small, c->dim, LV_TEXT_ALIGN_LEFT, pad, pad + lv_font_get_line_height(big) + 2, colw);
  w.bear_val = make_label(area, big, c->fg, LV_TEXT_ALIGN_RIGHT, W - pad - colw, pad, colw);
  w.bear_cap = make_label(area, small, c->dim, LV_TEXT_ALIGN_RIGHT, W - pad - colw,
                          pad + lv_font_get_line_height(big) + 2, colw);
  lv_label_set_text(w.dist_cap, "FROM ANCHOR");
  lv_label_set_text(w.bear_cap, "TO ANCHOR");

  int32_t main_w = w.wide ? colw : (W - 3 * pad) * 11 / 20;
  int32_t row_y = H - pad - bh;
  w.mode = make_label(area, mid, c->dim, LV_TEXT_ALIGN_CENTER, w.wide ? px : 0, w.wide ? H - pad - lv_font_get_line_height(mid) : H - strip - lv_font_get_line_height(mid) - 2, w.wide ? side : W);
  if (w.wide) lv_obj_set_y(w.mode, py + side - lv_font_get_line_height(mid));

  // Limit control: [-] limit [+].
  int32_t bw = w.wide ? (colw - pad) / 2 : 64;
  int32_t lim_x = pad, lim_y = row_y;
  if (w.wide) {
    w.limit = make_label(area, mid, c->fg, LV_TEXT_ALIGN_LEFT, pad, row_y - lv_font_get_line_height(mid) - pad, colw);
    w.minus = make_button(area, "-", &lv_font_montserrat_28, lim_x, lim_y, bw, bh, NULL);
    w.plus = make_button(area, "+", &lv_font_montserrat_28, lim_x + bw + pad, lim_y, bw, bh, NULL);
  } else {
    int32_t label_w = W - 3 * pad - main_w - 2 * bw - 2 * pad;
    w.minus = make_button(area, "-", &lv_font_montserrat_28, lim_x, lim_y, bw, bh, NULL);
    w.limit = make_label(area, mid, c->fg, LV_TEXT_ALIGN_CENTER, lim_x + bw + pad, lim_y + (bh - lv_font_get_line_height(mid)) / 2, label_w);
    w.plus = make_button(area, "+", &lv_font_montserrat_28, lim_x + bw + pad + label_w + pad, lim_y, bw, bh, NULL);
  }
  lv_obj_add_event_cb(w.minus, on_step, LV_EVENT_ALL, (void *)(intptr_t)-1);
  lv_obj_add_event_cb(w.plus, on_step, LV_EVENT_ALL, (void *)(intptr_t)1);

  int32_t mx = W - pad - main_w;
  w.main = make_button(area, "", mid, mx, row_y, main_w, bh, &w.main_lbl);
  lv_obj_set_style_clip_corner(w.main, true, 0);
  w.bar = lv_bar_create(w.main);   // the hold progress fills the button behind its label
  lv_obj_set_size(w.bar, main_w - 4, bh - 4);
  lv_obj_center(w.bar);
  lv_bar_set_range(w.bar, 0, 100);
  lv_obj_set_style_bg_opa(w.bar, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(w.bar, c->warn, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(w.bar, LV_OPA_70, LV_PART_INDICATOR);
  lv_obj_set_style_radius(w.bar, 10, LV_PART_INDICATOR);
  lv_obj_remove_flag(w.bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_move_background(w.bar);
  lv_obj_add_flag(w.bar, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(w.main, on_main, LV_EVENT_ALL, NULL);

  if (w.wide) w.info = make_label(area, small, c->dim, LV_TEXT_ALIGN_LEFT, pad, pad + lv_font_get_line_height(big) + 40, colw);
  w.nosd = make_label(area, small, c->warn, LV_TEXT_ALIGN_RIGHT, W - pad - 120, w.wide ? row_y - 2 * pad - 14 : H - strip - 2 * lv_font_get_line_height(small) - 4, 120);
}

static void dist_text(char *out, size_t n, double m, bool ft) {
  if (isnan(m)) snprintf(out, n, "--");
  else snprintf(out, n, ft ? "%.0f ft" : "%.0f m", ft ? m / 0.3048 : m);
}

static const char *mode_word(anchor_mode_t m) {
  switch (m) {
    case AM_NOT_SET: return "NOT SET";
    case AM_RESTORING: return "RESTORING";
    case AM_HOLDING: return "HOLDING";
    case AM_WARN: return "WARN";
    case AM_DRAGGING: return "DRAGGING";
    case AM_NO_LIMIT: return "NO LIMIT";
    default: return "NO DATA";
  }
}

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

static lv_color_t swing_color(ui_swing_state_t s) {
  const ui_colors_t *c = ui_theme();
  return s == UI_SWING_GREEN ? c->ok : s == UI_SWING_AMBER ? c->warn : s == UI_SWING_RED ? c->alarm : c->dim;
}

static void set_enabled(lv_obj_t *o, bool on) {
  if (on) lv_obj_remove_state(o, LV_STATE_DISABLED);
  else lv_obj_add_state(o, LV_STATE_DISABLED);
}

void anchor_refresh(role_binding_t *b, int64_t now_ms) {
  anchor_ctl_update(now_ms, track, sizeof track / sizeof track[0]);
  const anchor_ctl_state_t *s = anchor_ctl_state();
  if (!w.plot) return;   // controller only: the page is not on screen
  const anchor_view_t *v = &s->view;
  bool ft = feet(b);
  char t[40];

  ui_swing_state_t st = swing_state(v->mode);
  ui_swing_data_t d = {.track = (const ui_swing_pt_t *)track, .track_n = v->track_n,
                       .limit_m = v->mode == AM_NOT_SET ? NAN : v->limit_m, .warn_m = v->warn_m, .state = st,
                       .show_n = true};
  ui_swing_set(w.plot, &d);

  dist_text(t, sizeof t, v->distance_m, ft);
  lv_label_set_text(w.dist_val, t);
  if (isnan(v->bearing_rad)) snprintf(t, sizeof t, "--");
  else snprintf(t, sizeof t, "%.0f\xC2\xB0 T", fmod(v->bearing_rad * 180.0 / M_PI + 360.0, 360.0));
  lv_label_set_text(w.bear_val, t);
  lv_label_set_text(w.mode, mode_word(v->mode));
  lv_obj_set_style_text_color(w.mode, swing_color(st), 0);

  if (!v->circle && v->mode != AM_NO_DATA) {
    char zone[12];
    snprintf(zone, sizeof zone, "%s", v->zone);
    for (char *p = zone; *p; p++) *p = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
    snprintf(t, sizeof t, "ZONE: %s", zone);
  } else {
    char lim[16];
    dist_text(lim, sizeof lim, s->shown_limit_m, ft);
    snprintf(t, sizeof t, w.wide ? "LIMIT %s" : "%s", lim);
  }
  lv_label_set_text(w.limit, t);
  lv_obj_set_style_text_color(w.limit, s->limit_staged ? ui_theme()->warn : ui_theme()->fg, 0);
  set_enabled(w.minus, s->radius_enabled);
  set_enabled(w.plus, s->radius_enabled);

  const char *label = s->message[0] ? s->message : s->button == ANCHOR_BTN_DROP ? "DROP" : s->button == ANCHOR_BTN_RAISE ? "RAISE" : "--";
  lv_label_set_text(w.main_lbl, label);
  set_enabled(w.main, s->main_enabled);
  if (s->hold_pct > 0) {
    lv_obj_remove_flag(w.bar, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(w.bar, s->hold_pct, LV_ANIM_OFF);
  } else {
    lv_obj_add_flag(w.bar, LV_OBJ_FLAG_HIDDEN);
  }

  if (w.info) {   // the wide boards have room for the extras
    char a[24] = "", m[40] = "", r[40] = "", set[40] = "", info[128];
    if (!isnan(v->max_m)) { dist_text(a, sizeof a, v->max_m, ft); snprintf(m, sizeof m, "MAX %s", a); }
    if (v->set_ago_s >= 0) {
      long mins = (long)(v->set_ago_s / 60);
      if (mins < 60) snprintf(set, sizeof set, "SET %ld MIN AGO", mins);
      else snprintf(set, sizeof set, "SET %ldH%02ldM AGO", mins / 60, mins % 60);
    }
    if (!isnan(v->rode_m)) { dist_text(a, sizeof a, v->rode_m, ft); snprintf(r, sizeof r, "CHAIN %s", a); }
    snprintf(info, sizeof info, "%s\n%s\n%s", m, set, r);
    lv_label_set_text(w.info, info);
  }
  lv_label_set_text(w.nosd, s->no_sd ? "NO SD" : "");
}

const char *anchor_page_button_text(void) { return w.main_lbl ? lv_label_get_text(w.main_lbl) : NULL; }
