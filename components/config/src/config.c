#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cJSON.h"
#include "platform.h"

static const char *KNOWN_ROLES[] = {"glance", "engine", "power", "tanks", "anchor", "wind", "nav"};
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
  strcpy(c->palette, "auto");
  c->brightness = 80;
  c->utc_offset_min = 0;
  default_roles(c);
  strcpy(c->start_role, c->roles[0].role);
}

static bool known_role(const char *name) {
  for (size_t i = 0; i < ARRAY_LEN(KNOWN_ROLES); i++)
    if (strcmp(name, KNOWN_ROLES[i]) == 0) return true;
  return false;
}

static bool kind_ok(const char *k) { return !strcmp(k, "house") || !strcmp(k, "start") || !strcmp(k, "nav"); }

static bool has_kind(const cfg_role_t *r, const char *k) {
  for (uint8_t i = 0; i < r->bank_count; i++)
    if (strcmp(r->bank[i].kind, k) == 0) return true;
  return false;
}

bool cfg_power_topology_check(const cfg_role_t *r, char *why, size_t n) {
  if (r->bank_count == 0) return true;
  for (uint8_t i = 0; i < r->bank_count; i++) {
    if (!kind_ok(r->bank[i].kind)) { snprintf(why, n, "bank %u: kind must be house, start, or nav", i + 1); return false; }
    if (!r->bank[i].id[0]) { snprintf(why, n, "bank %u: id is missing", i + 1); return false; }
    for (uint8_t j = 0; j < i; j++)
      if (!strcmp(r->bank[i].kind, r->bank[j].kind)) { snprintf(why, n, "two %s banks; at most one of each kind", r->bank[i].kind); return false; }
  }
  if (!has_kind(r, "house")) { snprintf(why, n, "banks need exactly one house bank"); return false; }
  static const char *const groups[] = {"charger", "solar", "dcdc", "alternator"};
  for (uint8_t i = 0; i < r->link_count; i++) {
    const cfg_link_t *l = &r->link[i];
    const char *colon = strchr(l->src, ':');
    bool grp = false;
    if (colon && colon[1])
      for (size_t g = 0; g < ARRAY_LEN(groups); g++)
        if ((size_t)(colon - l->src) == strlen(groups[g]) && !strncmp(l->src, groups[g], strlen(groups[g]))) grp = true;
    if (!grp) { snprintf(why, n, "link %u: src must be charger, solar, dcdc, or alternator, then :<instance>", i + 1); return false; }
    bool dcdc = !strncmp(l->src, "dcdc:", 5);
    if (!has_kind(r, l->to)) { snprintf(why, n, "link %u: to \"%s\" is not a configured bank", i + 1, l->to); return false; }
    if (dcdc && !l->from[0]) { snprintf(why, n, "link %u: a dcdc link needs from", i + 1); return false; }
    if (!dcdc && l->from[0]) { snprintf(why, n, "link %u: only a dcdc link takes from", i + 1); return false; }
    if (dcdc && !has_kind(r, l->from)) { snprintf(why, n, "link %u: from \"%s\" is not a configured bank", i + 1, l->from); return false; }
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
  }
  cJSON_ArrayForEach(it, cJSON_GetObjectItem(j, "links")) {
    if (r->link_count >= CFG_MAX_LINKS) break;
    cfg_link_t *l = &r->link[r->link_count++];
    COPY(l->src, cJSON_GetObjectItem(it, "src"));
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
  if (cJSON_IsNumber(port) && port->valueint > 0 && port->valueint < 65536) c->sk.port = (uint16_t)port->valueint;
  COPY(c->sk.token, cJSON_GetObjectItem(sk, "token"));
  COPY(c->sk.request, cJSON_GetObjectItem(sk, "request"));

  const cJSON *u = cJSON_GetObjectItem(root, "units");
  unit(u, "temperature", c->units.temperature, sizeof c->units.temperature, (const char *[]){"C", "F", NULL}, rep);
  unit(u, "pressure", c->units.pressure, sizeof c->units.pressure, (const char *[]){"bar", "psi", "kPa", NULL}, rep);
  unit(u, "volume", c->units.volume, sizeof c->units.volume, (const char *[]){"L", "gal", NULL}, rep);
  unit(u, "speed", c->units.speed, sizeof c->units.speed, (const char *[]){"kn", "m/s", "km/h", NULL}, rep);
  unit(u, "distance", c->units.distance, sizeof c->units.distance, (const char *[]){"nm", "m", "ft", NULL}, rep);

  const cJSON *d = cJSON_GetObjectItem(root, "display");
  COPY(c->palette, cJSON_GetObjectItem(d, "palette"));
  const cJSON *br = cJSON_GetObjectItem(d, "brightness");
  if (cJSON_IsNumber(br)) c->brightness = br->valueint < 0 ? 0 : br->valueint > 100 ? 100 : (uint8_t)br->valueint;
  const cJSON *tz = cJSON_GetObjectItem(d, "utc_offset_min");
  if (cJSON_IsNumber(tz)) c->utc_offset_min = tz->valuedouble < -720 ? -720 : tz->valuedouble > 840 ? 840 : (int16_t)tz->valueint;
  COPY(c->start_role, cJSON_GetObjectItem(d, "start_role"));
  c->buzzer = cJSON_IsTrue(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "buzzer"), "enabled"));

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
                      cJSON_GetArraySize(cJSON_GetObjectItem(it, "links")) > CFG_MAX_LINKS;
          if (over) snprintf(why, sizeof why, "too many banks or links (at most %d and %d)", CFG_MAX_BANKS, CFG_MAX_LINKS);
          if (over || !cfg_power_topology_check(r, why, sizeof why)) {
            issue(rep, "roles.power.banks", 0, why);
            r->bank_count = 0;
            r->link_count = 0;
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
    const char *err = cJSON_GetErrorPtr();
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
  // banks and links are rewritten only when loaded; a rejected topology stays in the file as the user wrote it.
  if (r->bank_count) {
    cJSON_DeleteItemFromObject(o, "banks");
    cJSON_DeleteItemFromObject(o, "links");
    cJSON *a = cJSON_AddArrayToObject(o, "banks");
    for (uint8_t i = 0; i < r->bank_count; i++) {
      cJSON *k = cJSON_CreateObject();
      cJSON_AddStringToObject(k, "id", r->bank[i].id);
      if (r->bank[i].name[0]) cJSON_AddStringToObject(k, "name", r->bank[i].name);
      cJSON_AddStringToObject(k, "kind", r->bank[i].kind);
      cJSON_AddItemToArray(a, k);
    }
    if (r->link_count) {
      cJSON *la = cJSON_AddArrayToObject(o, "links");
      for (uint8_t i = 0; i < r->link_count; i++) {
        cJSON *l = cJSON_CreateObject();
        cJSON_AddStringToObject(l, "src", r->link[i].src);
        if (r->link[i].output) cJSON_AddNumberToObject(l, "output", r->link[i].output);
        if (r->link[i].from[0]) cJSON_AddStringToObject(l, "from", r->link[i].from);
        cJSON_AddStringToObject(l, "to", r->link[i].to);
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
  cJSON *u = object(tree, "units");
  set_str(u, "temperature", c->units.temperature);
  set_str(u, "pressure", c->units.pressure);
  set_str(u, "volume", c->units.volume);
  set_str(u, "speed", c->units.speed);
  set_str(u, "distance", c->units.distance);
  cJSON *d = object(tree, "display");
  set_str(d, "palette", c->palette);
  set_num(d, "brightness", c->brightness);
  set_num(d, "utc_offset_min", c->utc_offset_min);
  set_str(d, "start_role", c->start_role);
  cJSON *bz = object(tree, "buzzer");
  cJSON_DeleteItemFromObject(bz, "enabled");
  cJSON_AddBoolToObject(bz, "enabled", c->buzzer);
  write_roles(tree, c);

  char *text = cJSON_Print(tree);
  if (!text) return false;
  char tmp[160], main_[160], bak[160];
  snprintf(tmp, sizeof tmp, "%s/config.tmp", dir);
  snprintf(main_, sizeof main_, "%s/config.json", dir);
  snprintf(bak, sizeof bak, "%s/config.bak.json", dir);
  FILE *f = fopen(tmp, "w");
  bool ok = f && fputs(text, f) >= 0 && fputs("\n", f) >= 0 && fflush(f) == 0 && fsync(fileno(f)) == 0;
  if (f) ok = (fclose(f) == 0) && ok;
  cJSON_free(text);
  if (!ok) {
    remove(tmp);
    return false;
  }
  // FAT will not rename over an existing file, so clear the old backup first.
  // A power cut after this point leaves config.bak.json for cfg_load to find.
  remove(bak);
  rename(main_, bak);
  return rename(tmp, main_) == 0;
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
    o = object(o, seg);
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
    for (uint8_t i = 0; i < rep.count; i++)   // only issues about the key being set count
      if (!strncmp(rep.issues[i].key_path, path, pl)) {
        err->issues[err->count++] = rep.issues[i];
        ok = false;
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
