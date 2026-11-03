#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cJSON.h"

static const char *KNOWN_ROLES[] = {"engine", "power", "tanks", "anchor", "wind", "nav"};
static const char *DEFAULT_ROLES[] = {"engine", "power", "tanks", "anchor"};

// The JSON tree last loaded or saved. Saves edit it in place so unknown keys survive.
static cJSON *tree;

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
  default_roles(c);
  strcpy(c->start_role, c->roles[0].role);
}

static bool known_role(const char *name) {
  for (size_t i = 0; i < ARRAY_LEN(KNOWN_ROLES); i++)
    if (strcmp(name, KNOWN_ROLES[i]) == 0) return true;
  return false;
}

static void parse_role(const cJSON *j, cfg_role_t *r) {
  const cJSON *it;
  COPY(r->role, cJSON_GetObjectItem(j, "role"));
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
  COPY(c->start_role, cJSON_GetObjectItem(d, "start_role"));
  c->buzzer = cJSON_IsTrue(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "buzzer"), "enabled"));

  const cJSON *roles = cJSON_GetObjectItem(root, "roles");
  if (roles) {
    memset(c->roles, 0, sizeof c->roles);
    c->role_count = 0;
    int i = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, roles) {
      cfg_role_t r = {0};
      parse_role(it, &r);
      char key[24];
      snprintf(key, sizeof key, "roles[%d].role", i++);
      if (!known_role(r.role)) issue(rep, key, 0, "unknown role; skipped");
      else if (c->role_count < CFG_MAX_ROLES) c->roles[c->role_count++] = r;
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

cfg_origin_t cfg_load(const char *dir, cfg_t *out, cfg_report_t *rep) {
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
  const char *owned[] = {"instances", "overrides", "bind", "exclude"};
  for (size_t i = 0; i < ARRAY_LEN(owned); i++) cJSON_DeleteItemFromObject(o, owned[i]);
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

static void write_roles(cJSON *root, const cfg_t *c) {
  cJSON *a = cJSON_GetObjectItem(root, "roles");
  if (!cJSON_IsArray(a)) {
    cJSON_DeleteItemFromObject(root, "roles");
    a = cJSON_AddArrayToObject(root, "roles");
  }
  for (uint8_t i = 0; i < c->role_count; i++) {
    cJSON *o = cJSON_GetArrayItem(a, i);
    const cJSON *name = cJSON_GetObjectItem(o, "role");
    bool same = cJSON_IsObject(o) && cJSON_IsString(name) && strcmp(name->valuestring, c->roles[i].role) == 0;
    if (!same) {   // a different role now sits here; start this entry fresh
      cJSON *fresh = cJSON_CreateObject();
      if (o) cJSON_ReplaceItemInArray(a, i, fresh);
      else cJSON_AddItemToArray(a, fresh);
      o = fresh;
    }
    write_role(o, &c->roles[i]);
  }
  while (cJSON_GetArraySize(a) > c->role_count) cJSON_DeleteItemFromArray(a, c->role_count);
}

bool cfg_save(const char *dir, const cfg_t *c) {
  if (!tree) tree = cJSON_CreateObject();
  set_num(tree, "version", CFG_VERSION);
  cJSON *w = object(tree, "wifi");
  set_str(w, "ssid", c->wifi.ssid);
  set_str(w, "password", c->wifi.password);
  cJSON *sk = object(object(tree, "sources"), "signalk");
  set_str(sk, "host", c->sk.host);
  set_num(sk, "port", c->sk.port);
  set_str(sk, "token", c->sk.token);
  cJSON *u = object(tree, "units");
  set_str(u, "temperature", c->units.temperature);
  set_str(u, "pressure", c->units.pressure);
  set_str(u, "volume", c->units.volume);
  set_str(u, "speed", c->units.speed);
  set_str(u, "distance", c->units.distance);
  cJSON *d = object(tree, "display");
  set_str(d, "palette", c->palette);
  set_num(d, "brightness", c->brightness);
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
