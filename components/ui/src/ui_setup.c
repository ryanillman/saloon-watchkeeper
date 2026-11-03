// Setup screens (spec: "Setup screens"). One root object over the page area, below the top
// strip and below the alarm banner; a header (BACK, title, SAVE) and a scrolling body that is
// rebuilt for each screen.
#include "ui_setup.h"
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "ui_alerts.h"
#include "ui_shell.h"

#define MAX_PARTS 96
#define MAX_SEGS 8
#define ROW_H 56

typedef struct {
  lv_obj_t *btn[4];
  const char *val[4];
  uint8_t n;
  char *field;   // in the working copy
  size_t size;
} seg_t;

PLATFORM_EXT_BSS static struct {
  lv_obj_t *root, *header, *title, *back, *save, *body, *dialog;
  const ui_setup_cb_t *cb;
  size_class_t sc;
  ui_setup_screen_t screen;
  cfg_t work, base;   // the edited copy and the config it started from (or was last saved as)
  struct { char name[32]; lv_obj_t *obj; } parts[MAX_PARTS];
  uint8_t n_parts, n_parts_frame;
  seg_t segs[MAX_SEGS];
  uint8_t n_segs;
} u;

static bool open_now(void) { return u.root && lv_obj_is_valid(u.root); }

static void part(const char *name, lv_obj_t *o) {
  if (u.n_parts >= MAX_PARTS) return;
  snprintf(u.parts[u.n_parts].name, sizeof u.parts[0].name, "%s", name);
  u.parts[u.n_parts++].obj = o;
}

lv_obj_t *ui_setup_part(const char *name) {
  if (!open_now()) return NULL;
  for (uint8_t i = 0; i < u.n_parts; i++)
    if (!strcmp(u.parts[i].name, name)) return u.parts[i].obj;
  return NULL;
}

bool ui_setup_is_open(void) { return open_now(); }
ui_setup_screen_t ui_setup_screen(void) { return u.screen; }
const cfg_t *ui_setup_working(void) { return &u.work; }
bool ui_setup_dirty(void) { return memcmp(&u.work, &u.base, sizeof u.work) != 0; }

// ---- building blocks ----

static const lv_font_t *font(void) { return &lv_font_montserrat_20; }

static void style_button(lv_obj_t *b, bool selected) {
  const ui_colors_t *c = ui_theme();
  lv_obj_set_style_bg_color(b, selected ? c->fg : c->strip_bg, 0);
  lv_obj_set_style_border_color(b, selected ? c->fg : c->dim, 0);
  lv_obj_set_style_text_color(b, selected ? c->bg : c->fg, 0);
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t fn, void *arg) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(b, ROW_H - 8);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_pad_hor(b, 16, 0);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font(), 0);
  lv_obj_center(l);
  if (fn) lv_obj_add_event_cb(b, fn, LV_EVENT_CLICKED, arg);
  style_button(b, false);
  return b;
}

static lv_obj_t *row(lv_obj_t *parent) {
  lv_obj_t *r = lv_obj_create(parent);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, 8, 0);
  lv_obj_set_style_pad_row(r, 8, 0);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  return r;
}

static lv_obj_t *text(lv_obj_t *parent, const char *t, lv_color_t col) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_font(l, font(), 0);
  lv_obj_set_style_text_color(l, col, 0);
  return l;
}

// ---- segmented choices (units, palette) ----

static void seg_style(seg_t *g) {
  for (uint8_t i = 0; i < g->n; i++) style_button(g->btn[i], !strcmp(g->field, g->val[i]));
}

static void on_seg(lv_event_t *e) {
  uintptr_t v = (uintptr_t)lv_event_get_user_data(e);
  seg_t *g = &u.segs[v >> 4];
  snprintf(g->field, g->size, "%s", g->val[v & 15]);
  seg_style(g);
}

static void segmented(lv_obj_t *parent, const char *label, const char *kind, char *field, size_t size,
                      const char *const *vals, const char *const *labels, uint8_t n) {
  if (u.n_segs >= MAX_SEGS) return;
  uint8_t gi = u.n_segs++;
  seg_t *g = &u.segs[gi];
  memset(g, 0, sizeof *g);
  g->field = field;
  g->size = size;
  g->n = n;
  lv_obj_t *r = row(parent);
  lv_obj_t *l = text(r, label, ui_theme()->dim);
  lv_obj_set_width(l, u.sc == SIZE_WIDE ? 200 : lv_pct(100));
  for (uint8_t i = 0; i < n; i++) {
    g->val[i] = vals[i];
    g->btn[i] = button(r, labels[i], on_seg, (void *)(uintptr_t)((gi << 4) | i));
    char name[32];
    snprintf(name, sizeof name, "%s_%s", kind, vals[i]);
    part(name, g->btn[i]);
  }
  seg_style(g);
}

// ---- screens ----

static void show_screen(ui_setup_screen_t s);

static void on_menu(lv_event_t *e) { show_screen((ui_setup_screen_t)(uintptr_t)lv_event_get_user_data(e)); }
static void on_close(lv_event_t *e) {
  (void)e;
  ui_setup_close();
}

static void build_menu(void) {
  static const struct { ui_setup_screen_t s; const char *label, *name; } items[] = {
    {SETUP_WIFI, "Wi-Fi", "menu_wifi"},       {SETUP_SIGNALK, "Signal K", "menu_signalk"},
    {SETUP_ROLES, "Roles", "menu_roles"},     {SETUP_DISCOVERY, "Discovery", "menu_discovery"},
    {SETUP_DISPLAY, "Display", "menu_display"}, {SETUP_ABOUT, "About", "menu_about"},
  };
  for (size_t i = 0; i < sizeof items / sizeof items[0]; i++) {
    lv_obj_t *b = button(u.body, items[i].label, on_menu, (void *)(uintptr_t)items[i].s);
    lv_obj_set_width(b, lv_pct(100));
    part(items[i].name, b);
  }
  lv_obj_t *c = button(u.body, "CLOSE", on_close, NULL);
  lv_obj_set_width(c, lv_pct(100));
  part("close", c);
}

static void on_brightness(lv_event_t *e) { u.work.brightness = (uint8_t)lv_slider_get_value(lv_event_get_target(e)); }
static void on_buzzer(lv_event_t *e) { u.work.buzzer = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED); }

static void build_display(void) {
  const ui_colors_t *c = ui_theme();
  cfg_units_t *un = &u.work.units;
  segmented(u.body, "Temperature", "unit_temperature", un->temperature, sizeof un->temperature,
            (const char *const[]){"C", "F"}, (const char *const[]){"°C", "°F"}, 2);
  segmented(u.body, "Pressure", "unit_pressure", un->pressure, sizeof un->pressure,
            (const char *const[]){"bar", "psi", "kPa"}, (const char *const[]){"bar", "psi", "kPa"}, 3);
  segmented(u.body, "Volume", "unit_volume", un->volume, sizeof un->volume, (const char *const[]){"L", "gal"},
            (const char *const[]){"L", "gal"}, 2);
  segmented(u.body, "Speed", "unit_speed", un->speed, sizeof un->speed, (const char *const[]){"kn", "m/s", "km/h"},
            (const char *const[]){"kn", "m/s", "km/h"}, 3);
  segmented(u.body, "Distance", "unit_distance", un->distance, sizeof un->distance,
            (const char *const[]){"nm", "m", "ft"}, (const char *const[]){"nm", "m", "ft"}, 3);
  segmented(u.body, "Palette", "palette", u.work.palette, sizeof u.work.palette,
            (const char *const[]){"day", "dusk", "night", "auto"}, (const char *const[]){"DAY", "DUSK", "NIGHT", "AUTO"}, 4);

  lv_obj_t *r = row(u.body);
  lv_obj_t *l = text(r, "Brightness", c->dim);
  lv_obj_set_width(l, u.sc == SIZE_WIDE ? 200 : lv_pct(100));
  if (u.cb->dims) {
    lv_obj_t *sl = lv_slider_create(r);
    lv_slider_set_range(sl, 10, 100);
    lv_slider_set_value(sl, u.work.brightness < 10 ? 10 : u.work.brightness, LV_ANIM_OFF);
    lv_obj_set_size(sl, u.sc == SIZE_WIDE ? 400 : lv_pct(90), 16);
    lv_obj_set_ext_click_area(sl, 16);
    lv_obj_set_style_bg_color(sl, c->dim, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, c->accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, c->fg, LV_PART_KNOB);
    lv_obj_add_event_cb(sl, on_brightness, LV_EVENT_VALUE_CHANGED, NULL);
    part("brightness", sl);
  } else {
    text(r, "ON (this screen does not dim)", c->fg);
  }

  if (u.cb->has_buzzer) {
    lv_obj_t *br = row(u.body);
    lv_obj_t *bl = text(br, "Buzzer", c->dim);
    lv_obj_set_width(bl, u.sc == SIZE_WIDE ? 200 : lv_pct(60));
    lv_obj_t *sw = lv_switch_create(br);
    lv_obj_set_size(sw, 72, 40);
    if (u.work.buzzer) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, c->dim, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, c->accent, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, c->fg, LV_PART_KNOB);
    lv_obj_add_event_cb(sw, on_buzzer, LV_EVENT_VALUE_CHANGED, NULL);
    part("buzzer", sw);
  }
}

static void build_about(void) {
  char t[512] = "";
  if (u.cb->about) u.cb->about(t, sizeof t);
  lv_obj_t *l = text(u.body, t, ui_theme()->fg);
  lv_obj_set_width(l, lv_pct(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  part("about_text", l);
}

static void build_later(void) {
  lv_obj_t *l = text(u.body, "Not available in this firmware yet.", ui_theme()->dim);
  lv_obj_set_width(l, lv_pct(100));
}

static const char *title_of(ui_setup_screen_t s) {
  static const char *t[] = {"SETUP", "WI-FI", "SIGNAL K", "ROLES", "DISCOVERY", "DISPLAY", "ABOUT"};
  return t[s];
}

static bool edits(ui_setup_screen_t s) { return s != SETUP_MENU && s != SETUP_ABOUT; }

static void style_frame(void) {
  const ui_colors_t *c = ui_theme();
  lv_obj_set_style_bg_color(u.root, c->bg, 0);
  lv_obj_set_style_text_color(u.root, c->fg, 0);
  lv_obj_set_style_text_color(u.title, c->accent, 0);
  style_button(u.back, false);
  style_button(u.save, false);
}

static void show_screen(ui_setup_screen_t s) {
  if (!open_now()) return;
  if (u.dialog) {
    lv_obj_delete(u.dialog);
    u.dialog = NULL;
  }
  u.screen = s;
  u.n_parts = u.n_parts_frame;   // the header's parts stay
  u.n_segs = 0;
  lv_obj_clean(u.body);
  lv_label_set_text(u.title, title_of(s));
  if (edits(s)) lv_obj_remove_flag(u.save, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(u.save, LV_OBJ_FLAG_HIDDEN);
  style_frame();
  if (edits(s) && !u.cb->has_sd) {
    lv_obj_t *n = text(u.body, "No SD card. Changes last until restart.", ui_theme()->warn);
    part("nosd", n);
  }
  switch (s) {
    case SETUP_MENU: build_menu(); break;
    case SETUP_DISPLAY: build_display(); break;
    case SETUP_ABOUT: build_about(); break;
    default: build_later(); break;
  }
  lv_obj_scroll_to_y(u.body, 0, LV_ANIM_OFF);
}

void ui_setup_show(ui_setup_screen_t s) { show_screen(s); }

// ---- BACK, SAVE, and the discard question ----

static void on_discard(lv_event_t *e) {
  bool yes = (bool)(uintptr_t)lv_event_get_user_data(e);
  if (yes) {
    u.work = u.base;
    show_screen(SETUP_MENU);
  } else if (u.dialog) {
    lv_obj_delete_async(u.dialog);   // inside one of its own buttons' events
    u.dialog = NULL;
    u.n_parts -= 3;                  // its text and two buttons were the last parts added
  }
}

static void ask_discard(void) {
  const ui_colors_t *c = ui_theme();
  u.dialog = lv_obj_create(u.root);
  lv_obj_remove_style_all(u.dialog);
  lv_obj_set_size(u.dialog, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_color(u.dialog, c->bg, 0);
  lv_obj_set_style_bg_opa(u.dialog, LV_OPA_90, 0);
  lv_obj_add_flag(u.dialog, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_flex_flow(u.dialog, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(u.dialog, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(u.dialog, 16, 0);
  part("discard_text", text(u.dialog, "Discard changes?", c->fg));
  part("discard_yes", button(u.dialog, "DISCARD", on_discard, (void *)(uintptr_t)1));
  part("discard_no", button(u.dialog, "KEEP EDITING", on_discard, (void *)(uintptr_t)0));
}

static void on_back(lv_event_t *e) {
  (void)e;
  if (u.screen == SETUP_MENU) ui_setup_close();
  else if (ui_setup_dirty()) ask_discard();
  else show_screen(SETUP_MENU);
}

static void on_save(lv_event_t *e) {
  (void)e;
  if (u.cb->save) u.cb->save(&u.work);   // the app applies it; the palette may change under us
  u.base = u.work;
  show_screen(SETUP_MENU);
}

// ---- open, close, tick ----

void ui_setup_open(lv_obj_t *screen, const cfg_t *current, size_class_t sc, const ui_setup_cb_t *cb) {
  if (open_now()) return;
  memset(&u, 0, sizeof u);
  u.cb = cb;
  u.sc = sc;
  u.work = u.base = *current;
  lv_obj_update_layout(screen);
  int32_t w = lv_obj_get_width(screen), h = lv_obj_get_height(screen);
  lv_obj_t *strip = ui_shell_strip();
  int32_t top = strip ? lv_obj_get_height(strip) : 0;

  u.root = lv_obj_create(screen);
  lv_obj_remove_style_all(u.root);
  lv_obj_set_size(u.root, w, h - top);
  lv_obj_set_pos(u.root, 0, top);
  lv_obj_set_style_bg_opa(u.root, LV_OPA_COVER, 0);
  lv_obj_add_flag(u.root, LV_OBJ_FLAG_CLICKABLE);   // touches stop here: no swipes reach the pages
  lv_obj_remove_flag(u.root, LV_OBJ_FLAG_SCROLLABLE);
  part("root", u.root);

  u.header = lv_obj_create(u.root);
  lv_obj_remove_style_all(u.header);
  lv_obj_set_size(u.header, w, ROW_H);
  lv_obj_set_style_pad_hor(u.header, 8, 0);
  lv_obj_remove_flag(u.header, LV_OBJ_FLAG_SCROLLABLE);
  u.back = button(u.header, "BACK", on_back, NULL);
  lv_obj_align(u.back, LV_ALIGN_LEFT_MID, 0, 0);
  u.title = lv_label_create(u.header);
  lv_obj_set_style_text_font(u.title, font(), 0);
  lv_obj_align(u.title, LV_ALIGN_CENTER, 0, 0);
  u.save = button(u.header, "SAVE", on_save, NULL);
  lv_obj_align(u.save, LV_ALIGN_RIGHT_MID, 0, 0);
  part("back", u.back);
  part("save", u.save);
  u.n_parts_frame = u.n_parts;

  u.body = lv_obj_create(u.root);
  lv_obj_remove_style_all(u.body);
  lv_obj_set_size(u.body, w, h - top - ROW_H);
  lv_obj_set_pos(u.body, 0, ROW_H);
  lv_obj_set_style_pad_all(u.body, 12, 0);
  lv_obj_set_style_pad_row(u.body, 10, 0);
  lv_obj_set_flex_flow(u.body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(u.body, LV_DIR_VER);

  lv_display_trigger_activity(lv_obj_get_display(screen));
  show_screen(SETUP_MENU);
  ui_alerts_to_front();   // an alarm stays visible over setup
}

void ui_setup_close(void) {
  if (!open_now()) return;
  lv_obj_delete_async(u.root);   // may be called from an event on one of its own children
  u.root = NULL;
  u.dialog = NULL;
}

void ui_setup_tick(void) {
  if (!open_now()) return;
  if (lv_display_get_inactive_time(lv_obj_get_display(u.root)) >= SETUP_IDLE_MS) ui_setup_close();
}
