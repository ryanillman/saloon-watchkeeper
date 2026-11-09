#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alerts.h"
#include "anchor_ctl.h"
#include "anchor_profile.h"
#include "anchor_watch.h"
#include "nav_state.h"
#include "boatdata.h"
#include "app_can.h"
#include "can_instances.h"
#include "depth_state.h"
#include "emergency.h"
#include "emergency_page.h"
#include "glance_page.h"
#include "history.h"
#include "net.h"
#include "platform.h"
#include "role.h"
#include "sk_access.h"
#include "sk_client.h"
#include "sk_cmd.h"
#include "sk_parse.h"
#include "ui_alerts.h"
#include "ui_clock.h"
#include "ui_quick.h"
#include "ui_setup.h"
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
  size_t fuel_levels;   // tanks.fuel.*.currentLevel known last time subscriptions were sent
  size_t path_count;    // bd_count() at the last look for new instances
  void (*buzzer)(bool on);
  bool buzzer_ok, buzzer_state;
  char dir[128];   // config directory; empty = no SD card, nothing is saved
  void (*backlight)(uint8_t percent);
  void (*display_reset)(void);   // restarts the display scan; NULL = the board cannot (no RESET SCREEN button)
  bool dims;
  lv_obj_t *screen;
  palette_t pal;
  bool ui;
  bool dirty;
  int64_t dirty_ms;
  int64_t clock_rx_ms;   // arrival time of the navigation.datetime sample last fed to the clock; -1 = none
  palette_t auto_last;   // the last palette environment.mode asked for; day until one arrives
  cfg_origin_t origin;
} a;

static char info_board[16] = "?", info_version[32] = "?";
static void fill_status_now(app_status_t *s);

#define SAVE_DELAY_MS 2000

// Config writes happen on their own task: cfg_save prints JSON and does fopen/fsync/rename on
// the SD card, which can stall for many frames. app_tick hands over a copy; a newer copy
// replaces one the worker has not taken yet.
static struct {
  void *lock;
  cfg_t *cfg;     // pending copy, heap (18 KB: PSRAM on the boards, not internal RAM)
  char dir[128];
  bool pending, started;
  // A console `config set`, run here after any queued save so a stale copy can't overwrite it.
  bool set_pending, set_done, set_ok;
  char set_path[96], set_text[256];
  cfg_report_t set_err;
  cfg_t *set_out;   // heap, like the others
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
    if (take) fprintf(stderr, "config: saving to %s (wifi \"%s\", signalk %s:%u, token %s)\n", dir, w_work->wifi.ssid,
                      w_work->sk.host, (unsigned)w_work->sk.port, w_work->sk.token[0] ? "set" : "empty");
    if (take && !cfg_save(dir, w_work)) fprintf(stderr, "config: cannot save %s\n", dir);
    platform_lock(w.lock);
    bool set = w.set_pending;
    w.set_pending = false;
    memcpy(dir, w.dir, sizeof dir);
    platform_unlock(w.lock);
    if (set) {
      bool ok = cfg_set(dir, w.set_path, w.set_text, w.set_out, &w.set_err);
      platform_lock(w.lock);
      w.set_ok = ok;
      w.set_done = true;
      platform_unlock(w.lock);
    }
    platform_sleep_ms(take || set ? 10 : 100);
  }
}

static void start_writer(void) {
  if (w.started) return;
  w.lock = platform_lock_create();
  w.cfg = malloc(sizeof *w.cfg);   // buffers over the 16 KB internal-alloc limit go to PSRAM
  w_work = malloc(sizeof *w_work);
  w.set_out = malloc(sizeof *w.set_out);
  if (!w.lock || !w.cfg || !w_work || !w.set_out) {
    fprintf(stderr, "config: no memory for the writer; changes will not be saved\n");
    free(w.cfg);
    free(w_work);
    free(w.set_out);
    w.cfg = w_work = w.set_out = NULL;
    return;
  }
  w.started = platform_task(config_writer, NULL, "config", 6144);
  if (!w.started) fprintf(stderr, "config: cannot start the writer task; changes will not be saved\n");
}

// ---- Emergency buttons ----

#define EM_DEADLINE_MS 3000        // a notification not sent this fast is dropped: late is worse than never
#define EM_POS_MAX_AGE_MS 10000    // an older fix is no fix
#define EM_SEND_ALERT_ID "device.emergency.send"

static struct {
  sk_cmd_handle_t notify, waypoint, cancel;
  bool primary;   // the notification in flight is for the state on screen (a second nature during a MOB is not)
  em_nature_t seen;   // the nature the last tick saw, so a new declaration is noticed once
} emc;

static bool has_role(const char *name);

static void on_sk_notification(const char *id, bool active, bool has_pos, double lat, double lon, const char *ref) {
  em_nature_t n = em_nature_from_path(id);
  if (n != EM_NONE) em_on_notification(n, active, has_pos, lat, lon, ref, platform_millis());
}

bool app_emergency_available(void) { return has_role("navigation") && alerts_source_is_up(); }

const char *app_vessel_name(void) { return a.cfg.vessel_name[0] ? a.cfg.vessel_name : sk_vessel_name(); }
const char *app_vessel_mmsi(void) { return sk_vessel_mmsi(); }

static void raise_send_alert(const char *what, sk_cmd_err_t err, int64_t now) {
  alert_t al = {0};
  snprintf(al.id, sizeof al.id, "%s", EM_SEND_ALERT_ID);
  al.state = AL_ALARM;
  al.audible = true;
  snprintf(al.message, sizeof al.message, "%s NOT SENT%s - TELL HELM", what, err == SKC_ERR_NO_ACCESS ? ": NO WRITE ACCESS" : "");
  alerts_upsert_local(&al, now);
}

static sk_cmd_handle_t em_submit(const char *method, const char *path, const char *body) {
  sk_cmd_t k = {.expire_ms = EM_DEADLINE_MS};
  snprintf(k.method, sizeof k.method, "%s", method);
  snprintf(k.path, sizeof k.path, "%s", path);
  snprintf(k.body, sizeof k.body, "%s", body);
  return sk_cmd_submit(&k, false, platform_millis());
}

// POST /signalk/v2/api/notifications raises it (the server's Notifications API has no per-nature endpoint except mob);
// the reply carries the notification's id, which is what clears it later.
static sk_cmd_handle_t em_send_raise(const em_state_t *s, em_nature_t n) {
  char body[256];
  em_state_t tmp = *s;
  tmp.nature = n;
  return em_raise_body(&tmp, body, sizeof body) ? em_submit("", "/signalk/v2/api/notifications", body) : 0;
}

bool app_emergency_press(em_nature_t n) {
  if (n == EM_NONE || !app_emergency_available()) return false;
  em_state_t cur;
  em_snapshot(&cur);
  if (emc.notify && cur.send == EM_SENDING) return false;   // the last press has not been answered: not a new event
  int64_t now = platform_millis();
  bd_sample_t p = {0};
  bool fix = bd_get(bd_intern("navigation.position"), &p) && p.kind == BD_POSITION && bd_is_fresh(&p, EM_POS_MAX_AGE_MS, now);
  em_state_t s;
  emc.primary = !(cur.nature == EM_MOB && n != EM_MOB);   // a fire during a MOB is sent but does not replace the MOB on screen
  if (emc.primary) {
    em_press(n, fix, fix ? p.pos.lat : 0, fix ? p.pos.lon : 0, now, bd_newest_source_time());
    em_snapshot(&s);
  } else {
    s = cur;
  }
  emc.notify = em_send_raise(&s, n);
  emc.waypoint = 0;
  if (emc.notify == 0) {
    if (emc.primary) em_set_send(EM_NOT_SENT);
    raise_send_alert(em_nature_label(n), SKC_ERR_OFFLINE, now);
  }
  if (emc.primary && n == EM_MOB && s.has_pos) {
    char body[256];
    if (em_waypoint_body(&s, body, sizeof body)) emc.waypoint = em_submit("", "/signalk/v2/api/resources/waypoints", body);
    em_set_waypoint_send(emc.waypoint ? EM_SENDING : EM_NOT_SENT);
  }
  return true;
}

bool app_emergency_cancel(void) {
  em_state_t s;
  em_snapshot(&s);
  if (s.nature == EM_NONE || !app_emergency_available()) return false;
  if (!s.id[0]) {   // the server has not told us this notification's id yet: there is nothing to address the cancel to
    raise_send_alert("CANCEL", SKC_ERR_OFFLINE, platform_millis());
    return false;
  }
  char path[96], body[40];
  snprintf(path, sizeof path, "/signalk/v2/api/notifications/%s", s.id);
  emc.cancel = em_cancel_body(body, sizeof body) ? em_submit("PUT", path, body) : 0;   // PUT {"state":"normal"}; POST .../mob would raise another MOB
  if (emc.cancel == 0) {
    em_set_send(EM_NOT_SENT);   // the emergency stays: other displays and the signal controller still have it
    raise_send_alert("CANCEL", SKC_ERR_OFFLINE, platform_millis());
    return false;
  }
  return true;
}

// The SOS button in the strip jumps to the script page of the Navigation role.
static void on_sos(void) {
  for (uint8_t i = 0; i < a.count; i++)
    if (!strcmp(a.cfg.roles[i].role, "navigation")) {
      ui_shell_goto(i, 1);
      return;
    }
}

// UI task, every tick: reads the answers to what the buttons sent.
static void emergency_tick(int64_t now) {
  if (a.ui) {
    em_state_t shown;
    em_snapshot(&shown);
    // SOS shows for any MOB, Signal K up or not: it only goes to the script, which matters most with the network gone.
    ui_shell_set_sos(shown.nature == EM_MOB && has_role("navigation"));
    // A newly declared fire, flooding, sinking or other emergency selects the script page by itself, once; the crew can
    // leave it. A MOB does not: its GO button and the SOS button take them to the compass target and the script.
    if (shown.nature != emc.seen) {
      emc.seen = shown.nature;
      if (shown.nature != EM_NONE && shown.nature != EM_MOB && has_role("navigation")) on_sos();
    }
  }
  if (emc.notify) {
    sk_cmd_result_t r = sk_cmd_result(emc.notify);
    if (r.state == SKC_OK) {
      if (emc.primary) {
        em_set_send(EM_SENT);
        if (r.id[0]) em_set_server_id(r.id);   // the reply's id is what clears it
      }
      alerts_clear(EM_SEND_ALERT_ID);
      emc.notify = 0;
    } else if (r.state == SKC_FAILED) {
      em_state_t s;
      em_snapshot(&s);
      if (emc.primary) em_set_send(EM_NOT_SENT);
      raise_send_alert(em_nature_label(s.nature), r.err, now);
      emc.notify = 0;
    }
  }
  if (emc.waypoint) {
    sk_cmd_result_t r = sk_cmd_result(emc.waypoint);   // best effort: a failed waypoint never raises the alarm
    if (r.state != SKC_PENDING) {
      em_set_waypoint_send(r.state == SKC_OK ? EM_SENT : EM_NOT_SENT);
      emc.waypoint = 0;
    }
  }
  if (emc.cancel) {
    sk_cmd_result_t r = sk_cmd_result(emc.cancel);
    if (r.state == SKC_OK) {
      em_cancel_sent();
      alerts_clear(EM_SEND_ALERT_ID);
      emc.cancel = 0;
    } else if (r.state == SKC_FAILED) {
      em_set_send(EM_NOT_SENT);
      raise_send_alert("CANCEL", r.err, now);
      emc.cancel = 0;
    }
  }
}

// ---- the Anchor role ----

static anchor_watch_t anchor_watch;   // the boot-restore check and the watch-lost alarm; ticks whenever the role is configured

static bool has_role(const char *name) {
  for (uint8_t i = 0; i < a.cfg.role_count; i++)
    if (!strcmp(a.cfg.roles[i].role, name)) return true;
  return false;
}

static bool has_anchor_role(void) { return has_role("anchor"); }

static uint16_t anchor_submit(const anchor_cmd_t *c, bool replaces_same_path) {
  sk_cmd_t k = {.put_number = c->put_number, .value = c->value};
  snprintf(k.path, sizeof k.path, "%s", c->path);
  snprintf(k.body, sizeof k.body, "%s", c->body);
  return sk_cmd_submit(&k, replaces_same_path, platform_millis());
}

static int anchor_result(uint16_t handle) {
  sk_cmd_result_t r = sk_cmd_result(handle);
  if (r.state == SKC_PENDING) return ANCHOR_RES_PENDING;
  if (r.state == SKC_OK) return ANCHOR_RES_OK;
  switch (r.err) {
    case SKC_ERR_OFFLINE: return ANCHOR_RES_OFFLINE;
    case SKC_ERR_NO_ACCESS: return ANCHOR_RES_NO_ACCESS;
    case SKC_ERR_NO_PLUGIN: return ANCHOR_RES_NO_PLUGIN;
    default: return ANCHOR_RES_REJECTED;
  }
}

static const anchor_ops_t anchor_ops = {anchor_submit, anchor_result};

// (Re)starts the page's controller with the configured plugin; an unknown name (a hand-built config) means Hoeken.
static void anchor_setup(void) {
  anchor_plugin_t p = ANCHOR_PLUGIN_HOEKEN;
  anchor_plugin_from_name(a.cfg.anchor_plugin, &p);
  anchor_ctl_setup(&anchor_ops, p, &anchor_watch, a.dir[0] ? a.dir : NULL, &a.cfg.units);
}

static void anchor_tick(int64_t now) {
  if (!has_anchor_role()) return;
  anchor_view_t v;
  anchor_build_view(now, &v, NULL, 0);   // the watch needs only the mode, not the track
  anchor_watch_tick(&anchor_watch, &v, alerts_source_is_up(), now, a.dir[0] ? a.dir : NULL);
}

PLATFORM_EXT_BSS static can_instances_state_t inst_cache;   // ~4 KB, off internal RAM

const cfg_t *app_init(const char *dir, cfg_report_t *report) {
  bd_init();
  bd_source_register(BD_SRC_SK, "signalk", 2);   // preferred over any raw bus source (NMEA 2000 will rank 1)
  alerts_init();
  hist_init();
  em_reset();
  memset(&emc, 0, sizeof emc);
  sk_set_emergency_cb(on_sk_notification);
  emergency_page_set_vessel_name(app_vessel_name);
  ui_clock_reset();
  app_role_t *roles = a.roles;
  void (*buzzer)(bool) = a.buzzer;
  bool buzzer_ok = a.buzzer_ok;
  void (*backlight)(uint8_t) = a.backlight;
  void (*display_reset)(void) = a.display_reset;
  bool dims = a.dims;
  memset(&a, 0, sizeof a);
  a.clock_rx_ms = -1;
  a.backlight = backlight;
  a.display_reset = display_reset;
  a.dims = dims;
  if (dir) snprintf(a.dir, sizeof a.dir, "%s", dir);
  if (dir) start_writer();
  a.buzzer = buzzer;
  a.buzzer_ok = buzzer_ok;
  if (!roles) roles = calloc(CFG_MAX_ROLES, sizeof *roles);
  else memset(roles, 0, CFG_MAX_ROLES * sizeof *roles);
  a.roles = roles;
  if (dir) a.origin = cfg_load(dir, &a.cfg, report);
  else {
    a.origin = CFG_FROM_DEFAULTS;
    cfg_defaults(&a.cfg);
    memset(report, 0, sizeof *report);
  }
  sk_cmd_init();
  // The NMEA 2000 bus as a fallback to Signal K: after a boot with the server down, the paths Signal K had delivered are
  // restored empty, so the bus has somewhere to write and the roles find their instances.
  if (a.dir[0] && a.cfg.n2k.enabled && !a.cfg.n2k.only) {
    size_t n = can_instances_init(&inst_cache, a.dir);
    if (n) fprintf(stderr, "can: restored %u instance paths from the cache\n", (unsigned)n);
  }
  anchor_watch_init(&anchor_watch, a.dir[0] ? a.dir : NULL, platform_millis(), 60000);
  anchor_setup();
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

size_t app_bound_ids(sk_sub_t *out, size_t max) {
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
  // Fuel percent on the RPM dial. Skip ids a tanks role already subscribed.
  bd_id_t fuel[8];
  size_t nf = bd_match("tanks.fuel.*.currentLevel", fuel, 8);
  for (size_t f = 0; f < nf && n < max; f++) {
    bool seen = false;
    for (size_t j = 0; j < n; j++) seen = seen || out[j].id == fuel[f];
    if (!seen) out[n++] = (sk_sub_t){fuel[f], 0};
  }
  if (!strcmp(a.cfg.palette, "auto") && n < max) {   // the auto palette follows environment.mode
    bd_id_t mode = bd_intern("environment.mode");
    if (mode != BD_NONE) out[n++] = (sk_sub_t){mode, 0};
  }
  if (has_anchor_role()) {
    anchor_ids_t ids;
    anchor_ids_bind(&ids);
    bd_id_t anchor_ids[8];
    size_t na = anchor_sub_ids(&ids, anchor_ids, 8);
    for (size_t i = 0; i < na && n < max; i++) out[n++] = (sk_sub_t){anchor_ids[i], 0};
  }
  if (has_role("navigation")) {
    nav_ids_t ids;
    nav_ids_bind(&ids);
    bd_id_t nav[24];
    size_t nn = nav_sub_ids(&ids, nav, 24);
    for (size_t i = 0; i < nn && n < max; i++) out[n++] = (sk_sub_t){nav[i], 0};
  }
  bd_id_t when = bd_intern("navigation.datetime");   // the strip's clock; it runs on its own between updates
  if (when != BD_NONE && n < max) out[n++] = (sk_sub_t){when, 0};   // on change (about 1 Hz from a GPS): none while the GPS is off
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
  // The Glance page draws a tile for every bound role that has one, and the history store records their trends.
  glance_source_t src[CFG_MAX_ROLES];
  uint8_t ns = 0;
  for (uint8_t i = 0; i < a.count; i++) {
    const app_role_t *r = &a.roles[i];
    if (!r->def || !r->def->tile) continue;
    src[ns++] = (glance_source_t){&r->binding, i};
    glance_tile_t t = {0};
    r->def->tile(&r->binding, platform_millis(), &t);
    if (t.trend != BD_NONE) hist_track(t.trend);
  }
  glance_set_sources(src, ns);
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
  sk_client_rebind(ids, app_bound_ids(ids, 256));
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

// Setup: SAVE applies the edited copy and saves it (when there is an SD card).
static void setup_save(const cfg_t *edited) {
  app_apply_config(edited);
  mark_dirty();
}

static void setup_about(char *out, size_t n) {
  app_status_t s;
  fill_status_now(&s);
  snprintf(out, n,
           "Firmware: %s\nBoard: %s\nIP: %s\nSD card: %s\nConfig: %s\nMemory free: internal %u KB, PSRAM %u KB",
           s.firmware, s.board, s.wifi ? s.ip : "not connected", s.sd ? "mounted" : "none", s.origin, s.internal_kb,
           s.psram_kb);
}

static uint8_t setup_discovery(ui_disc_role_t *out, uint8_t max) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < a.count && n < max; i++) {
    if (!a.roles[i].def) continue;
    memset(&out[n], 0, sizeof out[n]);
    snprintf(out[n].role, sizeof out[n].role, "%s", a.cfg.roles[i].role);
    out[n].count = role_discover(a.roles[i].def, out[n].ids, 16);
    n++;
  }
  return n;
}

static void setup_rediscover(void) {
  if (a.network) sk_client_rediscover_now();
}

// Wi-Fi scan: blocking for a few seconds, so it runs on its own short-lived task and the
// results come back to the UI task.
static struct {
  bool busy;
  net_ap_t found[16];
  int n;
} scan;

static void scan_deliver(void *arg) {
  (void)arg;
  ui_ap_t aps[16];
  for (int i = 0; i < scan.n; i++) {
    snprintf(aps[i].ssid, sizeof aps[i].ssid, "%s", scan.found[i].ssid);
    aps[i].rssi = scan.found[i].rssi;
    aps[i].secure = scan.found[i].secure;
  }
  ui_setup_wifi_results(aps, scan.n);
  scan.busy = false;
}

static void scan_task(void *arg) {
  (void)arg;
  scan.n = net_wifi_scan(scan.found, 16);
  app_post(scan_deliver, NULL, 0);
}

static void setup_wifi_scan(void) {
  if (scan.busy) return;
  scan.busy = true;
  if (!platform_task(scan_task, NULL, "wifiscan", 4096)) {
    scan.busy = false;
    ui_setup_wifi_results(NULL, 0);
  }
}

static void setup_wifi_state(char *out, size_t n) {
  char ip[16];
  int8_t rssi;
  if (!a.cfg.wifi.ssid[0]) snprintf(out, n, "Wi-Fi: not set up");
  else if (net_wifi_info(ip, &rssi)) snprintf(out, n, "Connected to %s, %s, %d dBm", a.cfg.wifi.ssid, ip, rssi);
  else snprintf(out, n, "Connecting to %s", a.cfg.wifi.ssid);
}

static void setup_sk_state(char *out, size_t n) {
  app_status_t s;
  fill_status_now(&s);
  snprintf(out, n, "Signal K: %s", s.signalk);
}

static void setup_request_access(void) {
  char as[64];
  app_request_access(as, sizeof as);
}

static void setup_access_state(char *out, size_t n) {
  ska_state_t st = sk_access_state(NULL);
  if (st == SKA_IDLE) snprintf(out, n, "Access request: none");
  else snprintf(out, n, "Access request: %s (%s)", sk_access_state_name(st), sk_access_message());
}

static ui_setup_cb_t setup_cb = {.save = setup_save, .about = setup_about, .discovery = setup_discovery,
                                 .run_discovery = setup_rediscover, .wifi_scan = setup_wifi_scan,
                                 .wifi_state = setup_wifi_state, .sk_state = setup_sk_state,
                                 .request_access = setup_request_access, .access_state = setup_access_state};

static void open_setup(void) {
  ui_quick_close();
  setup_cb.has_buzzer = a.buzzer_ok;
  setup_cb.has_sd = a.dir[0] != 0;
  setup_cb.dims = a.dims;
  setup_cb.roles_available = role_names();
  ui_setup_open(a.screen, &a.cfg, a.sc, &setup_cb);
}

static void on_buzzer(bool on) {
  a.cfg.buzzer = on;   // alerts_buzzer_on reads it on the next tick
  mark_dirty();
}
static bool buzzer_state(void) { return a.cfg.buzzer; }

// The quick panel's emergency row. A hold declares it here (a single attempt, see app_emergency_press) and goes to the page
// that nature belongs on; CANCEL stays where it is.
static uint8_t alert_page(const char *id);
static bool quick_em_available(void) { return app_emergency_available(); }

static int quick_em_declared(void) {
  em_state_t s;
  em_snapshot(&s);
  switch (s.nature) {
    case EM_NONE: return UI_QUICK_EM_NONE;
    case EM_MOB: return UI_QUICK_EM_MOB;
    case EM_FIRE: return UI_QUICK_EM_FIRE;
    case EM_SINKING: return UI_QUICK_EM_SINKING;
    default: return UI_QUICK_EM_OTHER;   // flooding or other, raised elsewhere: no button, but it can be cancelled
  }
}

static void quick_em_held(int which) {
  static const em_nature_t NATURE[] = {EM_MOB, EM_FIRE, EM_SINKING};
  if (which == UI_QUICK_EM_CANCEL) {
    app_emergency_cancel();
    return;
  }
  if (which < UI_QUICK_EM_MOB || which > UI_QUICK_EM_SINKING || !app_emergency_press(NATURE[which])) return;
  // Where the alert's GO button takes every other display: a MOB to the compass (the target to steer to), the rest to the script.
  const char *path = em_nature_path(NATURE[which]);
  int8_t role = app_alert_role(path);
  if (role >= 0) ui_shell_goto((uint8_t)role, alert_page(path));
}

static ui_quick_cb_t quick_cb = {on_palette, on_brightness, open_setup, on_buzzer, NULL, NULL, quick_em_available, quick_em_declared, quick_em_held};   // buzzer_state set per board

static void do_display_reset(void) {
  if (a.display_reset) a.display_reset();
}

// The settle callback restarts the scan 400 ms after the last page change, when the config asks for it.
static void apply_swipe_reset(void) {
  ui_shell_set_settle_cb(a.cfg.reset_after_swipe && a.display_reset ? do_display_reset : NULL, 400);
}

static void toggle_quick(void) {
  if (ui_quick_is_open()) ui_quick_close();
  else {
    quick_cb.buzzer_state = a.buzzer_ok ? buzzer_state : NULL;   // no switch on a board without a buzzer
    quick_cb.on_reset_screen = a.display_reset ? do_display_reset : NULL;
    ui_quick_open(a.screen, a.cfg.palette, a.cfg.brightness ? a.cfg.brightness : 100, a.dims, &quick_cb);
  }
}

// Alert id (a notification path) to the configured role that shows that data, or -1. A prefix matches the id
// when it is the whole id ("navigation.anchor", the anchor notification's own id) or is followed by a dot.
int8_t app_alert_role(const char *id) {
  static const struct { const char *prefix, *role; } map[] = {
    {"propulsion", "engine"}, {"electrical", "power"}, {"tanks", "tanks"}, {"navigation.anchor", "anchor"},
    {"environment.depth", "depth"},
    {"mob", "navigation"}, {"fire", "navigation"}, {"flooding", "navigation"}, {"sinking", "navigation"},
    {"emergency", "navigation"},
  };
  for (size_t m = 0; m < sizeof map / sizeof map[0]; m++) {
    size_t n = strlen(map[m].prefix);
    if (strncmp(id, map[m].prefix, n) != 0 || (id[n] != '\0' && id[n] != '.')) continue;
    for (uint8_t i = 0; i < a.count; i++)
      if (!strcmp(a.cfg.roles[i].role, map[m].role)) return (int8_t)i;
    return -1;
  }
  return -1;
}

// Page inside the role that shows this alarm, not the role's first page.
static uint8_t alert_page(const char *id) {
  int8_t role = app_alert_role(id);
  if (role < 0) return 0;
  const role_def_t *d = a.roles[role].def;
  if (!d || !d->page_for) return 0;
  uint8_t page = d->page_for(&a.roles[role].binding, a.sc, id);
  uint8_t n = a.views[role].page_count;
  if (n == 0) return 0;
  if (page >= n) page = (uint8_t)(n - 1);
  return page;
}

void app_set_buzzer(void (*fn)(bool on), bool enabled_by_board) {
  a.buzzer = fn;
  a.buzzer_ok = enabled_by_board && fn;
  a.buzzer_state = false;
}

void app_set_display_reset(void (*fn)(void)) { a.display_reset = fn; }

void app_set_backlight(void (*fn)(uint8_t percent), bool dims) {
  a.backlight = fn;
  a.dims = dims;
}

void app_start_ui(lv_display_t *disp, size_class_t sc) {
  a.sc = sc;
  for (uint8_t i = 0; i < a.count; i++)   // page counts can depend on the size class
    if (a.roles[i].def && a.roles[i].def->pages) a.views[i].page_count = a.roles[i].def->pages(&a.roles[i].binding, sc);
  int start = -1, glance = -1;   // the start role; Glance if there is none; else the first
  for (uint8_t i = 0; i < a.count; i++) {
    if (!strcmp(a.cfg.roles[i].role, a.cfg.start_role)) start = i;
    if (!strcmp(a.cfg.roles[i].role, "glance")) glance = i;
  }
  if (start < 0) start = glance >= 0 ? glance : 0;
  a.pal = palette_of(a.cfg.palette);
  ui_shell_create(disp, sc, a.pal, a.views, a.count, start);
  a.screen = lv_display_get_screen_active(disp);
  a.ui = true;
  ui_shell_set_strip_tap(toggle_quick);
  ui_shell_set_sos_cb(on_sos);
  apply_swipe_reset();
  ui_alerts_attach(a.screen, ui_shell_strip(), sc, app_alert_role, alert_page);
  apply_backlight();
}

// ---- Signal K access request ----

static char access_as[64];

// Signal K task. Saving goes through the config writer; without an SD card the token
// still takes effect for this session.
static void session_token(void *arg) {
  cfg_t next = a.cfg;
  snprintf(next.sk.token, sizeof next.sk.token, "%s", (const char *)arg);
  app_apply_config(&next);
}

static void on_access_token(const char *token) {
  cfg_report_t err;
  unsigned bits;
  if (app_config_set("sources.signalk.token", token, &err, &bits)) {
    printf("signalk: access approved; token saved\n");
  } else {
    fprintf(stderr, "signalk: token not saved: %s\n", err.count ? err.issues[0].message : "?");
    if (app_post(session_token, (void *)token, 2000))
      printf("signalk: access approved; token in use until restart (not saved: %s)\n", err.issues[0].message);
    else fprintf(stderr, "signalk: token could not be applied either (UI task did not answer)\n");
  }
}

static void on_access_href(const char *href) {
  cfg_report_t err;
  unsigned bits;
  if (!app_config_set("sources.signalk.request", href, &err, &bits))
    fprintf(stderr, "signalk: could not store the access request: %s\n", err.count ? err.issues[0].message : "?");
  // no SD card: not resumable after a restart
}

static void start_access(void) {
  uint8_t mac[6];
  char id[37];
  platform_mac(mac);
  sk_access_client_id_from_mac(mac, id);
  snprintf(access_as, sizeof access_as, "boat-instruments %s %02x:%02x", info_board, mac[4], mac[5]);
  sk_access_set_identity(id, access_as);
  sk_access_on_token(on_access_token);
  sk_access_on_href(on_access_href);
  if (a.cfg.sk.request[0]) sk_access_resume(a.cfg.sk.request, platform_millis());
}

bool app_request_access(char *as, size_t n) {
  if (!a.network) return false;
  snprintf(as, n, "%s", access_as);
  sk_access_start();
  return true;
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
  // The engine dial shows fuel-tank percent even when the tanks role is not configured.
  bool engine = false, tanks = false;
  for (uint8_t i = 0; i < a.count; i++) {
    if (!a.roles[i].def) continue;
    if (!strcmp(a.roles[i].def->name, "engine")) engine = true;
    if (!strcmp(a.roles[i].def->name, "tanks")) tanks = true;
  }
  if (engine && !tanks && n_prefix < sizeof prefixes / sizeof prefixes[0] - 1)
    prefixes[n_prefix++] = "tanks.fuel";
  prefixes[n_prefix] = NULL;
  sk_parse_set_discovery_prefixes(prefixes);
  PLATFORM_EXT_BSS static sk_sub_t ids[256];
  size_t n = app_bound_ids(ids, 256);
  sk_client_init(&a.cfg.sk, ids, n);
  start_access();
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

const cfg_t *app_config(void) { return &a.cfg; }
const char *app_config_dir(void) { return a.dir; }

static bool same_roles(const cfg_t *x, const cfg_t *y) {
  if (x->role_count != y->role_count || strcmp(x->start_role, y->start_role)) return false;
  for (uint8_t i = 0; i < x->role_count; i++) {
    cfg_role_t p = x->roles[i], q = y->roles[i];
    p.src_entry = q.src_entry = 0;   // where it sits in the file is not a change to show
    if (memcmp(&p, &q, sizeof p)) return false;
  }
  return true;
}

unsigned app_apply_config(const cfg_t *next) {
  unsigned did = APPLY_NONE;
  bool units = memcmp(&a.cfg.units, &next->units, sizeof a.cfg.units) != 0;
  bool display = units || strcmp(a.cfg.palette, next->palette) || a.cfg.brightness != next->brightness ||
                 a.cfg.buzzer != next->buzzer || a.cfg.utc_offset_min != next->utc_offset_min ||
                 a.cfg.reset_after_swipe != next->reset_after_swipe;
  bool sk = strcmp(a.cfg.sk.host, next->sk.host) || a.cfg.sk.port != next->sk.port ||
            strcmp(a.cfg.sk.token, next->sk.token);   // a stored access-request href is not a reason to reconnect
  bool roles = !same_roles(&a.cfg, next);
  bool wifi = strcmp(a.cfg.wifi.ssid, next->wifi.ssid) || strcmp(a.cfg.wifi.password, next->wifi.password);
  bool was_auto = !strcmp(a.cfg.palette, "auto");
  uint8_t old_count = a.count;
  bool anchor_plugin = strcmp(a.cfg.anchor_plugin, next->anchor_plugin) != 0;
  a.cfg = *next;
  if (a.ui) apply_swipe_reset();
  if (anchor_plugin) anchor_setup();
  if (display) {
    did |= APPLY_DISPLAY;
    apply_backlight();
    apply_palette();
    if (was_auto != !strcmp(a.cfg.palette, "auto")) rebind_subs();   // environment.mode follows auto
  }
  if (sk) {
    did |= APPLY_SIGNALK;
    if (a.network) sk_client_reconfigure(&a.cfg.sk);
  }
  if (roles) {
    did |= APPLY_ROLES;
    app_bind();
    rebind_subs();
  }
  if (a.ui && (roles || units)) {   // units change every drawn value; roles change the pages
    if (a.count != old_count) ui_shell_set_roles(a.count);
    else ui_shell_rebuild();
  }
  if (wifi) {
    did |= APPLY_WIFI;
    net_wifi_restart(&a.cfg.wifi);
  }
  return did;
}

// ---- requests from other tasks ----

static struct {
  void *lock;
  struct { app_req_fn fn; void *arg; volatile bool *done; } q[8];
  uint8_t n;
} post;

bool app_post(app_req_fn fn, void *arg, uint32_t wait_ms) {
  if (!post.lock) post.lock = platform_lock_create();   // first use is app_init's task, before others post
  volatile bool done = false;
  platform_lock(post.lock);
  bool room = post.n < sizeof post.q / sizeof post.q[0];
  if (room) post.q[post.n++] = (typeof(post.q[0])){fn, arg, wait_ms ? &done : NULL};
  platform_unlock(post.lock);
  if (!room || !wait_ms) return room;
  // Callers pass pointers into their own stack frame, so a request must never run after its
  // caller has returned. On timeout a still-queued request is withdrawn; one already running
  // is waited for (it is on the UI task and about to finish).
  for (int64_t t0 = platform_millis(); !done; platform_sleep_ms(5)) {
    if (platform_millis() - t0 < wait_ms) continue;
    platform_lock(post.lock);
    for (uint8_t i = 0; i < post.n; i++)
      if (post.q[i].done == &done) {
        memmove(&post.q[i], &post.q[i + 1], (size_t)(post.n - i - 1) * sizeof post.q[0]);
        post.n--;
        platform_unlock(post.lock);
        return false;
      }
    platform_unlock(post.lock);   // not queued: running now, or just finished
  }
  return true;
}

static void run_posted(void) {
  if (!post.lock) return;
  for (;;) {
    platform_lock(post.lock);
    if (!post.n) {
      platform_unlock(post.lock);
      return;
    }
    typeof(post.q[0]) r = post.q[0];
    memmove(&post.q[0], &post.q[1], (size_t)(post.n - 1) * sizeof post.q[0]);
    post.n--;
    platform_unlock(post.lock);
    r.fn(r.arg);
    platform_lock(post.lock);
    if (r.done) *r.done = true;   // its waiter is still there: a waiter only leaves once withdrawn or done
    platform_unlock(post.lock);
  }
}

// The strip clock: navigation.datetime values are offered to it (ui_clock_offer shows one only when a second value
// agrees with the elapsed time, so a stale value Signal K resends is never shown); it then runs on the monotonic
// timer, so losing Signal K does not blank it. No valid time yet (or too old with the source down): not drawn.
static void update_clock(int64_t now) {
  bd_id_t id = bd_find("navigation.datetime");
  bd_sample_t smp;
  int64_t epoch;
  if (id != BD_NONE && bd_get(id, &smp) && smp.valid && smp.kind == BD_TEXT && smp.rx_ms != a.clock_rx_ms) {
    a.clock_rx_ms = smp.rx_ms;
    if (ui_clock_parse_iso(smp.text, &epoch)) ui_clock_offer(epoch, smp.rx_ms);   // shown once a second value agrees
  }
  depth_set_utc_offset(a.cfg.utc_offset_min);   // the Depth role shows tide times in the same local time
  char hhmm[6];
  bool up = a.network && sk_client_state() == SK_LIVE;
  ui_shell_set_clock(ui_clock_format(now, a.cfg.utc_offset_min, up, hhmm) ? hhmm : NULL);
}

void app_tick(void) {
  run_posted();
  uint32_t d = a.network ? sk_client_discovery_count() : a.discoveries;
  size_t paths = bd_count();
  if (d != a.discoveries || paths != a.path_count) {   // the server told us what this boat has, or any source (the NMEA 2000 bus) made new paths
    if (d != a.discoveries) ui_setup_discovery_changed();
    a.discoveries = d;
    a.path_count = paths;
    bd_id_t fuel[8];
    size_t fuel_n = bd_match("tanks.fuel.*.currentLevel", fuel, 8);
    bool fuel_new = fuel_n != a.fuel_levels;
    a.fuel_levels = fuel_n;
    bool inst = instances_changed();
    if (inst) {   // rebuild only when there is something new to show
      app_bind();
      ui_shell_rebuild();
    }
    if (a.network && (inst || fuel_new)) {   // fuel tanks are not an engine instance, but the dial reads them
      PLATFORM_EXT_BSS static sk_sub_t ids[256];
      sk_client_rebind(ids, app_bound_ids(ids, 256));
    }
  }
  if (a.dir[0] && a.cfg.n2k.enabled && !a.cfg.n2k.only)   // learn the boat's instances from Signal K for the next cold start
    can_instances_tick(&inst_cache, a.network && sk_client_state() == SK_LIVE, platform_millis(), a.dir);
  ui_src_t sk_ui = UI_SRC_OFFLINE;   // no client yet (or none by design in "only" mode): the dot says so
  if (a.network) {
    switch (sk_client_state()) {
      case SK_LIVE: sk_ui = UI_SRC_LIVE; bd_source_set_state(BD_SRC_SK, BD_SRC_LIVE); break;
      case SK_DISCOVERING:
      case SK_CONNECTING: sk_ui = UI_SRC_CONNECTING; bd_source_set_state(BD_SRC_SK, BD_SRC_CONNECTING); break;
      default: sk_ui = UI_SRC_OFFLINE; bd_source_set_state(BD_SRC_SK, BD_SRC_OFFLINE); break;
    }
  }
  // The strip's two dots: Signal K (hidden when the bus is the only source) and the NMEA 2000 bus (shown once enabled;
  // a bus that failed to start reads as offline).
  bd_src_state_t bus = bd_source_state(BD_SRC_N2K);
  ui_shell_set_sources((ui_source_t){app_network_wanted(&a.cfg), sk_ui},
                       (ui_source_t){a.cfg.n2k.enabled, bus == BD_SRC_LIVE ? UI_SRC_LIVE : bus == BD_SRC_CONNECTING ? UI_SRC_CONNECTING : UI_SRC_OFFLINE});
  apply_palette();   // auto: follows environment.mode; changes only when the result does
  update_clock(platform_millis());
  hist_tick(platform_millis());
  anchor_tick(platform_millis());
  emergency_tick(platform_millis());
  ui_shell_refresh();
  ui_setup_tick();

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

// ---- console support ----

void app_set_info(const char *board, const char *version) {
  snprintf(info_board, sizeof info_board, "%s", board);
  snprintf(info_version, sizeof info_version, "%s", version);
}

static void fill_status(void *arg) { fill_status_now(arg); }

static void fill_status_now(app_status_t *s) {   // UI task
  memset(s, 0, sizeof *s);
  snprintf(s->firmware, sizeof s->firmware, "%s", info_version);
  snprintf(s->board, sizeof s->board, "%s", info_board);
  s->wifi = net_wifi_info(s->ip, &s->rssi);
  const char *st = "offline";
  if (a.network) {
    switch (sk_client_state()) {
      case SK_LIVE: st = "live"; break;
      case SK_DISCOVERING:
      case SK_CONNECTING: st = "connecting"; break;
      default: break;
    }
  }
  if (!strcmp(a.cfg.sk.host, "auto")) snprintf(s->signalk, sizeof s->signalk, "%s auto", st);
  else snprintf(s->signalk, sizeof s->signalk, "%s %s:%u", st, a.cfg.sk.host, (unsigned)a.cfg.sk.port);
  s->token = a.cfg.sk.token[0] != 0;
  ska_state_t ast = sk_access_state(NULL);
  if (ast == SKA_IDLE) snprintf(s->access, sizeof s->access, "idle");
  else snprintf(s->access, sizeof s->access, "%s (%s)", sk_access_state_name(ast), sk_access_message());
  size_t in, ps;
  platform_mem(&in, &ps);
  s->internal_kb = (unsigned)(in / 1024);
  s->psram_kb = (unsigned)(ps / 1024);
  s->internal_largest_kb = (unsigned)(platform_largest_internal() / 1024);
  s->internal_min_kb = (unsigned)(platform_min_internal() / 1024);
  s->stack_count = (uint8_t)platform_stacks(s->stacks, sizeof s->stacks / sizeof s->stacks[0]);
  s->sd = a.dir[0] != 0;
  snprintf(s->origin, sizeof s->origin, "%s",
           a.origin == CFG_FROM_MAIN ? "main" : a.origin == CFG_FROM_BACKUP ? "backup" : "defaults");
  PLATFORM_EXT_BSS static alerts_snapshot_t snap;
  alerts_snapshot(&snap);
  s->alerts = snap.count;
  static const char *names[] = {"normal", "alert", "warn", "alarm", "emergency"};
  snprintf(s->worst, sizeof s->worst, "%s", snap.count ? names[snap.a[0].state] : "none");
}

bool app_status(app_status_t *out) { return app_post(fill_status, out, 2000); }

typedef struct { const cfg_t *next; unsigned applied; bool save; } apply_req_t;

static void apply_and_save(void *arg) {   // UI task
  apply_req_t *r = arg;
  r->applied = app_apply_config(r->next);
  if (r->save) mark_dirty();   // the file ends with these values even if an older queued copy is written first
}

static void refuse(cfg_report_t *err, const char *key, const char *msg) {
  fprintf(stderr, "config: set %s refused: %s\n", key, msg);
  memset(err, 0, sizeof *err);
  err->count = 1;
  snprintf(err->issues[0].key_path, sizeof err->issues[0].key_path, "%s", key);
  snprintf(err->issues[0].message, sizeof err->issues[0].message, "%s", msg);
}

bool app_config_set(const char *path, const char *text, cfg_report_t *err, unsigned *applied) {
  *applied = APPLY_NONE;
  if (!a.dir[0] || !w.started) {
    refuse(err, path, "no SD card: nothing can be saved");
    return false;
  }
  platform_lock(w.lock);
  snprintf(w.set_path, sizeof w.set_path, "%s", path);
  snprintf(w.set_text, sizeof w.set_text, "%s", text);
  memcpy(w.dir, a.dir, sizeof w.dir);   // the writer's directory came only from queued saves
  w.set_done = false;
  w.set_pending = true;
  platform_unlock(w.lock);
  bool done = false;
  for (int64_t t0 = platform_millis(); !done && platform_millis() - t0 < 5000; platform_sleep_ms(10)) {
    platform_lock(w.lock);
    done = w.set_done;
    platform_unlock(w.lock);
  }
  if (!done) {
    refuse(err, path, "the SD card did not answer");
    return false;
  }
  if (!w.set_ok) {
    *err = w.set_err;
    fprintf(stderr, "config: set %s failed: %s\n", path, err->count ? err->issues[0].message : "?");
    return false;
  }
  apply_req_t r = {w.set_out, 0, true};
  if (!app_post(apply_and_save, &r, 2000)) {
    refuse(err, path, "saved; the display did not take it yet (it applies on restart)");
    return false;
  }
  *applied = r.applied;
  return true;
}

bool app_config_reload(cfg_report_t *rep, unsigned *applied) {
  *applied = APPLY_NONE;
  if (!a.dir[0]) {
    refuse(rep, "config.json", "no SD card");
    return false;
  }
  cfg_t *next = malloc(sizeof *next);
  if (!next) {
    refuse(rep, "config.json", "no memory");
    return false;
  }
  cfg_origin_t origin = cfg_load(a.dir, next, rep);
  apply_req_t r = {next, 0, false};   // a reload never writes: the file may be the broken one
  bool ok = app_post(apply_and_save, &r, 2000);
  if (ok) a.origin = origin;
  *applied = r.applied;
  free(next);
  return ok;
}
