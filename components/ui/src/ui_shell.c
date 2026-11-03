#include "ui_shell.h"
#include <stdio.h>
#include <string.h>

static struct {
  const ui_role_view_t *roles;
  ui_nav_t nav;
  size_class_t sc;
  palette_t pal;
  void (*strip_tap)(void);
  lv_obj_t *screen, *strip, *tap, *name, *dots, *status, *content;
  char status_text[24], over_text[24];   // the app's status, and a temporary replacement
  lv_color_t status_color, over_color;
  bool over;
} s;

static void apply_palette(void) {
  ui_theme_set_current(s.pal);
  const ui_colors_t *c = ui_theme_colors(s.pal);
  lv_obj_set_style_bg_color(s.screen, c->bg, 0);
  lv_obj_set_style_text_color(s.screen, c->fg, 0);
  lv_obj_set_style_bg_color(s.strip, c->strip_bg, 0);
  lv_obj_set_style_bg_opa(s.content, LV_OPA_TRANSP, 0);
}

static void draw_dots(void) {
  const ui_colors_t *c = ui_theme_colors(s.pal);
  lv_obj_clean(s.dots);
  if (s.nav.role_count == 0) return;
  uint8_t pages = s.nav.page_count[s.nav.role];
  if (pages <= 1) return;
  int32_t d = s.sc == SIZE_WIDE ? 10 : 8;
  for (uint8_t i = 0; i < pages; i++) {
    lv_obj_t *dot = lv_obj_create(s.dots);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, d, d);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, i == s.nav.page ? c->fg : c->dim, 0);
  }
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
  draw_dots();
  if (s.nav.role_count == 0) {
    lv_label_set_text(s.name, "");
    return;
  }
  const ui_role_view_t *r = &s.roles[s.nav.role];
  lv_label_set_text(s.name, r->name);
  if (r->build_page) r->build_page(s.content, s.nav.page, s.sc, r->ctx);
  lv_obj_tree_walk(s.content, no_scroll, NULL);
}

void ui_shell_move(nav_dir_t dir) {
  if (ui_nav_move(&s.nav, dir)) rebuild();
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

void ui_shell_set_strip_tap(void (*fn)(void)) { s.strip_tap = fn; }
lv_obj_t *ui_shell_strip_tap(void) { return s.tap; }

void ui_shell_create(lv_display_t *disp, size_class_t sc, palette_t pal,
                     const ui_role_view_t *roles, uint8_t role_count, uint8_t start_role) {
  uint8_t pages[UI_MAX_ROLES];
  if (role_count > UI_MAX_ROLES) role_count = UI_MAX_ROLES;
  for (uint8_t i = 0; i < role_count; i++) pages[i] = roles[i].page_count;
  ui_nav_init(&s.nav, role_count, pages, start_role);
  s.roles = roles;
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

  s.name = lv_label_create(s.strip);
  lv_obj_set_style_text_font(s.name, &lv_font_montserrat_20, 0);
  lv_obj_align(s.name, LV_ALIGN_LEFT_MID, 0, 0);

  s.dots = lv_obj_create(s.strip);
  lv_obj_remove_style_all(s.dots);
  lv_obj_set_size(s.dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(s.dots, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(s.dots, 8, 0);
  lv_obj_align(s.dots, LV_ALIGN_CENTER, 0, 0);

  s.status = lv_label_create(s.strip);
  lv_obj_set_style_text_font(s.status, &lv_font_montserrat_14, 0);
  lv_label_set_text(s.status, "NO SOURCE");
  snprintf(s.status_text, sizeof s.status_text, "NO SOURCE");
  s.status_color = ui_theme_colors(pal)->dim;
  s.over = false;
  lv_obj_set_style_text_color(s.status, s.status_color, 0);
  lv_obj_align(s.status, LV_ALIGN_RIGHT_MID, 0, 0);

  // An invisible tap target over the strip's left part (role name side), created after the labels, which are not clickable.
  s.tap = lv_obj_create(s.strip);
  lv_obj_remove_style_all(s.tap);
  lv_obj_set_size(s.tap, (w - 24) * 2 / 5, strip_h);
  lv_obj_set_pos(s.tap, 0, 0);
  lv_obj_add_flag(s.tap, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s.tap, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s.tap, on_strip_tap, LV_EVENT_CLICKED, NULL);

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
  const char *text = s.over ? s.over_text : s.status_text;
  if (strcmp(lv_label_get_text(s.status), text) != 0) lv_label_set_text(s.status, text);
  lv_obj_set_style_text_color(s.status, s.over ? s.over_color : s.status_color, 0);
}

void ui_shell_set_status(const char *text, lv_color_t color) {
  if (!s.status) return;
  snprintf(s.status_text, sizeof s.status_text, "%s", text);
  s.status_color = color;
  show_status();
}

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

void ui_shell_rebuild(void) {
  if (!s.content) return;
  uint8_t pages[UI_MAX_ROLES];   // a rebind can change a role's page count
  for (uint8_t i = 0; i < s.nav.role_count; i++) pages[i] = s.roles[i].page_count;
  ui_nav_set_pages(&s.nav, pages);
  rebuild();
}

void ui_shell_goto_role(uint8_t role) {
  if (role >= s.nav.role_count) return;
  s.nav.role = role;
  s.nav.page = 0;
  rebuild();
}

lv_obj_t *ui_shell_strip(void) { return s.strip; }
lv_obj_t *ui_shell_status(void) { return s.status; }
lv_obj_t *ui_shell_dots(void) { return s.dots; }
