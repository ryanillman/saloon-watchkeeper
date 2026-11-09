#include "ui_shell.h"
#include <stdio.h>
#include <string.h>

static struct {
  const ui_role_view_t *roles;
  ui_nav_t nav;
  size_class_t sc;
  palette_t pal;
  void (*strip_tap)(void);
  void (*sos_cb)(void);
  lv_obj_t *screen, *strip, *tap, *home_btn, *home_icon, *sos, *name, *dots, *clock, *src, *can_dot, *status, *content;
  ui_source_t sk, can;   // the two data sources and what they are doing
  uint32_t sources_gen;
  bool text_hidden;   // the source text gave way to a crowded strip
  bool clock_has_text, clock_yield;   // a time was set; the clock gave way to an error message
  char status_text[32], over_text[24];   // the app's status, and a temporary replacement
  lv_color_t status_color, over_color;
  bool over;
  uint8_t home;   // the role the home button returns to (page 0)
} s;

#define SRC_DOT 16   // the source dot's diameter

static void show_status(void);

// Filled = live, hollow ring = connecting, ring with a cross = offline. Signal K is a circle, the bus a rounded square,
// so the two are told apart by shape, not colour (the night palette has one red).
static void draw_dot(lv_obj_t *dot, ui_source_t src, bool square) {
  if (!src.shown) {
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
  const ui_colors_t *c = ui_theme_colors(s.pal);
  bool live = src.state == UI_SRC_LIVE;
  lv_color_t col = live ? c->fg : src.state == UI_SRC_CONNECTING ? c->warn : c->alarm;
  lv_obj_clean(dot);
  lv_obj_set_style_radius(dot, square ? 4 : LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, col, 0);
  lv_obj_set_style_bg_opa(dot, live ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(dot, col, 0);
  lv_obj_set_style_border_width(dot, live ? 0 : 2, 0);
  if (src.state == UI_SRC_OFFLINE) {
    static const lv_point_precise_t slash[] = {{2, 2}, {9, 9}}, back[] = {{9, 2}, {2, 9}};
    const lv_point_precise_t *strokes[] = {slash, back};
    for (int i = 0; i < 2; i++) {
      lv_obj_t *l = lv_line_create(dot);
      lv_line_set_points(l, strokes[i], 2);
      lv_obj_set_style_line_width(l, 2, 0);
      lv_obj_set_style_line_color(l, col, 0);
      lv_obj_set_pos(l, 0, 0);
    }
  }
}

// The dots sit at the strip's right edge (Signal K outermost); the status text is left of the leftmost one.
static void place_sources(void) {
  int shown = (s.sk.shown ? 1 : 0) + (s.can.shown ? 1 : 0);
  lv_obj_align(s.src, LV_ALIGN_RIGHT_MID, 0, 0);
  if (s.can.shown) {
    if (s.sk.shown) lv_obj_align_to(s.can_dot, s.src, LV_ALIGN_OUT_LEFT_MID, -8, 0);
    else lv_obj_align(s.can_dot, LV_ALIGN_RIGHT_MID, 0, 0);
  }
  lv_obj_align(s.status, LV_ALIGN_RIGHT_MID, -(shown ? shown * SRC_DOT + (shown - 1) * 8 + 8 : 0), 0);
}

// Names the sources that are in the worst state present (offline before connecting), e.g. "SK + CAN OFFLINE".
static void source_status(char *out, size_t n, lv_color_t *col) {
  const ui_colors_t *c = ui_theme_colors(s.pal);
  ui_src_t worst = UI_SRC_LIVE;
  const ui_source_t *all[2] = {&s.sk, &s.can};
  for (int i = 0; i < 2; i++) {
    if (!all[i]->shown || all[i]->state == UI_SRC_LIVE) continue;
    if (all[i]->state == UI_SRC_OFFLINE || worst == UI_SRC_LIVE) worst = all[i]->state;
  }
  out[0] = 0;
  *col = c->fg;
  if (worst == UI_SRC_LIVE) return;
  const char *names[2] = {"SK", "NMEA 2k"};
  size_t len = 0;
  for (int i = 0; i < 2; i++)
    if (all[i]->shown && all[i]->state == worst) len += (size_t)snprintf(out + len, n - len, "%s%s", len ? " + " : "", names[i]);
  snprintf(out + len, n - len, " %s", worst == UI_SRC_CONNECTING ? "CONNECTING" : "OFFLINE");
  *col = worst == UI_SRC_CONNECTING ? c->warn : c->alarm;
}

static void draw_source(void) {
  draw_dot(s.src, s.sk, false);
  draw_dot(s.can_dot, s.can, true);
  source_status(s.status_text, sizeof s.status_text, &s.status_color);
  place_sources();
  s.sources_gen++;
  show_status();
}

static void place_clock(void) {
  lv_obj_update_layout(s.strip);   // the dots' and the title's widths are their content
  if (s.sc == SIZE_SQUARE) {
    // The square strip has no room for centred dots with the clock hanging off their left: a long title (NAVIGATION)
    // ran into it. Pack title, dots and clock from the left instead. The SOS button takes the title's place while it
    // shows, so while it does the cluster starts after whichever is wider.
    bool sos_up = !lv_obj_has_flag(s.sos, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *left = sos_up && lv_obj_get_x2(s.sos) > lv_obj_get_x2(s.name) ? s.sos : s.name;
    lv_obj_align_to(s.dots, left, LV_ALIGN_OUT_RIGHT_MID, 16, 0);
    lv_obj_update_layout(s.strip);
    lv_obj_align_to(s.clock, s.dots, LV_ALIGN_OUT_RIGHT_MID, 14, 0);
    return;
  }
  lv_obj_align_to(s.clock, s.dots, LV_ALIGN_OUT_LEFT_MID, -16, 0);
}

static void apply_palette(void) {
  ui_theme_set_current(s.pal);
  const ui_colors_t *c = ui_theme_colors(s.pal);
  lv_obj_set_style_bg_color(s.screen, c->bg, 0);
  lv_obj_set_style_text_color(s.screen, c->fg, 0);
  lv_obj_set_style_bg_color(s.strip, c->strip_bg, 0);
  lv_obj_set_style_bg_opa(s.content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_text_color(s.clock, c->fg, 0);
  lv_obj_set_style_text_color(s.home_icon, c->fg, 0);
  lv_obj_set_style_bg_color(s.sos, c->alarm, 0);
  lv_obj_set_style_text_color(lv_obj_get_child(s.sos, 0), c->bg, 0);
  draw_source();
}

static void draw_dots(void) {
  const ui_colors_t *c = ui_theme_colors(s.pal);
  lv_obj_clean(s.dots);
  uint8_t pages = s.nav.role_count == 0 ? 0 : s.nav.page_count[s.nav.role];
  if (pages <= 1) {
    place_clock();   // no dots, but the clock still follows the (new) title
    return;
  }
  int32_t d = s.sc == SIZE_WIDE ? 10 : pages > 4 ? 6 : 8;   // a role with many pages (Power) gets smaller dots on the square board
  lv_obj_set_style_pad_column(s.dots, s.sc != SIZE_WIDE && pages > 4 ? 5 : 8, 0);
  for (uint8_t i = 0; i < pages; i++) {
    lv_obj_t *dot = lv_obj_create(s.dots);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, d, d);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, i == s.nav.page ? c->fg : c->dim, 0);
  }
  place_clock();
}

// LVGL makes every container scrollable, and a scrollable child that overflows
// captures the drag as a scroll, so the swipe never becomes a gesture.
// Pages do not scroll; clear the flag on everything a page builds.
static lv_obj_tree_walk_res_t no_scroll(lv_obj_t *obj, void *user) {
  (void)user;
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return LV_OBJ_TREE_WALK_NEXT;
}

static void rebuild(void) {
  lv_obj_clean(s.content);
  lv_label_set_text(s.name, s.nav.role_count == 0 ? "" : s.roles[s.nav.role].name);   // before the dots: they sit after it
  draw_dots();
  if (s.nav.role_count == 0) return;
  const ui_role_view_t *r = &s.roles[s.nav.role];
  if (r->build_page) r->build_page(s.content, s.nav.page, s.sc, r->ctx);
  lv_obj_tree_walk(s.content, no_scroll, NULL);
}

static void (*settle_cb)(void);
static uint32_t settle_ms;
static lv_timer_t *settle_timer;

static void settle_fire(lv_timer_t *t) {
  (void)t;
  settle_timer = NULL;   // a repeat-count-1 timer deletes itself
  if (settle_cb) settle_cb();
}

static void settle_restart(void) {
  if (settle_timer) {
    lv_timer_delete(settle_timer);
    settle_timer = NULL;
  }
  if (!settle_cb) return;
  settle_timer = lv_timer_create(settle_fire, settle_ms, NULL);
  lv_timer_set_repeat_count(settle_timer, 1);
}

void ui_shell_set_settle_cb(void (*cb)(void), uint32_t delay_ms) {
  settle_cb = cb;
  settle_ms = delay_ms;
  if (!cb && settle_timer) {
    lv_timer_delete(settle_timer);
    settle_timer = NULL;
  }
}

void ui_shell_move(nav_dir_t dir) {
  if (ui_nav_move(&s.nav, dir)) {
    rebuild();
    settle_restart();
  }
}

// Gestures and keys arrive during an event on an object rebuild() may delete,
// so the move runs after the event finishes.
static void deferred_move(void *p) { ui_shell_move((nav_dir_t)(uintptr_t)p); }

static void on_gesture(lv_event_t *e) {
  (void)e;
  lv_indev_t *indev = lv_indev_active();
  if (!indev) return;
  nav_dir_t dir;
  switch (lv_indev_get_gesture_dir(indev)) {
    case LV_DIR_LEFT: dir = NAV_LEFT; break;
    case LV_DIR_RIGHT: dir = NAV_RIGHT; break;
    case LV_DIR_TOP: dir = NAV_UP; break;
    case LV_DIR_BOTTOM: dir = NAV_DOWN; break;
    default: return;
  }
  lv_indev_wait_release(indev);
  lv_async_call(deferred_move, (void *)(uintptr_t)dir);
}

static void on_key(lv_event_t *e) {
  nav_dir_t dir;
  switch (lv_event_get_key(e)) {
    case LV_KEY_LEFT: dir = NAV_LEFT; break;
    case LV_KEY_RIGHT: dir = NAV_RIGHT; break;
    case LV_KEY_UP: dir = NAV_UP; break;
    case LV_KEY_DOWN: dir = NAV_DOWN; break;
    default: return;
  }
  lv_async_call(deferred_move, (void *)(uintptr_t)dir);
}

static void on_strip_tap(lv_event_t *e) {
  (void)e;
  if (s.strip_tap) s.strip_tap();
}

// The strip's objects go when its screen does (a display deleted, or the shell built again);
// forget them so a late caller (the alert markers align to the status label) never touches freed memory.
static void on_strip_delete(lv_event_t *e) {
  (void)e;
  s.strip = s.tap = s.home_btn = s.home_icon = s.sos = s.name = s.dots = s.clock = s.src = s.can_dot = s.status = s.content = NULL;
}

static void on_home(lv_event_t *e) {
  (void)e;
  ui_shell_goto(s.home, 0);
}

void ui_shell_set_strip_tap(void (*fn)(void)) { s.strip_tap = fn; }

static void on_sos(lv_event_t *e) {
  (void)e;
  if (s.sos_cb) s.sos_cb();
}
void ui_shell_set_sos_cb(void (*cb)(void)) { s.sos_cb = cb; }
lv_obj_t *ui_shell_sos(void) { return s.sos; }
void ui_shell_set_sos(bool visible) {
  if (!s.sos || visible != lv_obj_has_flag(s.sos, LV_OBJ_FLAG_HIDDEN)) return;   // the app calls this every tick
  if (visible) {
    lv_obj_remove_flag(s.sos, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s.name, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(s.sos, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s.name, LV_OBJ_FLAG_HIDDEN);
  }
  place_clock();   // the square strip packs the dots and clock after the SOS button or the title, whichever shows
}
lv_obj_t *ui_shell_strip_tap(void) { return s.tap; }

void ui_shell_create(lv_display_t *disp, size_class_t sc, palette_t pal,
                     const ui_role_view_t *roles, uint8_t role_count, uint8_t start_role) {
  uint8_t pages[UI_MAX_ROLES];
  if (role_count > UI_MAX_ROLES) role_count = UI_MAX_ROLES;
  for (uint8_t i = 0; i < role_count; i++) pages[i] = roles[i].page_count;
  ui_nav_init(&s.nav, role_count, pages, start_role);
  s.roles = roles;
  s.home = start_role < role_count ? start_role : 0;
  s.sc = sc;
  s.pal = pal;

  s.screen = lv_display_get_screen_active(disp);
  lv_obj_clean(s.screen);
  lv_obj_remove_flag(s.screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(s.screen, LV_OPA_COVER, 0);

  int32_t strip_h = sc == SIZE_WIDE ? 40 : 36;
  int32_t w = lv_display_get_horizontal_resolution(disp);
  int32_t h = lv_display_get_vertical_resolution(disp);

  s.strip = lv_obj_create(s.screen);
  lv_obj_remove_style_all(s.strip);
  lv_obj_set_size(s.strip, w, strip_h);
  lv_obj_set_style_bg_opa(s.strip, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(s.strip, 12, 0);
  lv_obj_remove_flag(s.strip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s.strip, on_strip_delete, LV_EVENT_DELETE, NULL);

  s.name = lv_label_create(s.strip);
  lv_obj_set_style_text_font(s.name, &lv_font_montserrat_20, 0);
  lv_obj_align(s.name, LV_ALIGN_LEFT_MID, strip_h - 4, 0);   // right of the home button

  s.dots = lv_obj_create(s.strip);
  lv_obj_remove_style_all(s.dots);
  lv_obj_set_size(s.dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(s.dots, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(s.dots, 8, 0);
  lv_obj_align(s.dots, LV_ALIGN_CENTER, 0, 0);

  s.clock = lv_label_create(s.strip);
  lv_obj_set_style_text_font(s.clock, &lv_font_montserrat_20, 0);
  lv_obj_add_flag(s.clock, LV_OBJ_FLAG_HIDDEN);   // until a time is set

  s.src = lv_obj_create(s.strip);
  lv_obj_remove_style_all(s.src);
  lv_obj_set_size(s.src, SRC_DOT, SRC_DOT);
  lv_obj_set_style_radius(s.src, LV_RADIUS_CIRCLE, 0);
  lv_obj_remove_flag(s.src, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(s.src, LV_ALIGN_RIGHT_MID, 0, 0);

  s.can_dot = lv_obj_create(s.strip);
  lv_obj_remove_style_all(s.can_dot);
  lv_obj_set_size(s.can_dot, SRC_DOT, SRC_DOT);
  lv_obj_remove_flag(s.can_dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s.can_dot, LV_OBJ_FLAG_HIDDEN);   // until the bus is in use

  // The status text sits left of the dot. The alert markers align to its left edge.
  s.status = lv_label_create(s.strip);
  lv_obj_set_style_text_font(s.status, &lv_font_montserrat_14, 0);
  lv_label_set_text(s.status, "");
  s.over = false;
  lv_obj_align(s.status, LV_ALIGN_RIGHT_MID, -(SRC_DOT + 8), 0);
  s.sk = (ui_source_t){true, UI_SRC_OFFLINE};   // no source heard from yet
  s.can = (ui_source_t){false, UI_SRC_OFFLINE};

  // An invisible tap target over the strip's left part (role name side), created after the labels, which are not clickable.
  s.tap = lv_obj_create(s.strip);
  lv_obj_remove_style_all(s.tap);
  lv_obj_set_size(s.tap, (w - 24) * 2 / 5, strip_h);
  lv_obj_set_pos(s.tap, 0, 0);
  lv_obj_add_flag(s.tap, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s.tap, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s.tap, on_strip_tap, LV_EVENT_CLICKED, NULL);

  // Home: back to the start role's first page. After the strip tap so it wins where they overlap.
  s.home_btn = lv_obj_create(s.strip);
  lv_obj_remove_style_all(s.home_btn);
  lv_obj_set_size(s.home_btn, strip_h, strip_h);
  lv_obj_align(s.home_btn, LV_ALIGN_LEFT_MID, -12, 0);
  lv_obj_add_flag(s.home_btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s.home_btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s.home_btn, on_home, LV_EVENT_CLICKED, NULL);
  s.home_icon = lv_label_create(s.home_btn);
  lv_obj_set_style_text_font(s.home_icon, &lv_font_montserrat_20, 0);
  lv_label_set_text(s.home_icon, LV_SYMBOL_HOME);
  lv_obj_center(s.home_icon);

  // SOS: where the role name is, created after the strip tap so it wins there. Hidden until a MOB is declared.
  s.sos = lv_obj_create(s.strip);
  lv_obj_remove_style_all(s.sos);
  lv_obj_set_size(s.sos, 76, strip_h - 4);
  lv_obj_align(s.sos, LV_ALIGN_LEFT_MID, strip_h - 4, 0);
  lv_obj_set_style_bg_opa(s.sos, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s.sos, 6, 0);
  lv_obj_set_ext_click_area(s.sos, 8);   // 48 px of reach in the 36 px strip
  lv_obj_add_flag(s.sos, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(s.sos, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s.sos, on_sos, LV_EVENT_CLICKED, NULL);
  lv_obj_t *sos_label = lv_label_create(s.sos);
  lv_obj_set_style_text_font(sos_label, &lv_font_montserrat_20, 0);
  lv_label_set_text(sos_label, "SOS");
  lv_obj_center(sos_label);

  s.content = lv_obj_create(s.screen);
  lv_obj_remove_style_all(s.content);
  lv_obj_set_size(s.content, w, h - strip_h);
  lv_obj_set_pos(s.content, 0, strip_h);
  lv_obj_remove_flag(s.content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s.content, LV_OBJ_FLAG_CLICKABLE);
  // LVGL bubbles gestures to the parent by default. Stop them here so swipes
  // over a page (or any child on it) reach on_gesture.
  lv_obj_remove_flag(s.content, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(s.content, on_gesture, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(s.content, on_key, LV_EVENT_KEY, NULL);
  lv_group_t *g = lv_group_get_default();
  if (g) lv_group_add_obj(g, s.content);

  apply_palette();
  rebuild();
}

void ui_shell_set_palette(palette_t pal) {
  s.pal = pal;
  apply_palette();
  rebuild();
}

const ui_nav_t *ui_shell_nav(void) { return &s.nav; }

void ui_shell_refresh(void) {
  if (s.nav.role_count == 0 || !s.roles) return;
  const ui_role_view_t *r = &s.roles[s.nav.role];
  if (r->refresh) r->refresh(r->ctx);
}

static void show_status(void) {
  const char *text = s.over ? s.over_text : s.text_hidden ? "" : s.status_text;
  if (strcmp(lv_label_get_text(s.status), text) != 0) lv_label_set_text(s.status, text);
  lv_obj_set_style_text_color(s.status, s.over ? s.over_color : s.status_color, 0);
}

void ui_shell_set_sources(ui_source_t sk, ui_source_t can) {
  if (!s.src || (sk.shown == s.sk.shown && sk.state == s.sk.state && can.shown == s.can.shown && can.state == s.can.state))
    return;   // the app calls this every tick
  s.sk = sk;
  s.can = can;
  draw_source();
}
void ui_shell_set_source(ui_src_t st) { ui_shell_set_sources((ui_source_t){true, st}, s.can); }
uint32_t ui_shell_sources_gen(void) { return s.sources_gen; }
void ui_shell_set_source_text_hidden(bool hidden) {
  if (!s.status || hidden == s.text_hidden) return;
  s.text_hidden = hidden;
  show_status();
}
bool ui_shell_source_text_shown(void) { return s.status && !s.over && !s.text_hidden && s.status_text[0]; }

void ui_shell_set_clock(const char *hhmm) {   // the app calls this every tick: touch nothing that has not changed
  if (!s.clock) return;
  bool hidden = lv_obj_has_flag(s.clock, LV_OBJ_FLAG_HIDDEN);
  s.clock_has_text = hhmm != NULL;
  if (!hhmm || s.clock_yield) {   // no time, or the clock gave way: stay hidden, but remember the text for later
    if (hhmm && strcmp(lv_label_get_text(s.clock), hhmm) != 0) lv_label_set_text(s.clock, hhmm);
    if (!hidden) lv_obj_add_flag(s.clock, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (!hidden && strcmp(lv_label_get_text(s.clock), hhmm) == 0) return;
  lv_label_set_text(s.clock, hhmm);
  if (hidden) lv_obj_remove_flag(s.clock, LV_OBJ_FLAG_HIDDEN);
  place_clock();
}

void ui_shell_set_clock_yielded(bool yielded) {
  if (!s.clock || yielded == s.clock_yield) return;
  s.clock_yield = yielded;
  if (yielded) {
    lv_obj_add_flag(s.clock, LV_OBJ_FLAG_HIDDEN);
  } else if (s.clock_has_text) {
    lv_obj_remove_flag(s.clock, LV_OBJ_FLAG_HIDDEN);
    place_clock();
  }
}
bool ui_shell_clock_wanted(void) { return s.clock && s.clock_has_text; }

void ui_shell_set_status_override(const char *text, lv_color_t color) {
  if (!s.status) return;
  bool over = text != NULL;
  if (over == s.over && (!over || (strcmp(text, s.over_text) == 0 && lv_color_eq(color, s.over_color)))) return;
  s.over = over;
  if (over) {
    snprintf(s.over_text, sizeof s.over_text, "%s", text);
    s.over_color = color;
  }
  show_status();
}

void ui_shell_set_roles(uint8_t role_count) {
  if (role_count > UI_MAX_ROLES) role_count = UI_MAX_ROLES;
  uint8_t pages[UI_MAX_ROLES] = {0};
  for (uint8_t i = 0; i < role_count; i++) pages[i] = s.roles[i].page_count;
  if (s.home >= role_count) s.home = 0;
  uint8_t keep = s.nav.role < role_count ? s.nav.role : 0;
  ui_nav_init(&s.nav, role_count, pages, keep);
  rebuild();
}

void ui_shell_rebuild(void) {
  if (!s.content) return;
  uint8_t pages[UI_MAX_ROLES];   // a rebind can change a role's page count
  for (uint8_t i = 0; i < s.nav.role_count; i++) pages[i] = s.roles[i].page_count;
  ui_nav_set_pages(&s.nav, pages);
  rebuild();
}

void ui_shell_goto(uint8_t role, uint8_t page) {
  if (role >= s.nav.role_count) return;
  uint8_t n = s.nav.page_count[role];
  s.nav.role = role;
  s.nav.page = n && page >= n ? (uint8_t)(n - 1) : page;
  rebuild();
  settle_restart();
}

lv_obj_t *ui_shell_strip(void) { return s.strip; }
lv_obj_t *ui_shell_status(void) { return s.status; }
lv_obj_t *ui_shell_dots(void) { return s.dots; }
lv_obj_t *ui_shell_name(void) { return s.name; }
lv_obj_t *ui_shell_clock(void) { return s.clock; }
lv_obj_t *ui_shell_source_dot(void) { return s.src; }
lv_obj_t *ui_shell_can_dot(void) { return s.can_dot; }
lv_obj_t *ui_shell_home_button(void) { return s.home_btn; }
