// Console commands. The text here is what a person reads at the serial prompt.
#include "app_cmd.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "config.h"
#include "platform.h"

static void outf(app_out_fn out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void outf(app_out_fn out, const char *fmt, ...) {
  char line[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  out(line);
}

static int help(app_out_fn out) {
  out("status                      firmware, network, Signal K, memory, SD, alerts");
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
  outf(out, "token: %s", s.token ? "set" : "not set");
  outf(out, "access: %s", s.access);
  outf(out, "ram: internal %u KB, psram %u KB free", s.internal_kb, s.psram_kb);
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
    out("not available yet");
    return 1;
  }
  out("usage: sk request-access");
  return 1;
}

int app_cmd_run(int argc, char **argv, app_out_fn out) {
  if (argc < 1) return 0;
  if (!strcmp(argv[0], "help")) return help(out);
  if (!strcmp(argv[0], "status")) return status(out);
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
