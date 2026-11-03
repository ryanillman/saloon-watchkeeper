#include "ui_shell.h"

static struct {
  const ui_role_view_t *roles;
  ui_nav_t nav;
  size_class_t sc;
  palette_t pal;
  lv_obj_t *screen, *strip, *name, *dots, *status, *content;
} s;

static void apply_palette(void) {
  ui_theme_set_current(s.pal);
  const ui_colors_t *c = ui_theme_colors(s.pal);
  lv_obj_set_style_bg_color(s.screen, c->bg, 0);
  lv_obj_set_style_text_color(s.screen, c->fg, 0);
  lv_obj_set_style_bg_color(s.strip, c->strip_bg, 0);
  lv_obj_set_style_text_color(s.status, c->dim, 0);
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
  lv_obj_align(s.status, LV_ALIGN_RIGHT_MID, 0, 0);

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
