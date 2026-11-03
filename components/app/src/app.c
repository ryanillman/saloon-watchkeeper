#include "app.h"
#include <stdlib.h>
#include <string.h>
#include "alerts.h"
#include "boatdata.h"
#include "platform.h"
#include "role.h"
#include "sk_client.h"
#include "sk_parse.h"
#include "ui_alerts.h"
#include "ui_shell.h"

typedef struct {
  const role_def_t *def;   // NULL: not built yet, shows a placeholder
  role_binding_t binding;
  char title[16];
} app_role_t;

static struct {
  cfg_t cfg;
  app_role_t *roles;   // calloc'd once in app_init: lands in PSRAM on the boards
  ui_role_view_t views[CFG_MAX_ROLES];
  uint8_t count;
  bool network;
  size_class_t sc;
  uint32_t discoveries;
  void (*buzzer)(bool on);
  bool buzzer_ok, buzzer_state;
} a;

const cfg_t *app_init(const char *dir, cfg_report_t *report) {
  bd_init();
  alerts_init();
  app_role_t *roles = a.roles;
  void (*buzzer)(bool) = a.buzzer;
  bool buzzer_ok = a.buzzer_ok;
  memset(&a, 0, sizeof a);
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

static palette_t palette_of(const char *p) {
  if (!strcmp(p, "dusk")) return PALETTE_DUSK;
  if (!strcmp(p, "night")) return PALETTE_NIGHT;
  return PALETTE_DAY;
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

void app_start_ui(lv_display_t *disp, size_class_t sc) {
  a.sc = sc;
  for (uint8_t i = 0; i < a.count; i++)   // page counts can depend on the size class
    if (a.roles[i].def && a.roles[i].def->pages) a.views[i].page_count = a.roles[i].def->pages(&a.roles[i].binding, sc);
  uint8_t start = 0;
  for (uint8_t i = 0; i < a.count; i++)
    if (!strcmp(a.cfg.roles[i].role, a.cfg.start_role)) start = i;
  ui_shell_create(disp, sc, palette_of(a.cfg.palette), a.views, a.count, start);
  ui_alerts_attach(lv_display_get_screen_active(disp), ui_shell_strip(), sc, alert_role);
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
  static sk_sub_t ids[256];
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
        static sk_sub_t ids[256];
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
  ui_shell_refresh();

  static alerts_snapshot_t snap;   // 2 KB: off the UI task stack
  int64_t now = platform_millis();
  alerts_snapshot(&snap);
  ui_alerts_update(&snap, now);
  bool buzz = alerts_buzzer_on(&snap, a.cfg.buzzer && a.buzzer_ok, now);
  if (a.buzzer && buzz != a.buzzer_state) {   // call the driver only on a change
    a.buzzer_state = buzz;
    a.buzzer(buzz);
  }
}
