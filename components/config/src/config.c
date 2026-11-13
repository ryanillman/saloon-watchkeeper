#include "config.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cJSON.h"
#include "platform.h"

static const char *KNOWN_ROLES[] = {"glance", "engine", "power", "tanks", "anchor", "navigation", "depth"};
static const char *DEFAULT_ROLES[] = {"glance", "engine", "power", "tanks", "anchor"};

// The JSON tree last loaded or saved. Saves edit it in place so unknown keys survive.
// The UI, config writer, and console tasks all reach it: every access holds `lock`.
static cJSON *tree;
static void *lock;

static void take(void) {
  if (!lock) lock = platform_lock_create();   // first use is cfg_load at boot, before other tasks
  platform_lock(lock);
}
static void give(void) { platform_unlock(lock); }

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define COPY(dst, json) copy_str((dst), sizeof(dst), (json))

static void copy_str(char *dst, size_t n, const cJSON *j) {
  if (cJSON_IsString(j) && j->valuestring) {
    strncpy(dst, j->valuestring, n - 1);
    dst[n - 1] = 0;
  }
}

static void issue(cfg_report_t *r, const char *key, int line, const char *msg) {
  if (r->count >= ARRAY_LEN(r->issues)) return;
  cfg_issue_t *i = &r->issues[r->count++];
  snprintf(i->key_path, sizeof i->key_path, "%s", key);
  i->line = line;
  snprintf(i->message, sizeof i->message, "%s", msg);
}

#define ANCHOR_PLUGIN_DEFAULT "hoekens-anchor-alarm"
static const char *const ANCHOR_PLUGINS[] = {ANCHOR_PLUGIN_DEFAULT, "anchoralarm", NULL};

static void default_roles(cfg_t *c) {
  memset(c->roles, 0, sizeof c->roles);
  c->role_count = ARRAY_LEN(DEFAULT_ROLES);
  for (size_t i = 0; i < ARRAY_LEN(DEFAULT_ROLES); i++)
    snprintf(c->roles[i].role, sizeof c->roles[i].role, "%s", DEFAULT_ROLES[i]);
}

void cfg_defaults(cfg_t *c) {
  memset(c, 0, sizeof *c);
  strcpy(c->sk.host, "auto");
  c->sk.port = 3000;
  strcpy(c->units.temperature, "C");
  strcpy(c->units.pressure, "bar");
  strcpy(c->units.volume, "L");
  strcpy(c->units.speed, "kn");
  strcpy(c->units.distance, "nm");
  strcpy(c->units.depth, "m");
  strcpy(c->palette, "auto");
  c->brightness = 80;
  c->utc_offset_min = 0;
  strcpy(c->anchor_plugin, ANCHOR_PLUGIN_DEFAULT);
  default_roles(c);
  strcpy(c->start_role, c->roles[0].role);
}

static bool known_role(const char *name) {
  for (size_t i = 0; i < ARRAY_LEN(KNOWN_ROLES); i++)
    if (strcmp(name, KNOWN_ROLES[i]) == 0) return true;
  return false;
}

static const char *const BANK_KINDS[] = {"house", "start", "nav", "windlass", "thruster", "genset", "other"};
static const char *const FLOW_KINDS[] = {"solar", "shore", "engine", "converter", "other"};

static bool in_list(const char *const *list, size_t n, const char *k) {
  for (size_t i = 0; i < n; i++)
    if (!strcmp(list[i], k)) return true;
  return false;
}

static bool is_hex16(const char *s) {
  for (int i = 0; i < 16; i++)
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
  return true;
}

static bool device_ref(const char *s) { return strlen(s) == 16 && is_hex16(s); }

static bool endpoint_ref(const char *s) {   // "<16 hex>.<1-3 digits>"
  size_t n = strlen(s);
  if (n < 18 || n > 20 || !is_hex16(s) || s[16] != '.') return false;
  for (size_t i = 17; i < n; i++)
    if (s[i] < '0' || s[i] > '9') return false;
  return true;
}

static bool slug(const char *s) {
  if (!s[0]) return false;
  for (; *s; s++)
    if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '-')) return false;
  return true;
}

static int bank_index(const cfg_role_t *r, const char *id) {
  for (uint8_t i = 0; i < r->bank_count; i++)
    if (!strcmp(r->bank[i].id, id)) return i;
  return -1;
}

bool cfg_power_has_old_keys(const cfg_role_t *r) { return r->power_old_keys; }

bool cfg_power_check(const cfg_role_t *r, char *why, size_t n) {
  if (r->bank_count == 0 && r->flow_count == 0) return true;
  int houses = 0;
  for (uint8_t i = 0; i < r->bank_count; i++) {
    const cfg_bank_t *k = &r->bank[i];
    if (!in_list(BANK_KINDS, ARRAY_LEN(BANK_KINDS), k->kind)) { snprintf(why, n, "bank %u: kind must be house, start, nav, windlass, thruster, genset or other", i + 1); return false; }
    if (!slug(k->id)) { snprintf(why, n, "bank %u: id must be lower-case letters, digits and dashes", i + 1); return false; }
    if (!endpoint_ref(k->battery)) { snprintf(why, n, "bank %u: battery must be <NAME>.<instance>", i + 1); return false; }
    if (!strcmp(k->kind, "other") && !k->name[0]) { snprintf(why, n, "bank %u: an other bank needs a name", i + 1); return false; }
    if (!strcmp(k->kind, "house")) houses++;
    for (uint8_t j = 0; j < i; j++) {
      if (!strcmp(k->id, r->bank[j].id)) { snprintf(why, n, "bank %u: id \"%s\" is used twice", i + 1, k->id); return false; }
      if (!strcmp(k->battery, r->bank[j].battery)) { snprintf(why, n, "bank %u: battery is already in another bank", i + 1); return false; }
    }
  }
  if (houses != 1) { snprintf(why, n, "banks need exactly one house bank"); return false; }
  for (uint8_t i = 0; i < r->flow_count; i++) {
    const cfg_flow_t *f = &r->flow[i];
    bool conv = !strcmp(f->kind, "converter");
    if (!in_list(FLOW_KINDS, ARRAY_LEN(FLOW_KINDS), f->kind)) { snprintf(why, n, "flow %u: kind must be solar, shore, engine, converter or other", i + 1); return false; }
    if (!device_ref(f->device)) { snprintf(why, n, "flow %u: device must be a 16-digit NAME", i + 1); return false; }
    if (bank_index(r, f->to) < 0) { snprintf(why, n, "flow %u: to \"%s\" is not a bank", i + 1, f->to); return false; }
    if (conv && !f->from[0]) { snprintf(why, n, "flow %u: a converter needs from", i + 1); return false; }
    if (!conv && f->from[0]) { snprintf(why, n, "flow %u: only a converter takes from", i + 1); return false; }
    if (conv && bank_index(r, f->from) < 0) { snprintf(why, n, "flow %u: from \"%s\" is not a bank", i + 1, f->from); return false; }
    if (conv && !strcmp(f->from, f->to)) { snprintf(why, n, "flow %u: a converter's from and to must differ", i + 1); return false; }
    if (f->output > 3) { snprintf(why, n, "flow %u: output must be 0 to 3", i + 1); return false; }
    for (uint8_t j = 0; j < i; j++)
      if (!strcmp(f->device, r->flow[j].device) && f->output == r->flow[j].output) { snprintf(why, n, "flow %u: that device output is already flow %u", i + 1, j + 1); return false; }
  }
  return true;
}

static void parse_role(const cJSON *j, cfg_role_t *r) {
  const cJSON *it;
  COPY(r->role, cJSON_GetObjectItem(j, "role"));
  COPY(r->primary, cJSON_GetObjectItem(j, "primary"));
  cJSON_ArrayForEach(it, cJSON_GetObjectItem(j, "banks")) {
    if (r->bank_count >= CFG_MAX_BANKS) break;
    cfg_bank_t *k = &r->bank[r->bank_count++];
    COPY(k->id, cJSON_GetObjectItem(it, "id"));
    COPY(k->name, cJSON_GetObjectItem(it, "name"));
    COPY(k->kind, cJSON_GetObjectItem(it, "kind"));
    COPY(k->battery, cJSON_GetObjectItem(it, "battery"));
  }
  cJSON_ArrayForEach(it, cJSON_GetObjectItem(j, "flows")) {
    if (r->flow_count >= CFG_MAX_FLOWS) break;
    cfg_flow_t *l = &r->flow[r->flow_count++];
    COPY(l->device, cJSON_GetObjectItem(it, "device"));
    COPY(l->kind, cJSON_GetObjectItem(it, "kind"));
    COPY(l->from, cJSON_GetObjectItem(it, "from"));
    COPY(l->to, cJSON_GetObjectItem(it, "to"));
    const cJSON *o = cJSON_GetObjectItem(it, "output");
    if (cJSON_IsNumber(o) && o->valuedouble >= 0 && o->valuedouble < 255) l->output = (uint8_t)o->valuedouble;
  }
  cJSON_ArrayForEach(it, cJSON_GetObjectItem(j, "instances")) {
    if (r->inst_count >= ARRAY_LEN(r->inst)) break;
    cfg_instance_t *in = &r->inst[r->inst_count++];
    COPY(in->id, cJSON_GetObjectItem(it, "id"));
    COPY(in->label, cJSON_GetObjectItem(it, "label"));
  }
  cJSON_ArrayForEach(it, cJSON_GetObjectItem(j, "overrides")) {
    if (r->ovr_count >= ARRAY_LEN(r->ovr) || !it->string) break;
    cfg_override_t *o = &r->ovr[r->ovr_count++];
    snprintf(o->slot, sizeof o->slot, "%s", it->string);
    const cJSON *mn = cJSON_GetObjectItem(it, "min"), *mx = cJSON_GetObjectItem(it, "max");
    if ((o->has_min = cJSON_IsNumber(mn))) o->min = mn->valuedouble;
    if ((o->has_max = cJSON_IsNumber(mx))) o->max = mx->valuedouble;
  }
  cJSON_ArrayForEach(it, cJSON_GetObjectItem(j, "bind")) {
    if (r->bind_count >= ARRAY_LEN(r->bind) || !it->string) break;
    cfg_bind_t *b = &r->bind[r->bind_count++];
    snprintf(b->slot, sizeof b->slot, "%s", it->string);
    COPY(b->path, it);
  }
  cJSON_ArrayForEach(it, cJSON_GetObjectItem(j, "exclude")) {
    if (r->exclude_count >= ARRAY_LEN(r->exclude)) break;
    COPY(r->exclude[r->exclude_count++], it);
  }
}

// Applies a unit only if it is one we can draw; anything else is reported and
// the default stays. Compared whole, so "barometric" can never become "bar".
static void unit(const cJSON *units, const char *key, char *dst, size_t n, const char *const *allowed,
                 cfg_report_t *rep) {
  const cJSON *j = cJSON_GetObjectItem(units, key);
  if (!j) return;
  for (size_t i = 0; cJSON_IsString(j) && allowed[i]; i++)
    if (strcmp(j->valuestring, allowed[i]) == 0) {
      snprintf(dst, n, "%s", allowed[i]);
      return;
    }
  char path[32], msg[96] = "unknown unit; using the default. Valid:";
  snprintf(path, sizeof path, "units.%s", key);
  for (size_t i = 0; allowed[i]; i++) {
    strncat(msg, " ", sizeof msg - strlen(msg) - 1);
    strncat(msg, allowed[i], sizeof msg - strlen(msg) - 1);
  }
  issue(rep, path, 0, msg);
}

static void parse(const cJSON *root, cfg_t *c, cfg_report_t *rep) {
  const cJSON *w = cJSON_GetObjectItem(root, "wifi");
  COPY(c->wifi.ssid, cJSON_GetObjectItem(w, "ssid"));
  COPY(c->wifi.password, cJSON_GetObjectItem(w, "password"));

  const cJSON *sk = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "sources"), "signalk");
  COPY(c->sk.host, cJSON_GetObjectItem(sk, "host"));
  const cJSON *port = cJSON_GetObjectItem(sk, "port");
  if (cJSON_IsNumber(port)) {
    if (port->valuedouble >= 1 && port->valuedouble <= 65535) c->sk.port = (uint16_t)port->valueint;
    else issue(rep, "sources.signalk.port", 0, "out of range; using the default. Valid: 1-65535");
  }
  COPY(c->sk.token, cJSON_GetObjectItem(sk, "token"));
  COPY(c->sk.request, cJSON_GetObjectItem(sk, "request"));

  const cJSON *n2k = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "sources"), "nmea2000");
  if (n2k) {
    const cJSON *en = cJSON_GetObjectItem(n2k, "enabled");   // `config set` writes a new key as text
    c->n2k.enabled = cJSON_IsTrue(en) || (cJSON_IsString(en) && !strcmp(en->valuestring, "true"));
    const cJSON *mode = cJSON_GetObjectItem(n2k, "mode");
    if (mode) {
      if (cJSON_IsString(mode) && !strcmp(mode->valuestring, "only")) c->n2k.only = true;
      else if (!cJSON_IsString(mode) || strcmp(mode->valuestring, "fallback"))
        issue(rep, "sources.nmea2000.mode", 0, "unknown mode; using fallback. Valid: fallback only");
    }
    const cJSON *it;
    cJSON_ArrayForEach(it, cJSON_GetObjectItem(n2k, "ids")) {
      if (!cJSON_IsString(it) || !it->string) continue;
      if (c->n2k.id_count >= CFG_MAX_N2K_IDS) {
        issue(rep, "sources.nmea2000.ids", 0, "too many ids; the first 16 are used");
        break;
      }
      cfg_n2k_id_t *e = &c->n2k.ids[c->n2k.id_count++];
      snprintf(e->key, sizeof e->key, "%s", it->string);
      snprintf(e->id, sizeof e->id, "%s", it->valuestring);
    }
  }

  const cJSON *u = cJSON_GetObjectItem(root, "units");
  unit(u, "temperature", c->units.temperature, sizeof c->units.temperature, (const char *[]){"C", "F", NULL}, rep);
  unit(u, "pressure", c->units.pressure, sizeof c->units.pressure, (const char *[]){"bar", "psi", "kPa", NULL}, rep);
  unit(u, "volume", c->units.volume, sizeof c->units.volume, (const char *[]){"L", "gal", NULL}, rep);
  unit(u, "speed", c->units.speed, sizeof c->units.speed, (const char *[]){"kn", "m/s", "km/h", NULL}, rep);
  // Before the depth unit existed, distance also chose metres or feet for depth; honour an old file's "m" or "ft".
  const cJSON *old = cJSON_GetObjectItem(u, "distance");
  bool legacy = cJSON_IsString(old) && (!strcmp(old->valuestring, "m") || !strcmp(old->valuestring, "ft"));
  if (legacy && !cJSON_GetObjectItem(u, "depth")) snprintf(c->units.depth, sizeof c->units.depth, "%s", old->valuestring);
  if (!legacy) unit(u, "distance", c->units.distance, sizeof c->units.distance, (const char *[]){"nm", "mi", "km", NULL}, rep);
  unit(u, "depth", c->units.depth, sizeof c->units.depth, (const char *[]){"ft", "m", NULL}, rep);

  const cJSON *d = cJSON_GetObjectItem(root, "display");
  COPY(c->palette, cJSON_GetObjectItem(d, "palette"));
  const cJSON *br = cJSON_GetObjectItem(d, "brightness");
  if (cJSON_IsNumber(br)) {
    if (br->valuedouble < 0 || br->valuedouble > 100) issue(rep, "display.brightness", 0, "out of range; clamped. Valid: 0-100");
    c->brightness = br->valuedouble < 0 ? 0 : br->valuedouble > 100 ? 100 : (uint8_t)br->valueint;
  }
  const cJSON *tz = cJSON_GetObjectItem(d, "utc_offset_min");
  double minutes = 0;
  bool have_tz = false;
  if (cJSON_IsNumber(tz)) {
    minutes = tz->valuedouble;
    have_tz = true;
  } else if (cJSON_IsString(tz)) {   // `config set` creates a key the file lacks as text; the next save writes a number
    char *end;
    long v = strtol(tz->valuestring, &end, 10);
    if (*tz->valuestring && !*end) {
      minutes = (double)v;
      have_tz = true;
    } else {
      issue(rep, "display.utc_offset_min", 0, "must be a whole number of minutes");
    }
  }
  if (have_tz) c->utc_offset_min = minutes < -720 ? -720 : minutes > 840 ? 840 : (int16_t)minutes;
  COPY(c->start_role, cJSON_GetObjectItem(d, "start_role"));
  c->buzzer = cJSON_IsTrue(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "buzzer"), "enabled"));
  const cJSON *ras = cJSON_GetObjectItem(d, "reset_after_swipe");   // `config set` creates an absent key as text
  c->reset_after_swipe = cJSON_IsTrue(ras) || (cJSON_IsString(ras) && !strcmp(ras->valuestring, "true"));
  COPY(c->vessel_name, cJSON_GetObjectItem(cJSON_GetObjectItem(root, "vessel"), "name"));
  const cJSON *ap = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "anchor"), "plugin");
  if (cJSON_IsString(ap)) {
    bool known = false;
    for (size_t i = 0; ANCHOR_PLUGINS[i]; i++) known = known || !strcmp(ap->valuestring, ANCHOR_PLUGINS[i]);
    if (known) snprintf(c->anchor_plugin, sizeof c->anchor_plugin, "%s", ap->valuestring);
    else issue(rep, "anchor.plugin", 0, "unknown anchor plugin; using hoekens-anchor-alarm. Valid: hoekens-anchor-alarm anchoralarm");
  }

  const cJSON *roles = cJSON_GetObjectItem(root, "roles");
  if (roles) {
    memset(c->roles, 0, sizeof c->roles);
    c->role_count = 0;
    int i = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, roles) {
      if (c->role_count >= CFG_MAX_ROLES) break;
      // Parse straight into the next slot (a cfg_role_t is ~2 KB: too big for a small task stack).
      cfg_role_t *r = &c->roles[c->role_count];
      memset(r, 0, sizeof *r);
      parse_role(it, r);
      char key[24];
      snprintf(key, sizeof key, "roles[%d].role", i++);
      if (known_role(r->role)) {
        if (!strcmp(r->role, "power")) {
          char why[96] = "";
          bool over = cJSON_GetArraySize(cJSON_GetObjectItem(it, "banks")) > CFG_MAX_BANKS ||
                      cJSON_GetArraySize(cJSON_GetObjectItem(it, "flows")) > CFG_MAX_FLOWS;
          bool old = cJSON_GetObjectItem(it, "links") != NULL;
          const cJSON *bk;
          cJSON_ArrayForEach(bk, cJSON_GetObjectItem(it, "banks"))
            if (!cJSON_IsString(cJSON_GetObjectItem(bk, "battery"))) old = true;
          if (old) snprintf(why, sizeof why, "old-format banks/links are not read; set up Power on the display");
          else if (over) snprintf(why, sizeof why, "too many banks or flows (at most %d and %d)", CFG_MAX_BANKS, CFG_MAX_FLOWS);
          if (old || over || !cfg_power_check(r, why, sizeof why)) {
            issue(rep, "roles.power.banks", 0, why);
            r->bank_count = 0;
            r->flow_count = 0;
            r->power_rejected = true;
            r->power_old_keys = old;
          }
        }
        r->src_entry = (uint8_t)i;   // i was already advanced: 1-based
        c->role_count++;
      }
      else {
        issue(rep, key, 0, "unknown role; skipped");
        memset(r, 0, sizeof *r);
      }
    }
    if (c->role_count == 0) {
      issue(rep, "roles", 0, "no valid roles; using the default roles");
      default_roles(c);
    }
  }
  if (!known_role(c->start_role)) strcpy(c->start_role, c->roles[0].role);
}

static char *read_text(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = n >= 0 ? malloc((size_t)n + 1) : NULL;
  if (buf) {
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
  }
  fclose(f);
  return buf;
}

static int line_at(const char *text, const char *at) {
  int line = 1;
  for (const char *p = text; p < at && *p; p++)
    if (*p == '\n') line++;
  return line;
}

// Returns true if the file existed and parsed.
static bool try_load(const char *path, const char *name, cfg_t *out, cfg_report_t *rep) {
  char *text = read_text(path);
  if (!text) return false;
  cJSON *root = cJSON_ParseWithOpts(text, NULL, true);
  if (!root || !cJSON_IsObject(root)) {
    const char *err = root ? NULL : cJSON_GetErrorPtr();   // a parsed non-object has no error position
    issue(rep, name, err ? line_at(text, err) : 0, "not valid JSON");
    cJSON_Delete(root);
    free(text);
    return false;
  }
  free(text);
  cfg_defaults(out);
  parse(root, out, rep);
  cJSON_Delete(tree);
  tree = root;
  return true;
}

static cfg_origin_t load_locked(const char *dir, cfg_t *out, cfg_report_t *rep);

cfg_origin_t cfg_load(const char *dir, cfg_t *out, cfg_report_t *rep) {
  take();
  cfg_origin_t o = load_locked(dir, out, rep);
  give();
  return o;
}

static cfg_origin_t load_locked(const char *dir, cfg_t *out, cfg_report_t *rep) {
  char path[160];
  memset(rep, 0, sizeof *rep);
  snprintf(path, sizeof path, "%s/config.json", dir);
  if (try_load(path, "config.json", out, rep)) return CFG_FROM_MAIN;
  snprintf(path, sizeof path, "%s/config.bak.json", dir);
  if (try_load(path, "config.bak.json", out, rep)) return CFG_FROM_BACKUP;
  cfg_defaults(out);
  cJSON_Delete(tree);
  tree = NULL;
  return CFG_FROM_DEFAULTS;
}

// ---- saving: edit the tree in place ----

static cJSON *object(cJSON *parent, const char *key) {
  cJSON *o = cJSON_GetObjectItem(parent, key);
  if (!cJSON_IsObject(o)) {
    cJSON_DeleteItemFromObject(parent, key);
    o = cJSON_AddObjectToObject(parent, key);
  }
  return o;
}

static void set_str(cJSON *o, const char *key, const char *v) {
  cJSON_DeleteItemFromObject(o, key);
  cJSON_AddStringToObject(o, key, v);
}

static void set_num(cJSON *o, const char *key, double v) {
  cJSON_DeleteItemFromObject(o, key);
  cJSON_AddNumberToObject(o, key, v);
}

static void write_role(cJSON *o, const cfg_role_t *r) {
  set_str(o, "role", r->role);
  const char *owned[] = {"instances", "overrides", "bind", "exclude", "primary"};
  for (size_t i = 0; i < ARRAY_LEN(owned); i++) cJSON_DeleteItemFromObject(o, owned[i]);
  if (r->primary[0]) set_str(o, "primary", r->primary);
  // Banks and flows are rewritten when loaded; a rejected topology stays in the file as the user wrote it, and
  // banks cleared on the display are removed. The old `links` key is dropped whenever new banks are written.
  if (!strcmp(r->role, "power") && (r->bank_count || !r->power_rejected)) {
    cJSON_DeleteItemFromObject(o, "banks");
    cJSON_DeleteItemFromObject(o, "flows");
    cJSON_DeleteItemFromObject(o, "links");
  }
  if (r->bank_count) {
    cJSON *a = cJSON_AddArrayToObject(o, "banks");
    for (uint8_t i = 0; i < r->bank_count; i++) {
      cJSON *k = cJSON_CreateObject();
      cJSON_AddStringToObject(k, "id", r->bank[i].id);
      if (r->bank[i].name[0]) cJSON_AddStringToObject(k, "name", r->bank[i].name);
      cJSON_AddStringToObject(k, "kind", r->bank[i].kind);
      cJSON_AddStringToObject(k, "battery", r->bank[i].battery);
      cJSON_AddItemToArray(a, k);
    }
    if (r->flow_count) {
      cJSON *la = cJSON_AddArrayToObject(o, "flows");
      for (uint8_t i = 0; i < r->flow_count; i++) {
        cJSON *l = cJSON_CreateObject();
        cJSON_AddStringToObject(l, "device", r->flow[i].device);
        cJSON_AddStringToObject(l, "kind", r->flow[i].kind);
        if (r->flow[i].output) cJSON_AddNumberToObject(l, "output", r->flow[i].output);
        if (r->flow[i].from[0]) cJSON_AddStringToObject(l, "from", r->flow[i].from);
        cJSON_AddStringToObject(l, "to", r->flow[i].to);
        cJSON_AddItemToArray(la, l);
      }
    }
  }
  if (r->inst_count) {
    cJSON *a = cJSON_AddArrayToObject(o, "instances");
    for (uint8_t i = 0; i < r->inst_count; i++) {
      cJSON *in = cJSON_CreateObject();
      cJSON_AddStringToObject(in, "id", r->inst[i].id);
      cJSON_AddStringToObject(in, "label", r->inst[i].label);
      cJSON_AddItemToArray(a, in);
    }
  }
  if (r->ovr_count) {
    cJSON *ov = cJSON_AddObjectToObject(o, "overrides");
    for (uint8_t i = 0; i < r->ovr_count; i++) {
      cJSON *s = cJSON_AddObjectToObject(ov, r->ovr[i].slot);
      if (r->ovr[i].has_min) cJSON_AddNumberToObject(s, "min", r->ovr[i].min);
      if (r->ovr[i].has_max) cJSON_AddNumberToObject(s, "max", r->ovr[i].max);
    }
  }
  if (r->bind_count) {
    cJSON *b = cJSON_AddObjectToObject(o, "bind");
    for (uint8_t i = 0; i < r->bind_count; i++) cJSON_AddStringToObject(b, r->bind[i].slot, r->bind[i].path);
  }
  if (r->exclude_count) {
    cJSON *e = cJSON_AddArrayToObject(o, "exclude");
    for (uint8_t i = 0; i < r->exclude_count; i++) cJSON_AddItemToArray(e, cJSON_CreateString(r->exclude[i]));
  }
}

// Rebuilds the roles array: each known role updates the entry it came from (matched by
// src_entry and name, else by name), so its own unknown keys stay. Entries the firmware did
// not load (role names it does not know) are kept unchanged after the known roles. Known
// entries no role claims were removed by the user and are dropped.
static void write_roles(cJSON *root, const cfg_t *c) {
  cJSON *old = cJSON_DetachItemFromObject(root, "roles");
  if (!cJSON_IsArray(old)) {
    cJSON_Delete(old);
    old = cJSON_CreateArray();
  }
  int n_old = cJSON_GetArraySize(old);
  bool claimed[64] = {0};
  cJSON *a = cJSON_AddArrayToObject(root, "roles");
  for (uint8_t i = 0; i < c->role_count; i++) {
    const cfg_role_t *r = &c->roles[i];
    int from = -1;
    int k = (int)r->src_entry - 1;
    if (k >= 0 && k < n_old && k < 64 && !claimed[k]) {
      const cJSON *nm = cJSON_GetObjectItem(cJSON_GetArrayItem(old, k), "role");
      if (cJSON_IsString(nm) && strcmp(nm->valuestring, r->role) == 0) from = k;
    }
    for (k = 0; from < 0 && k < n_old && k < 64; k++) {
      const cJSON *nm = cJSON_GetObjectItem(cJSON_GetArrayItem(old, k), "role");
      if (!claimed[k] && cJSON_IsString(nm) && strcmp(nm->valuestring, r->role) == 0) from = k;
    }
    cJSON *o;
    if (from >= 0) {
      claimed[from] = true;
      o = cJSON_Duplicate(cJSON_GetArrayItem(old, from), 1);
    } else {
      o = cJSON_CreateObject();
    }
    write_role(o, r);
    cJSON_AddItemToArray(a, o);
  }
  for (int k = 0; k < n_old; k++) {
    cJSON *it = cJSON_GetArrayItem(old, k);
    const cJSON *nm = cJSON_GetObjectItem(it, "role");
    bool known = cJSON_IsString(nm) && known_role(nm->valuestring);
    if (!(k < 64 && claimed[k]) && !known) cJSON_AddItemToArray(a, cJSON_Duplicate(it, 1));
  }
  cJSON_Delete(old);
}

static bool save_locked(const char *dir, const cfg_t *c) {
  if (!tree) tree = cJSON_CreateObject();
  set_num(tree, "version", CFG_VERSION);
  cJSON *w = object(tree, "wifi");
  set_str(w, "ssid", c->wifi.ssid);
  set_str(w, "password", c->wifi.password);
  cJSON *sk = object(object(tree, "sources"), "signalk");
  set_str(sk, "host", c->sk.host);
  set_num(sk, "port", c->sk.port);
  set_str(sk, "token", c->sk.token);
  if (c->sk.request[0]) set_str(sk, "request", c->sk.request);
  else cJSON_DeleteItemFromObject(sk, "request");
  cJSON *srcs = object(tree, "sources");
  cJSON *n2k = cJSON_GetObjectItem(srcs, "nmea2000");
  if (n2k || c->n2k.enabled || c->n2k.only || c->n2k.id_count) {   // a file that never mentions it stays that way
    n2k = object(srcs, "nmea2000");
    cJSON_DeleteItemFromObject(n2k, "enabled");
    cJSON_AddBoolToObject(n2k, "enabled", c->n2k.enabled);
    set_str(n2k, "mode", c->n2k.only ? "only" : "fallback");
    cJSON_DeleteItemFromObject(n2k, "ids");
    if (c->n2k.id_count) {
      cJSON *ids = cJSON_AddObjectToObject(n2k, "ids");
      for (uint8_t i = 0; i < c->n2k.id_count; i++) cJSON_AddStringToObject(ids, c->n2k.ids[i].key, c->n2k.ids[i].id);
    }
  }
  cJSON *u = object(tree, "units");
  set_str(u, "temperature", c->units.temperature);
  set_str(u, "pressure", c->units.pressure);
  set_str(u, "volume", c->units.volume);
  set_str(u, "speed", c->units.speed);
  set_str(u, "distance", c->units.distance);
  set_str(u, "depth", c->units.depth);
  cJSON *d = object(tree, "display");
  set_str(d, "palette", c->palette);
  set_num(d, "brightness", c->brightness);
  set_num(d, "utc_offset_min", c->utc_offset_min);
  set_str(d, "start_role", c->start_role);
  cJSON_DeleteItemFromObject(d, "reset_after_swipe");
  cJSON_AddBoolToObject(d, "reset_after_swipe", c->reset_after_swipe);
  cJSON *bz = object(tree, "buzzer");
  cJSON_DeleteItemFromObject(bz, "enabled");
  cJSON_AddBoolToObject(bz, "enabled", c->buzzer);
  set_str(object(tree, "vessel"), "name", c->vessel_name);
  set_str(object(tree, "anchor"), "plugin", c->anchor_plugin);
  write_roles(tree, c);

  char *text = cJSON_Print(tree);
  if (!text) {
    fprintf(stderr, "config: save failed: cJSON_Print out of memory\n");
    return false;
  }
  char tmp[160], main_[160], bak[160];
  snprintf(tmp, sizeof tmp, "%s/config.tmp", dir);
  snprintf(main_, sizeof main_, "%s/config.json", dir);
  snprintf(bak, sizeof bak, "%s/config.bak.json", dir);
  FILE *f = fopen(tmp, "w");
  if (!f) fprintf(stderr, "config: save failed: fopen %s: %s\n", tmp, strerror(errno));
  bool ok = f != NULL;
  if (ok && fputs(text, f) < 0) { fprintf(stderr, "config: save failed: write: %s\n", strerror(errno)); ok = false; }
  if (ok && fputs("\n", f) < 0) { fprintf(stderr, "config: save failed: write: %s\n", strerror(errno)); ok = false; }
  if (ok && fflush(f) != 0) { fprintf(stderr, "config: save failed: fflush: %s\n", strerror(errno)); ok = false; }
  if (ok && fsync(fileno(f)) != 0) { fprintf(stderr, "config: save failed: fsync: %s\n", strerror(errno)); ok = false; }
  if (f && fclose(f) != 0) { fprintf(stderr, "config: save failed: fclose: %s\n", strerror(errno)); ok = false; }
  size_t text_len = strlen(text);
  cJSON_free(text);
  if (!ok) {
    remove(tmp);
    return false;
  }
  if (!platform_replace_file(tmp, main_, bak)) {
    fprintf(stderr, "config: save failed: could not replace %s (the previous config stays in place)\n", main_);
    return false;
  }
  fprintf(stderr, "config: saved %s (%u bytes)\n", main_, (unsigned)text_len);
  return true;
}


bool cfg_save(const char *dir, const cfg_t *c) {
  take();
  bool ok = save_locked(dir, c);
  give();
  return ok;
}

// ---- key-path edits ----

// Sets path (dot-separated) in root to text, keeping an existing key's type.
// Returns false with an issue in *err when the text does not fit that type.
static bool set_path(cJSON *root, const char *path, const char *text, cfg_report_t *err) {
  char buf[96];
  snprintf(buf, sizeof buf, "%s", path);
  cJSON *o = root;
  char *seg = buf, *dot;
  while ((dot = strchr(seg, '.')) != NULL) {
    *dot = 0;
    if (!*seg) break;
    cJSON *next = cJSON_GetObjectItem(o, seg);
    if (next && !cJSON_IsObject(next)) {   // never replace an array or value with an empty object
      issue(err, path, 0, "not a key path");
      return false;
    }
    o = next ? next : object(o, seg);
    seg = dot + 1;
  }
  if (!*seg) {
    issue(err, path, 0, "not a key path");
    return false;
  }
  cJSON *cur = cJSON_GetObjectItem(o, seg);
  if (cJSON_IsNumber(cur)) {
    char *end;
    double v = strtod(text, &end);
    if (end == text || *end) {
      issue(err, path, 0, "not a number");
      return false;
    }
    set_num(o, seg, v);
  } else if (cJSON_IsBool(cur)) {
    bool v;
    if (!strcmp(text, "true")) v = true;
    else if (!strcmp(text, "false")) v = false;
    else {
      issue(err, path, 0, "not true or false");
      return false;
    }
    cJSON_DeleteItemFromObject(o, seg);
    cJSON_AddBoolToObject(o, seg, v);
  } else if (cur && !cJSON_IsString(cur)) {
    issue(err, path, 0, "not a plain value; edit config.json by hand");
    return false;
  } else {
    set_str(o, seg, text);
  }
  return true;
}

bool cfg_set(const char *dir, const char *path, const char *text, cfg_t *out, cfg_report_t *err) {
  memset(err, 0, sizeof *err);
  take();
  cJSON *edit = tree ? cJSON_Duplicate(tree, 1) : cJSON_CreateObject();
  bool ok = edit && set_path(edit, path, text, err);
  if (ok) {
    cfg_report_t rep = {0};
    cfg_defaults(out);
    parse(edit, out, &rep);
    size_t pl = strlen(path);
    for (uint8_t i = 0; i < rep.count; i++) {   // only issues about the key being set, or a parent of it, count
      const char *k = rep.issues[i].key_path;
      size_t kl = strlen(k);
      bool match = !strncmp(k, path, pl) || (kl < pl && !strncmp(k, path, kl) && path[kl] == '.');
      if (match) {
        err->issues[err->count++] = rep.issues[i];
        ok = false;
      }
    }
  }
  if (ok) {
    cJSON_Delete(tree);
    tree = edit;
    edit = NULL;
    ok = save_locked(dir, out);
    if (!ok) issue(err, path, 0, "could not write config.json");
  }
  cJSON_Delete(edit);
  give();
  return ok;
}

static void mask(cJSON *o, const char *key) {
  cJSON *v = cJSON_GetObjectItem(o, key);
  if (!cJSON_IsString(v) || !v->valuestring[0]) return;
  char m[32];
  snprintf(m, sizeof m, "set (%u chars)", (unsigned)strlen(v->valuestring));
  set_str(o, key, m);
}

char *cfg_show(const char *dir) {
  (void)dir;
  take();
  cJSON *copy = tree ? cJSON_Duplicate(tree, 1) : NULL;
  give();
  if (!copy) return NULL;
  mask(cJSON_GetObjectItem(copy, "wifi"), "password");
  mask(cJSON_GetObjectItem(cJSON_GetObjectItem(copy, "sources"), "signalk"), "token");
  char *s = cJSON_Print(copy);
  cJSON_Delete(copy);
  return s;
}
