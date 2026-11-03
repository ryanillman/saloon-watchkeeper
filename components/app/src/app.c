#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alerts.h"
#include "boatdata.h"
#include "platform.h"
#include "role.h"
#include "sk_client.h"
#include "sk_parse.h"
#include "ui_alerts.h"
#include "ui_quick.h"
#include "ui_shell.h"

typedef struct {
  const role_def_t *def;   // NULL: not built yet, shows a placeholder
  role_binding_t binding;
  char title[16];
} app_role_t;

PLATFORM_EXT_BSS static struct {   // ~19 KB: PSRAM on the boards
  cfg_t cfg;
  app_role_t *roles;   // calloc'd once in app_init: lands in PSRAM on the boards
  ui_role_view_t views[CFG_MAX_ROLES];
  uint8_t count;
  bool network;
  size_class_t sc;
  uint32_t discoveries;
  void (*buzzer)(bool on);
  bool buzzer_ok, buzzer_state;
  char dir[128];   // config directory; empty = no SD card, nothing is saved
  void (*backlight)(uint8_t percent);
  bool dims;
  lv_obj_t *screen;
  palette_t pal;
  bool ui;
  bool dirty;
  int64_t dirty_ms;
  palette_t auto_last;   // the last palette environment.mode asked for; day until one arrives
} a;

#define SAVE_DELAY_MS 2000

// Config writes happen on their own task: cfg_save prints JSON and does fopen/fsync/rename on
// the SD card, which can stall for many frames. app_tick hands over a copy; a newer copy
// replaces one the worker has not taken yet.
static struct {
  void *lock;
  cfg_t *cfg;     // pending copy, heap (18 KB: PSRAM on the boards, not internal RAM)
  char dir[128];
  bool pending, started;
} w;
static cfg_t *w_work;   // the worker's own copy, heap for the same reason

static void config_writer(void *arg) {
  (void)arg;
  for (;;) {
    char dir[128];
    bool take = false;
    platform_lock(w.lock);
    if (w.pending) {
      *w_work = *w.cfg;
      memcpy(dir, w.dir, sizeof dir);
      w.pending = false;
      take = true;
    }
    platform_unlock(w.lock);
    if (take && !cfg_save(dir, w_work)) fprintf(stderr, "config: cannot save %s\n", dir);
    platform_sleep_ms(100);
  }
}

static void start_writer(void) {
  if (w.started) return;
  w.lock = platform_lock_create();
  w.cfg = malloc(sizeof *w.cfg);   // two buffers over the 16 KB internal-alloc limit go to PSRAM
  w_work = malloc(sizeof *w_work);
  if (!w.lock || !w.cfg || !w_work) {
    fprintf(stderr, "config: no memory for the writer; changes will not be saved\n");
    free(w.cfg);
    free(w_work);
    w.cfg = w_work = NULL;
    return;
  }
  w.started = platform_task(config_writer, NULL, "config", 6144);
  if (!w.started) fprintf(stderr, "config: cannot start the writer task; changes will not be saved\n");
}

const cfg_t *app_init(const char *dir, cfg_report_t *report) {
  bd_init();
  alerts_init();
  app_role_t *roles = a.roles;
  void (*buzzer)(bool) = a.buzzer;
  bool buzzer_ok = a.buzzer_ok;
  void (*backlight)(uint8_t) = a.backlight;
  bool dims = a.dims;
  memset(&a, 0, sizeof a);
  a.backlight = backlight;
  a.dims = dims;
  if (dir) snprintf(a.dir, sizeof a.dir, "%s", dir);
  if (dir) start_writer();
  a.buzzer = buzzer;
  a.buzzer_ok = buzzer_ok;
  if (!roles) roles = calloc(CFG_MAX_ROLES, sizeof *roles);
  else memset(roles, 0, CFG_MAX_ROLES * sizeof *roles);
  a.roles = roles;
  if (dir) cfg_load(dir, &a.cfg, report);
  else {
    cfg_defaults(&a.cfg);
    memset(report, 0, sizeof *report);
  }
  return &a.cfg;
}

static void title_for(const char *name, char *out, size_t n) {
  size_t i = 0;
  for (; name[i] && i + 1 < n; i++) out[i] = (char)(name[i] >= 'a' && name[i] <= 'z' ? name[i] - 32 : name[i]);
  out[i] = 0;
}

static void build(lv_obj_t *area, uint8_t page, size_class_t sc, void *ctx) {
  app_role_t *r = ctx;
  if (r->def) {
    r->def->build_page(area, page, sc, &r->binding);
    r->def->refresh(&r->binding, platform_millis());
    return;
  }
  lv_obj_t *l = lv_label_create(area);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(l, ui_theme()->dim, 0);
  lv_label_set_text_fmt(l, "%s\nnot built yet", r->title);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(l);
}

static void refresh(void *ctx) {
  app_role_t *r = ctx;
  if (r->def) r->def->refresh(&r->binding, platform_millis());
}

static size_t bound_ids(sk_sub_t *out, size_t max) {
  size_t n = 0;
  for (uint8_t i = 0; i < a.count; i++) {
    role_binding_t *b = &a.roles[i].binding;
    if (!a.roles[i].def) continue;
    for (uint8_t g = 0; g < b->def->group_count; g++)
      for (uint8_t j = 0; j < b->g[g].inst_count; j++)
        for (uint8_t k = 0; k < b->def->groups[g].slot_count; k++)
          if (b->g[g].ids[j][k] != BD_NONE && n < max)
            out[n++] = (sk_sub_t){b->g[g].ids[j][k], b->def->groups[g].slots[k].resend_ms};
  }
  if (!strcmp(a.cfg.palette, "auto") && n < max) {   // the auto palette follows environment.mode
    bd_id_t mode = bd_intern("environment.mode");
    if (mode != BD_NONE) out[n++] = (sk_sub_t){mode, 0};
  }
  return n;
}

void app_bind(void) {
  a.count = a.cfg.role_count;
  for (uint8_t i = 0; i < a.count; i++) {
    app_role_t *r = &a.roles[i];
    r->def = role_find(a.cfg.roles[i].role);
    title_for(a.cfg.roles[i].role, r->title, sizeof r->title);
    if (r->def) role_bind(r->def, &a.cfg.roles[i], &a.cfg.units, &r->binding);
    a.views[i] = (ui_role_view_t){
      .name = r->def ? r->def->title : r->title,
      .page_count = !r->def ? 1 : r->def->pages ? r->def->pages(&r->binding, a.sc) : r->def->page_count,
      .build_page = build,
      .refresh = refresh,
      .ctx = r,
    };
  }
}

// The palette environment.mode asks for; day while there is no valid value.
static palette_t auto_palette(void) {
  bd_id_t id = bd_find("environment.mode");
  bd_sample_t s;
  if (id != BD_NONE && bd_get(id, &s) && s.valid && s.kind == BD_TEXT)
    a.auto_last = palette_from_env_mode(s.text);   // only a valid sample moves it: stale, invalid or offline holds
  return a.auto_last;
}

static palette_t palette_of(const char *p) {
  if (!strcmp(p, "auto")) return auto_palette();
  if (!strcmp(p, "dusk")) return PALETTE_DUSK;
  if (!strcmp(p, "night")) return PALETTE_NIGHT;
  return PALETTE_DAY;
}

static void mark_dirty(void) {
  if (!a.dir[0]) return;   // no SD card
  a.dirty = true;
  a.dirty_ms = platform_millis();
}

static void apply_backlight(void) {
  if (!a.backlight) return;
  uint8_t b = a.dims && a.cfg.brightness ? a.cfg.brightness : 100;
  a.backlight(b < 10 ? 10 : b);   // a hand-edited 1..9 would be nearly black
}

static void rebind_subs(void) {
  if (!a.network) return;
  PLATFORM_EXT_BSS static sk_sub_t ids[256];
  sk_client_rebind(ids, bound_ids(ids, 256));
}

static void apply_palette(void) {
  palette_t p = palette_of(a.cfg.palette);
  if (!a.ui || p == a.pal) return;
  a.pal = p;
  ui_shell_set_palette(p);
}

// Quick panel callbacks run inside LVGL events: they only change state; app_tick saves.
static void on_palette(const char *choice) {
  bool was_auto = !strcmp(a.cfg.palette, "auto");
  snprintf(a.cfg.palette, sizeof a.cfg.palette, "%s", choice);
  if (was_auto != !strcmp(a.cfg.palette, "auto")) rebind_subs();   // environment.mode is subscribed only in auto
  apply_palette();
  mark_dirty();
}

static void on_brightness(uint8_t percent) {
  a.cfg.brightness = percent;
  apply_backlight();
  mark_dirty();
}

static const ui_quick_cb_t quick_cb = {on_palette, on_brightness};

static void toggle_quick(void) {
  if (ui_quick_is_open()) ui_quick_close();
  else ui_quick_open(a.screen, a.cfg.palette, a.cfg.brightness ? a.cfg.brightness : 100, a.dims, &quick_cb);
}

// Alert id (a notification path) to the configured role that shows that data, or -1.
static int8_t alert_role(const char *id) {
  static const struct { const char *prefix, *role; } map[] = {
    {"propulsion.", "engine"}, {"electrical.", "power"}, {"tanks.", "tanks"}, {"navigation.anchor.", "anchor"},
  };
  for (size_t m = 0; m < sizeof map / sizeof map[0]; m++) {
    if (strncmp(id, map[m].prefix, strlen(map[m].prefix)) != 0) continue;
    for (uint8_t i = 0; i < a.count; i++)
      if (!strcmp(a.cfg.roles[i].role, map[m].role)) return (int8_t)i;
    return -1;
  }
  return -1;
}

void app_set_buzzer(void (*fn)(bool on), bool enabled_by_board) {
  a.buzzer = fn;
  a.buzzer_ok = enabled_by_board && fn;
  a.buzzer_state = false;
}

void app_set_backlight(void (*fn)(uint8_t percent), bool dims) {
  a.backlight = fn;
  a.dims = dims;
}

void app_start_ui(lv_display_t *disp, size_class_t sc) {
  a.sc = sc;
  for (uint8_t i = 0; i < a.count; i++)   // page counts can depend on the size class
    if (a.roles[i].def && a.roles[i].def->pages) a.views[i].page_count = a.roles[i].def->pages(&a.roles[i].binding, sc);
  uint8_t start = 0;
  for (uint8_t i = 0; i < a.count; i++)
    if (!strcmp(a.cfg.roles[i].role, a.cfg.start_role)) start = i;
  a.pal = palette_of(a.cfg.palette);
  ui_shell_create(disp, sc, a.pal, a.views, a.count, start);
  a.screen = lv_display_get_screen_active(disp);
  a.ui = true;
  ui_shell_set_strip_tap(toggle_quick);
  ui_alerts_attach(a.screen, ui_shell_strip(), sc, alert_role);
  apply_backlight();
}

void app_start_network(void) {
  // Discovery keeps only the groups the configured roles read.
  static const char *prefixes[CFG_MAX_ROLES * ROLE_MAX_GROUPS + 1];
  uint8_t n_prefix = 0;
  for (uint8_t i = 0; i < a.count; i++) {
    const role_def_t *d = a.roles[i].def;
    if (!d) continue;
    for (uint8_t g = 0; g < d->group_count; g++) {
      bool dup = false;
      for (uint8_t j = 0; j < n_prefix; j++) dup |= !strcmp(prefixes[j], d->groups[g].prefix);
      if (!dup) prefixes[n_prefix++] = d->groups[g].prefix;
    }
  }
  prefixes[n_prefix] = NULL;
  sk_parse_set_discovery_prefixes(prefixes);
  PLATFORM_EXT_BSS static sk_sub_t ids[256];
  size_t n = bound_ids(ids, 256);
  sk_client_init(&a.cfg.sk, ids, n);
  sk_client_start();
  a.network = true;
}

// True if any role's instances differ from what a fresh bind finds now.
static bool instances_changed(void) {
  static role_binding_t *fresh;   // ~13 KB; malloc'd once, off the UI task stack
  if (!fresh) fresh = malloc(sizeof *fresh);
  if (!fresh) return false;
  for (uint8_t i = 0; i < a.count; i++) {
    app_role_t *r = &a.roles[i];
    if (!r->def) continue;
    role_bind(r->def, &a.cfg.roles[i], &a.cfg.units, fresh);
    if (!role_binding_same(fresh, &r->binding)) return true;
  }
  return false;
}

void app_tick(void) {
  if (a.network) {
    uint32_t d = sk_client_discovery_count();
    if (d != a.discoveries) {   // the server told us what this boat has
      a.discoveries = d;
      if (instances_changed()) {   // rebuild only when there is something new to show
        app_bind();
        PLATFORM_EXT_BSS static sk_sub_t ids[256];
        sk_client_rebind(ids, bound_ids(ids, 256));
        ui_shell_rebuild();
      }
    }
    const ui_colors_t *c = ui_theme();
    switch (sk_client_state()) {
      case SK_LIVE: ui_shell_set_status("SK LIVE", c->fg); break;
      case SK_DISCOVERING:
      case SK_CONNECTING: ui_shell_set_status("SK CONNECTING", c->warn); break;
      default: ui_shell_set_status("SK OFFLINE", c->alarm); break;
    }
  }
  apply_palette();   // auto: follows environment.mode; changes only when the result does
  ui_shell_refresh();

  PLATFORM_EXT_BSS static alerts_snapshot_t snap;   // 2 KB: off the UI task stack
  int64_t now = platform_millis();
  alerts_snapshot(&snap);
  ui_alerts_update(&snap, now);
  bool buzz = alerts_buzzer_on(&snap, a.cfg.buzzer && a.buzzer_ok, now);
  if (a.buzzer && buzz != a.buzzer_state) {   // call the driver only on a change
    a.buzzer_state = buzz;
    a.buzzer(buzz);
  }
  if (a.dirty && now - a.dirty_ms >= SAVE_DELAY_MS) {   // settled: write once, outside any LVGL event
    a.dirty = false;
    if (w.started) {   // a copy for the writer task; this call does no I/O
      platform_lock(w.lock);
      *w.cfg = a.cfg;
      memcpy(w.dir, a.dir, sizeof w.dir);
      w.pending = true;
      platform_unlock(w.lock);
    }
  }
}
