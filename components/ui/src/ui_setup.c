// Setup screens (spec: "Setup screens"). One root object over the page area, below the top
// strip and below the alarm banner; a header (BACK, title, SAVE) and a scrolling body that is
// rebuilt for each screen.
#include "ui_setup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "ui_alerts.h"
#include "ui_shell.h"

#define MAX_PARTS 160
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
  ui_disc_role_t disc[CFG_MAX_ROLES];
  uint8_t n_disc;
  struct { uint8_t role; char id[24]; char label[12]; bool keep; lv_obj_t *btn, *text; } rows[CFG_MAX_ROLES * 16];
  lv_obj_t *kb_box;
  void (*kb_done)(const char *text, uintptr_t arg);
  uintptr_t kb_arg;
  ui_ap_t aps[16];
  int n_aps;
  bool scanning;
  lv_obj_t *live_state, *live_access;   // refreshed by ui_setup_tick
  void (*live_fn)(char *, size_t);
  uint16_t n_rows;
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
static void kb_close(void);

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

// Rebuilding from inside a click on a body child would delete the clicked object mid-event.
static void rebuild_cb(void *arg) {
  (void)arg;
  if (open_now()) show_screen(u.screen);
}
static void rebuild_later(void) { lv_async_call(rebuild_cb, NULL); }

static void upper(const char *in, char *out, size_t n) {
  size_t i = 0;
  for (; in[i] && i + 1 < n; i++) out[i] = (char)(in[i] >= 'a' && in[i] <= 'z' ? in[i] - 32 : in[i]);
  out[i] = 0;
}

// ---- Roles ----

enum { ROLE_UP, ROLE_DOWN, ROLE_REMOVE };

static void on_role(lv_event_t *e) {
  uintptr_t v = (uintptr_t)lv_event_get_user_data(e);
  uint8_t i = (uint8_t)(v >> 4), op = (uint8_t)(v & 15), n = u.work.role_count;
  cfg_role_t *r = u.work.roles;
  if (op == ROLE_UP && i > 0) {
    cfg_role_t t = r[i - 1];
    r[i - 1] = r[i];
    r[i] = t;
  } else if (op == ROLE_DOWN && i + 1 < n) {
    cfg_role_t t = r[i + 1];
    r[i + 1] = r[i];
    r[i] = t;
  } else if (op == ROLE_REMOVE && n > 1) {   // a panel always shows at least one role
    memmove(&r[i], &r[i + 1], (size_t)(n - i - 1) * sizeof r[0]);
    memset(&r[n - 1], 0, sizeof r[0]);
    u.work.role_count--;
  }
  rebuild_later();
}

static void on_add(lv_event_t *e) {
  const char *name = lv_event_get_user_data(e);
  if (u.work.role_count >= CFG_MAX_ROLES) return;
  cfg_role_t *r = &u.work.roles[u.work.role_count++];
  memset(r, 0, sizeof *r);   // src_entry 0: new
  snprintf(r->role, sizeof r->role, "%s", name);
  rebuild_later();
}

static bool has_role(const char *name) {
  for (uint8_t i = 0; i < u.work.role_count; i++)
    if (!strcmp(u.work.roles[i].role, name)) return true;
  return false;
}

static void build_roles(void) {
  const ui_colors_t *c = ui_theme();
  text(u.body, "Swipe order", c->dim);
  for (uint8_t i = 0; i < u.work.role_count; i++) {
    const char *name = u.work.roles[i].role;
    char title[16], pn[32];
    upper(name, title, sizeof title);
    lv_obj_t *r = row(u.body);
    lv_obj_t *l = text(r, title, c->fg);
    lv_obj_set_width(l, u.sc == SIZE_WIDE ? 200 : 140);
    static const char *ops[] = {LV_SYMBOL_UP, LV_SYMBOL_DOWN, LV_SYMBOL_CLOSE}, *names[] = {"up", "down", "remove"};
    for (uint8_t op = 0; op < 3; op++) {
      lv_obj_t *b = button(r, ops[op], on_role, (void *)(uintptr_t)((i << 4) | op));
      snprintf(pn, sizeof pn, "%s_%s", names[op], name);
      part(pn, b);
    }
  }
  if (u.cb->roles_available) {
    lv_obj_t *r = NULL;
    for (const char *const *n = u.cb->roles_available; *n; n++) {
      if (has_role(*n) || u.work.role_count >= CFG_MAX_ROLES) continue;
      if (!r) {
        text(u.body, "Add", c->dim);
        r = row(u.body);
      }
      char title[16], pn[32];
      upper(*n, title, sizeof title);
      lv_obj_t *b = button(r, title, on_add, (void *)*n);
      snprintf(pn, sizeof pn, "add_%s", *n);
      part(pn, b);
    }
  }
  static const char *vals[CFG_MAX_ROLES], *labels[CFG_MAX_ROLES];
  static char titles[CFG_MAX_ROLES][16];
  uint8_t n = u.work.role_count > 4 ? 4 : u.work.role_count;   // segmented rows hold 4
  for (uint8_t i = 0; i < n; i++) {
    vals[i] = u.work.roles[i].role;
    upper(vals[i], titles[i], sizeof titles[i]);
    labels[i] = titles[i];
  }
  segmented(u.body, "Start on", "start", u.work.start_role, sizeof u.work.start_role, vals, labels, n);
}

// ---- Discovery ----

static int find_role(const char *name) {
  for (uint8_t i = 0; i < u.work.role_count; i++)
    if (!strcmp(u.work.roles[i].role, name)) return i;
  return -1;
}

static bool in_list(const char ids[][24], uint8_t n, const char *id) {
  for (uint8_t i = 0; i < n; i++)
    if (!strcmp(ids[i], id)) return true;
  return false;
}

static void on_keep(lv_event_t *e) {
  uint16_t k = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
  u.rows[k].keep = !u.rows[k].keep;
  lv_obj_t *l = lv_obj_get_child(u.rows[k].btn, 0);
  lv_label_set_text(l, u.rows[k].keep ? "KEEP" : "SKIP");
  style_button(u.rows[k].btn, u.rows[k].keep);
}

static void on_confirm(lv_event_t *e) {
  uint8_t ri = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  cfg_role_t *r = &u.work.roles[ri];
  r->inst_count = 0;
  r->exclude_count = 0;
  for (uint16_t k = 0; k < u.n_rows; k++) {
    if (u.rows[k].role != ri) continue;
    if (u.rows[k].keep && r->inst_count < 8) {
      cfg_instance_t *in = &r->inst[r->inst_count++];
      memset(in, 0, sizeof *in);
      snprintf(in->id, sizeof in->id, "%s", u.rows[k].id);
      snprintf(in->label, sizeof in->label, "%s", u.rows[k].label);   // starts as the saved label
    } else if (!u.rows[k].keep && r->exclude_count < 8) {
      snprintf(r->exclude[r->exclude_count++], sizeof r->exclude[0], "%s", u.rows[k].id);
    }
  }
  rebuild_later();
}

static void on_auto(lv_event_t *e) {
  cfg_role_t *r = &u.work.roles[(uint8_t)(uintptr_t)lv_event_get_user_data(e)];
  r->inst_count = 0;
  r->exclude_count = 0;
  rebuild_later();
}

static void on_run(lv_event_t *e) {
  (void)e;
  if (u.cb->run_discovery) u.cb->run_discovery();
}

static void on_label(lv_event_t *e);

static void build_discovery(void) {
  const ui_colors_t *c = ui_theme();
  lv_obj_t *run = button(u.body, "RUN DISCOVERY", on_run, NULL);
  part("run", run);
  u.n_disc = u.cb->discovery ? u.cb->discovery(u.disc, CFG_MAX_ROLES) : 0;
  u.n_rows = 0;
  for (uint8_t d = 0; d < u.n_disc; d++) {
    int ri = find_role(u.disc[d].role);
    if (ri < 0) continue;   // removed on the Roles screen, not saved yet
    cfg_role_t *r = &u.work.roles[ri];
    char title[16], pn[48];
    upper(r->role, title, sizeof title);
    lv_obj_t *head = row(u.body);
    lv_obj_t *t = text(head, title, c->accent);
    lv_obj_set_width(t, u.sc == SIZE_WIDE ? 200 : lv_pct(100));
    text(head, r->inst_count ? "pinned" : "automatic", c->dim);
    // Every id found, then pinned ones that were not found (MISSING).
    char ids[32][24];
    uint8_t n = 0;
    for (uint8_t i = 0; i < u.disc[d].count && n < 32; i++) snprintf(ids[n++], 24, "%s", u.disc[d].ids[i]);
    for (uint8_t i = 0; i < r->inst_count && n < 32; i++)
      if (!in_list(ids, n, r->inst[i].id)) snprintf(ids[n++], 24, "%s", r->inst[i].id);
    if (!n) text(u.body, "nothing found", c->dim);
    for (uint8_t i = 0; i < n && u.n_rows < sizeof u.rows / sizeof u.rows[0]; i++) {
      bool pinned = false, excluded = false;
      const char *label = "";
      for (uint8_t j = 0; j < r->inst_count; j++)
        if (!strcmp(r->inst[j].id, ids[i])) {
          pinned = true;
          label = r->inst[j].label;
        }
      for (uint8_t j = 0; j < r->exclude_count; j++) excluded |= !strcmp(r->exclude[j], ids[i]);
      bool found = in_list(u.disc[d].ids, u.disc[d].count, ids[i]);
      uint16_t k = u.n_rows++;
      u.rows[k].role = (uint8_t)ri;
      snprintf(u.rows[k].id, sizeof u.rows[k].id, "%s", ids[i]);
      u.rows[k].keep = r->inst_count ? pinned : !excluded;
      snprintf(u.rows[k].label, sizeof u.rows[k].label, "%s", label);
      lv_obj_t *rr = row(u.body);
      u.rows[k].btn = button(rr, u.rows[k].keep ? "KEEP" : "SKIP", on_keep, (void *)(uintptr_t)k);
      style_button(u.rows[k].btn, u.rows[k].keep);
      snprintf(pn, sizeof pn, "keep_%s_%s", r->role, ids[i]);
      part(pn, u.rows[k].btn);
      char line[48];
      snprintf(line, sizeof line, "%s%s%s", ids[i], label[0] ? "  " : "", label);
      u.rows[k].text = text(rr, line, c->fg);
      lv_obj_t *lb = button(rr, "LABEL", on_label, (void *)(uintptr_t)k);
      snprintf(pn, sizeof pn, "label_%s_%s", r->role, ids[i]);
      part(pn, lb);
      if (pinned && !found) {
        lv_obj_t *m = text(rr, "MISSING", c->warn);
        snprintf(pn, sizeof pn, "missing_%s_%s", r->role, ids[i]);
        part(pn, m);
      }
    }
    lv_obj_t *act = row(u.body);
    lv_obj_t *cb = button(act, "CONFIRM", on_confirm, (void *)(uintptr_t)ri);
    snprintf(pn, sizeof pn, "confirm_%s", r->role);
    part(pn, cb);
    lv_obj_t *ab = button(act, "AUTO", on_auto, (void *)(uintptr_t)ri);
    snprintf(pn, sizeof pn, "auto_%s", r->role);
    part(pn, ab);
  }
}

// ---- keyboard ----

static void kb_close(void) {
  if (!u.kb_box) return;
  lv_obj_delete_async(u.kb_box);   // inside one of the keyboard's own events
  u.kb_box = NULL;
}

static void on_kb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL) return;
  char t[128] = "";
  if (code == LV_EVENT_READY) snprintf(t, sizeof t, "%s", lv_textarea_get_text(ui_setup_part("kb_text")));
  void (*done)(const char *, uintptr_t) = u.kb_done;
  uintptr_t arg = u.kb_arg;
  kb_close();
  if (code == LV_EVENT_READY && done) done(t, arg);
}

// The field being edited sits at the top; the keyboard takes the bottom of setup's area,
// so the text is never under the keys.
static void kb_open(const char *prompt, const char *initial, bool password, bool numeric, uint32_t max_len,
                    void (*done)(const char *, uintptr_t), uintptr_t arg) {
  const ui_colors_t *c = ui_theme();
  kb_close();
  u.kb_done = done;
  u.kb_arg = arg;
  int32_t w = lv_obj_get_width(u.root), h = lv_obj_get_height(u.root);
  u.kb_box = lv_obj_create(u.root);
  lv_obj_remove_style_all(u.kb_box);
  lv_obj_set_size(u.kb_box, w, h);
  lv_obj_set_style_bg_color(u.kb_box, c->bg, 0);
  lv_obj_set_style_bg_opa(u.kb_box, LV_OPA_COVER, 0);
  lv_obj_add_flag(u.kb_box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(u.kb_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *l = text(u.kb_box, prompt, c->dim);
  lv_obj_set_pos(l, 12, 8);
  lv_obj_t *ta = lv_textarea_create(u.kb_box);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_password_mode(ta, password);
  lv_textarea_set_max_length(ta, max_len);
  lv_textarea_set_text(ta, initial ? initial : "");
  lv_obj_set_size(ta, w - 24, 52);
  lv_obj_set_pos(ta, 12, 40);
  lv_obj_set_style_text_font(ta, font(), 0);
  lv_obj_set_style_bg_color(ta, c->strip_bg, 0);
  lv_obj_set_style_text_color(ta, c->fg, 0);
  lv_obj_set_style_border_color(ta, c->accent, 0);
  part("kb_text", ta);
  lv_obj_t *kb = lv_keyboard_create(u.kb_box);
  lv_keyboard_set_textarea(kb, ta);
  if (numeric) lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
  int32_t kh = h - 104;   // everything below the field
  if (kh > 320) kh = 320;
  lv_obj_set_size(kb, w, kh);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(kb, c->bg, 0);
  lv_obj_set_style_bg_color(kb, c->strip_bg, LV_PART_ITEMS);
  lv_obj_set_style_text_color(kb, c->fg, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, c->dim, LV_PART_ITEMS | LV_STATE_CHECKED);   // the special keys: never white at night
  lv_obj_set_style_text_color(kb, c->fg, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_add_event_cb(kb, on_kb, LV_EVENT_ALL, NULL);
  part("kb", kb);
}

// ---- live state lines (Wi-Fi, Signal K, access request) ----

static lv_obj_t *live_line(const char *name, void (*fn)(char *, size_t)) {
  char t[128] = "";
  if (fn) fn(t, sizeof t);
  lv_obj_t *l = text(u.body, t, ui_theme()->fg);
  lv_obj_set_width(l, lv_pct(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  part(name, l);
  return l;
}

static void refresh_live(lv_obj_t *l, void (*fn)(char *, size_t)) {
  if (!l || !fn || !lv_obj_is_valid(l)) return;
  char t[128] = "";
  fn(t, sizeof t);
  if (strcmp(t, lv_label_get_text(l))) lv_label_set_text(l, t);   // redraw only on a change
}

// ---- Wi-Fi ----

static void wifi_password_done(const char *t, uintptr_t arg) {
  (void)arg;
  snprintf(u.work.wifi.password, sizeof u.work.wifi.password, "%s", t);
  rebuild_later();
}

static void choose_network(const char *ssid, bool secure) {
  snprintf(u.work.wifi.ssid, sizeof u.work.wifi.ssid, "%s", ssid);
  if (!secure) {
    u.work.wifi.password[0] = 0;
    rebuild_later();
    return;
  }
  char prompt[64];
  snprintf(prompt, sizeof prompt, "Password for %s", ssid);
  kb_open(prompt, "", true, false, sizeof u.work.wifi.password - 1, wifi_password_done, 0);
}

static void on_ap(lv_event_t *e) {
  int i = (int)(uintptr_t)lv_event_get_user_data(e);
  choose_network(u.aps[i].ssid, u.aps[i].secure);
}

static void other_ssid_done(const char *t, uintptr_t arg) {
  (void)arg;
  if (t[0]) choose_network(t, true);
}

static void on_other(lv_event_t *e) {
  (void)e;
  kb_open("Network name", "", false, false, sizeof u.work.wifi.ssid - 1, other_ssid_done, 0);
}

static void on_scan(lv_event_t *e) {
  (void)e;
  u.scanning = true;
  if (u.cb->wifi_scan) u.cb->wifi_scan();
  rebuild_later();
}

void ui_setup_wifi_results(const ui_ap_t *aps, int n) {
  if (n > 16) n = 16;
  for (int i = 0; i < n; i++) {   // strongest first
    int j = i;
    while (j > 0 && u.aps[j - 1].rssi < aps[i].rssi) {
      u.aps[j] = u.aps[j - 1];
      j--;
    }
    u.aps[j] = aps[i];
  }
  u.n_aps = n;
  u.scanning = false;
  if (open_now() && u.screen == SETUP_WIFI) rebuild_later();
}

static void build_wifi(void) {
  const ui_colors_t *c = ui_theme();
  u.live_state = live_line("wifi_state", u.cb->wifi_state);
  u.live_fn = u.cb->wifi_state;
  char t[64];
  snprintf(t, sizeof t, "Network: %s", u.work.wifi.ssid[0] ? u.work.wifi.ssid : "none");
  text(u.body, t, c->fg);
  part("scan", button(u.body, u.scanning ? "SCANNING..." : "SCAN", on_scan, NULL));
  for (int i = 0; i < u.n_aps; i++) {
    char label[64], pn[48];
    snprintf(label, sizeof label, "%s   %d dBm%s", u.aps[i].ssid, u.aps[i].rssi, u.aps[i].secure ? "" : "   open");
    lv_obj_t *b = button(u.body, label, on_ap, (void *)(uintptr_t)i);
    lv_obj_set_width(b, lv_pct(100));
    style_button(b, !strcmp(u.aps[i].ssid, u.work.wifi.ssid));
    snprintf(pn, sizeof pn, "ap_%s", u.aps[i].ssid);
    part(pn, b);
  }
  lv_obj_t *o = button(u.body, "Other...", on_other, NULL);
  lv_obj_set_width(o, lv_pct(100));
  part("ap_other", o);
}

// ---- Signal K ----

static void host_done(const char *t, uintptr_t arg) {
  (void)arg;
  if (t[0]) snprintf(u.work.sk.host, sizeof u.work.sk.host, "%s", t);
  rebuild_later();
}

static void port_done(const char *t, uintptr_t arg) {
  (void)arg;
  long p = strtol(t, NULL, 10);
  if (p > 0 && p < 65536) u.work.sk.port = (uint16_t)p;
  rebuild_later();
}

static void on_sk_auto(lv_event_t *e) {
  (void)e;
  snprintf(u.work.sk.host, sizeof u.work.sk.host, "auto");
  rebuild_later();
}

static void on_edit_host(lv_event_t *e) {
  (void)e;
  bool aut = !strcmp(u.work.sk.host, "auto");
  kb_open("Server address", aut ? "" : u.work.sk.host, false, false, sizeof u.work.sk.host - 1, host_done, 0);
}

static void on_edit_port(lv_event_t *e) {
  (void)e;
  char p[8];
  snprintf(p, sizeof p, "%u", (unsigned)u.work.sk.port);
  kb_open("Port", p, false, true, 5, port_done, 0);
}

static void on_request_access(lv_event_t *e) {
  (void)e;
  if (u.cb->request_access) u.cb->request_access();
}

static void build_signalk(void) {
  const ui_colors_t *c = ui_theme();
  u.live_state = live_line("sk_state", u.cb->sk_state);
  u.live_fn = u.cb->sk_state;
  bool aut = !strcmp(u.work.sk.host, "auto");
  lv_obj_t *r = row(u.body);
  lv_obj_t *l = text(r, "Server", c->dim);
  lv_obj_set_width(l, u.sc == SIZE_WIDE ? 200 : lv_pct(100));
  lv_obj_t *ba = button(r, "AUTO", on_sk_auto, NULL), *bm = button(r, "MANUAL", on_edit_host, NULL);
  style_button(ba, aut);
  style_button(bm, !aut);
  part("sk_auto", ba);
  part("sk_manual", bm);
  if (!aut) {
    char t[96];
    lv_obj_t *hr = row(u.body);
    snprintf(t, sizeof t, "Host: %s", u.work.sk.host);
    text(hr, t, c->fg);
    part("edit_host", button(hr, "EDIT", on_edit_host, NULL));
  }
  char t[32];
  lv_obj_t *pr = row(u.body);
  snprintf(t, sizeof t, "Port: %u", (unsigned)u.work.sk.port);
  text(pr, t, c->fg);
  part("edit_port", button(pr, "EDIT", on_edit_port, NULL));
  text(u.body, u.work.sk.token[0] ? "Token: set" : "Token: not set (ACK and SILENCE need one)", c->fg);
  part("request_access", button(u.body, "REQUEST ACCESS", on_request_access, NULL));
  u.live_access = live_line("access_state", u.cb->access_state);
}

// ---- discovery labels ----

static void label_done(const char *t, uintptr_t k) {
  snprintf(u.rows[k].label, sizeof u.rows[k].label, "%s", t);
  if (u.rows[k].text && lv_obj_is_valid(u.rows[k].text)) {
    char line[48];
    snprintf(line, sizeof line, "%s%s%s", u.rows[k].id, t[0] ? "  " : "", t);
    lv_label_set_text(u.rows[k].text, line);   // in place: a rebuild would drop unconfirmed keep/skip choices
  }
}

static void on_label(lv_event_t *e) {
  uintptr_t k = (uintptr_t)lv_event_get_user_data(e);
  char prompt[48];
  snprintf(prompt, sizeof prompt, "Label for %s", u.rows[k].id);
  kb_open(prompt, u.rows[k].label, false, false, sizeof u.rows[k].label - 1, label_done, k);
}

void ui_setup_discovery_changed(void) {
  if (open_now() && u.screen == SETUP_DISCOVERY) rebuild_later();
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
  kb_close();   // a screen change drops a half-typed field
  u.screen = s;
  u.live_state = u.live_access = NULL;
  u.live_fn = NULL;
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
    case SETUP_ROLES: build_roles(); break;
    case SETUP_DISCOVERY: build_discovery(); break;
    case SETUP_WIFI: build_wifi(); break;
    case SETUP_SIGNALK: build_signalk(); break;
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
  if (lv_display_get_inactive_time(lv_obj_get_display(u.root)) >= SETUP_IDLE_MS) {
    ui_setup_close();
    return;
  }
  refresh_live(u.live_state, u.live_fn);
  refresh_live(u.live_access, u.cb->access_state);
}
