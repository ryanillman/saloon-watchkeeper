// Console commands. The text here is what a person reads at the serial prompt.
#include "app_can.h"
#include "app_cmd.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "config.h"
#include "platform.h"

#define EL_LISTING_MAX 32   // endpoints listed per group

static void outf(app_out_fn out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void outf(app_out_fn out, const char *fmt, ...) {
  char line[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  out(line);
}

// What the display actually holds, one path per line: the ground truth behind a page that says FAIL.
static int data(int argc, char **argv, app_out_fn out) {
  if (argc != 2) {
    out("usage: data <path prefix>   e.g. data electrical.resolved");
    return 1;
  }
  const size_t pl = strlen(argv[1]);
  const unsigned limit = 80;
  unsigned shown = 0, found = 0;
  int64_t now = platform_millis();
  for (bd_id_t id = 1; id <= bd_count(); id++) {
    const char *path = bd_path(id);
    bd_sample_t s;
    if (!path || strncmp(path, argv[1], pl) != 0 || !bd_get(id, &s)) continue;
    found++;
    if (shown >= limit) continue;
    shown++;
    char val[48];
    if (!s.valid) snprintf(val, sizeof val, "INVALID");
    else if (s.kind == BD_NUMBER) snprintf(val, sizeof val, "%.4g", s.num);
    else if (s.kind == BD_TEXT) snprintf(val, sizeof val, "\"%s\"", s.text);
    else snprintf(val, sizeof val, "%.5f,%.5f", s.pos.lat, s.pos.lon);
    outf(out, "%s = %s  age %llds  from %s", path, val, (long long)((now - s.rx_ms) / 1000), bd_source_name(s.source)[0] ? bd_source_name(s.source) : "?");
  }
  if (!found) out("no paths");
  else outf(out, "%u path(s)%s", found, found > shown ? ", the first 80 shown: use a longer prefix" : "");
  return 0;
}

// Every published endpoint with its ref (the CAN NAME and instance a config file names it by), label, live value and what the
// power configuration does with it; then what is configured but not seen. Enough to write banks and flows by hand.
static bool text_of(const char *path, char *out, size_t n) {
  bd_id_t id = bd_find(path);
  bd_sample_t s;
  if (id == BD_NONE || !bd_get(id, &s) || !s.valid || s.kind != BD_TEXT) return false;
  snprintf(out, n, "%s", s.text);
  return true;
}

static int power(app_out_fn out) {
  static const struct { const char *group, *leaf, *unit; bool battery; } G[] = {
    {"batteries", "voltage", "V", true}, {"solar", "voltage", "V", false}, {"chargers", "voltage", "V", false},
    {"alternators", "voltage", "V", false}, {"acin", "current", "A", false}};
  const cfg_t *c = app_config();
  const cfg_role_t *r = NULL;
  for (uint8_t i = 0; i < c->role_count; i++)
    if (!strcmp(c->roles[i].role, "power")) r = &c->roles[i];
  if (r && r->power_old_keys) out("old-format banks/links in config.json are not read: place banks and flows again (Setup > Power)");
  else if (r && r->power_rejected) out("banks/flows in config.json are invalid and not loaded: see config reload");
  bool bank_seen[CFG_MAX_BANKS] = {0}, flow_seen[CFG_MAX_FLOWS] = {0};
  unsigned rows = 0;
  for (size_t g = 0; g < sizeof G / sizeof G[0]; g++) {
    char pat[96];
    snprintf(pat, sizeof pat, "electrical.resolved.%s.*.ref", G[g].group);
    bd_id_t ids[EL_LISTING_MAX];
    size_t n = bd_match(pat, ids, EL_LISTING_MAX);
    for (size_t k = 0; k < n; k++) {
      const char *path = bd_path(ids[k]);
      char ref[40], label[24] = "-", stem[96], p2[112], assign[96] = "UNASSIGNED";
      if (!path || !text_of(path, ref, sizeof ref)) continue;
      snprintf(stem, sizeof stem, "%s", path);
      stem[strlen(stem) - 3] = 0;   // drop "ref"
      snprintf(p2, sizeof p2, "%slabel", stem);
      text_of(p2, label, sizeof label);
      char val[24] = "-";
      snprintf(p2, sizeof p2, "%s%s", stem, G[g].leaf);
      bd_sample_t s;
      bd_id_t vid = bd_find(p2);
      if (vid != BD_NONE && bd_get(vid, &s) && s.valid && s.kind == BD_NUMBER) snprintf(val, sizeof val, "%.1f %s", s.num, G[g].unit);
      if (G[g].battery) {
        for (uint8_t b = 0; r && b < r->bank_count; b++)
          if (!strcmp(r->bank[b].battery, ref)) { snprintf(assign, sizeof assign, "bank %s", r->bank[b].id); bank_seen[b] = true; }
      } else if (!strcmp(G[g].group, "acin")) {
        snprintf(assign, sizeof assign, "AC input");
      } else {
        int ordinal = 0, inst = atoi(ref + 17);   // which of the device's outputs this is, by instance
        for (size_t j = 0; j < n; j++) {
          char other[40];
          const char *op = bd_path(ids[j]);
          if (j != k && op && text_of(op, other, sizeof other) && !strncmp(other, ref, 16) && atoi(other + 17) < inst) ordinal++;
        }
        bool any = false;
        assign[0] = 0;
        for (uint8_t f = 0; r && f < r->flow_count; f++) {
          bool panel = !strcmp(G[g].group, "solar");
          if (strncmp(r->flow[f].device, ref, 16) || (!panel && r->flow[f].output != ordinal)) continue;
          size_t len = strlen(assign);
          if (panel) snprintf(assign + len, sizeof assign - len, "%spanel of flow %s", any ? "; " : "", r->flow[f].kind);
          else if (r->flow[f].from[0]) snprintf(assign + len, sizeof assign - len, "%sflow %s %s -> %s", any ? "; " : "", r->flow[f].kind, r->flow[f].from, r->flow[f].to);
          else snprintf(assign + len, sizeof assign - len, "%sflow %s -> %s", any ? "; " : "", r->flow[f].kind, r->flow[f].to);
          flow_seen[f] = true;
          any = true;
        }
        if (!any) snprintf(assign, sizeof assign, "UNASSIGNED");
      }
      outf(out, "%s  %s  %s  %s", ref, label, val, assign);
      rows++;
    }
  }
  if (!rows) out("no endpoints seen (nothing with a CAN NAME yet)");
  for (uint8_t b = 0; r && b < r->bank_count; b++)
    if (!bank_seen[b]) outf(out, "MISSING bank %s  %s", r->bank[b].id, r->bank[b].battery);
  for (uint8_t f = 0; r && f < r->flow_count; f++)
    if (!flow_seen[f]) outf(out, "MISSING flow %s  %s", r->flow[f].kind, r->flow[f].device);
  return 0;
}

// The config is rewritten whole on the UI task (app_apply_config), so the listing runs there too.
typedef struct { app_out_fn out; int rc; } power_req_t;
static void power_on_ui(void *arg) {
  power_req_t *r = arg;
  r->rc = power(r->out);
}
static int power_cmd(app_out_fn out) {
  power_req_t r = {out, 1};
  if (!app_post(power_on_ui, &r, 2000)) {
    out("power: the display did not answer");
    return 1;
  }
  return r.rc;
}

static int help(app_out_fn out) {
  out("status                      firmware, network, Signal K, memory, SD, alerts");
  out("data <prefix>               the data model under a path prefix: value, age, source (data electrical.resolved)");
  out("power                       endpoints by ref (CAN NAME.instance) with what Power does with each, for writing banks and flows");
  out("config show                 the config (password and token masked)");
  out("config set <path> <value>   change one setting, save it, and apply it");
  out("                            e.g. config set sources.signalk.token <token>");
  out("config reload               read config.json again and apply it");
  out("sk request-access           ask Signal K for a token (approve it in the admin UI)");
  out("reboot                      restart");
  return 0;
}

static int status(app_out_fn out) {
  app_status_t s;
  if (!app_status(&s)) {
    out("status: the display did not answer");
    return 1;
  }
  outf(out, "firmware: %s", s.firmware);
  outf(out, "board: %s", s.board);
  if (s.wifi) {
    outf(out, "ip: %s", s.ip);
    outf(out, "rssi: %d dBm", s.rssi);
  } else {
    out("ip: none (Wi-Fi not connected)");
    out("rssi: -");
  }
  outf(out, "signalk: %s", s.signalk);
  app_can_status_t can;
  app_can_status(&can);
  if (!can.started) {
    out("can: off");
  } else {
    const char *st = can.state == BD_SRC_LIVE ? "live" : can.state == BD_SRC_OFFLINE ? "offline" : "connecting";
    outf(out, "can: %s rx=%u values=%u unmapped=%u bus_off=%u", st, (unsigned)can.rx_frames, (unsigned)can.values,
         (unsigned)can.unmapped, (unsigned)can.bus_off_events);
  }
  outf(out, "token: %s", s.token ? "set" : "not set");
  outf(out, "access: %s", s.access);
  outf(out, "ram: internal %u KB, psram %u KB free", s.internal_kb, s.psram_kb);
  outf(out, "ram: internal largest block %u KB, lowest free since boot %u KB", s.internal_largest_kb, s.internal_min_kb);
  char stk[128] = "";
  for (uint8_t i = 0; i < s.stack_count; i++)
    snprintf(stk + strlen(stk), sizeof stk - strlen(stk), " %s %u", s.stacks[i].name, (unsigned)s.stacks[i].min_free);
  outf(out, "stacks: least free bytes:%s", s.stack_count ? stk : " n/a");
  outf(out, "sd: %s", s.sd ? "mounted" : "none");
  outf(out, "config: %s", s.origin);
  outf(out, "alerts: %u active, worst %s", s.alerts, s.worst);
  return 0;
}

static void issues(app_out_fn out, const cfg_report_t *r) {
  for (uint8_t i = 0; i < r->count; i++)
    outf(out, "error: %s: %s", r->issues[i].key_path, r->issues[i].message);
}

static void applied_line(app_out_fn out, const char *done, unsigned bits) {
  char list[96] = "";
  static const struct { unsigned bit; const char *name; } names[] = {
    {APPLY_DISPLAY, "display"}, {APPLY_SIGNALK, "reconnect signalk"},
    {APPLY_ROLES, "rebind roles"}, {APPLY_WIFI, "restart wifi"},
  };
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
    if (bits & names[i].bit) {
      if (list[0]) strncat(list, ", ", sizeof list - strlen(list) - 1);
      strncat(list, names[i].name, sizeof list - strlen(list) - 1);
    }
  outf(out, "%s; applied: %s", done, list[0] ? list : "nothing to apply");
}

static int config(int argc, char **argv, app_out_fn out) {
  if (argc >= 2 && !strcmp(argv[1], "show")) {
    char *text = cfg_show(app_config_dir());
    if (!text) {
      out("no config file loaded (built-in defaults; no SD card or no config.json)");
      return 0;
    }
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) out(line);
    free(text);
    return 0;
  }
  if (argc >= 2 && !strcmp(argv[1], "set")) {
    if (argc < 4) {
      out("usage: config set <path> <value>");
      return 1;
    }
    char value[256] = "";   // a value with spaces arrives as several words unless quoted
    for (int i = 3; i < argc; i++) {
      if (i > 3) strncat(value, " ", sizeof value - strlen(value) - 1);
      strncat(value, argv[i], sizeof value - strlen(value) - 1);
    }
    cfg_report_t err;
    unsigned bits;
    if (!app_config_set(argv[2], value, &err, &bits)) {
      issues(out, &err);
      return 1;
    }
    applied_line(out, "saved", bits);
    return 0;
  }
  if (argc >= 2 && !strcmp(argv[1], "reload")) {
    cfg_report_t rep;
    unsigned bits;
    bool ok = app_config_reload(&rep, &bits);
    issues(out, &rep);
    if (!ok) return 1;
    applied_line(out, "reloaded", bits);
    return 0;
  }
  out("usage: config show | config set <path> <value> | config reload");
  return 1;
}

static int sk(int argc, char **argv, app_out_fn out) {
  if (argc >= 2 && !strcmp(argv[1], "request-access")) {
    char as[64];
    if (!app_request_access(as, sizeof as)) {
      out("Signal K is not running on this panel");
      return 1;
    }
    outf(out, "requested as '%s'", as);
    out("approve it in the Signal K admin UI: Security > Access Requests (read/write)");
    out("`status` shows progress; the token is saved when it is approved");
    return 0;
  }
  out("usage: sk request-access");
  return 1;
}

int app_cmd_run(int argc, char **argv, app_out_fn out) {
  if (argc < 1) return 0;
  if (!strcmp(argv[0], "help")) return help(out);
  if (!strcmp(argv[0], "status")) return status(out);
  if (!strcmp(argv[0], "data")) return data(argc, argv, out);
  if (!strcmp(argv[0], "power")) return power_cmd(out);
  if (!strcmp(argv[0], "config")) return config(argc, argv, out);
  if (!strcmp(argv[0], "sk")) return sk(argc, argv, out);
  if (!strcmp(argv[0], "reboot")) {
    out("restarting");
    platform_sleep_ms(100);   // let the line reach the terminal
    platform_reboot();
    return 0;
  }
  outf(out, "unknown command '%s'; type help", argv[0]);
  return 1;
}
