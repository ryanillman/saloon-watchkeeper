#include "sk_electrical.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "cJSON.h"
#include "sk_parse.h"

#define FEED_SK 1

static el_t *res;

static uint32_t ref_of(const char *source) {
  uint32_t h = 2166136261u;
  for (; *source; source++) h = (h ^ (uint8_t)*source) * 16777619u;
  return h;
}

// An instance segment: a number when it is one, else a hash of its text (ids such as "house").
static uint8_t inst_of(const char *seg, size_t n) {
  unsigned v = 0;
  size_t i = 0;
  for (; i < n && isdigit((unsigned char)seg[i]) && v < 1000; i++) v = v * 10 + (unsigned)(seg[i] - '0');
  if (n && i == n && v < 256) return (uint8_t)v;
  uint32_t h = 2166136261u;
  for (i = 0; i < n; i++) h = (h ^ (uint8_t)seg[i]) * 16777619u;
  return (uint8_t)(h ^ h >> 8 ^ h >> 16);
}

// Takes "<segment>." off the front of *p. False when there is no further segment.
static bool segment(const char **p, const char **seg, size_t *n) {
  const char *dot = strchr(*p, '.');
  if (!dot) return false;
  *seg = *p;
  *n = (size_t)(dot - *p);
  *p = dot + 1;
  return *n > 0;
}

static bool in_range(el_field_t f, double v) {
  switch (f) {
    case EL_F_SOC: return v >= 0 && v <= 1;
    case EL_F_VOLTAGE: return v >= -5 && v <= 80;
    case EL_F_CURRENT: case EL_F_AC_CURRENT: return fabs(v) <= 5000;
    default: return true;
  }
}

static int state_code(const char *s) {
  static const struct { const char *prefix; int code; } t[] = {
    {"off", 0}, {"low", 1}, {"fault", 2}, {"bulk", 3}, {"absorption", 4}, {"float", 5},
    {"storage", 6}, {"equal", 7}, {"pass", 8}, {"invert", 9}, {"assist", 10},
  };
  for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
    if (!strncasecmp(s, t[i].prefix, strlen(t[i].prefix))) return t[i].code;
  return -1;
}

static void observe(el_ref_t ref, uint8_t inst, el_field_t f, double v, int64_t now) {
  if (in_range(f, v)) el_observe(res, FEED_SK, ref, inst, f, v, now);
}
static void observe_path(el_ref_t ref, uint8_t inst, el_fn_t fn, el_field_t f, double v, int64_t now) {
  if (in_range(f, v)) el_observe_path(res, FEED_SK, ref, inst, fn, f, v, now);
}

static bool leaf_field(const char *leaf, el_field_t *f) {
  if (!strcmp(leaf, "voltage")) *f = EL_F_VOLTAGE;
  else if (!strcmp(leaf, "current")) *f = EL_F_CURRENT;
  else if (!strcmp(leaf, "capacity.stateOfCharge")) *f = EL_F_SOC;
  else if (!strcmp(leaf, "capacity.timeRemaining")) *f = EL_F_TIME_REMAINING;
  else return false;
  return true;
}

static void on_observe(const char *path, double num, const char *text, const char *source, int64_t now, void *ctx) {
  (void)ctx;
  if (!res || strncmp(path, "electrical.", 11)) return;
  const char *p = path + 11, *seg, *seg2;
  size_t n, n2;
  el_ref_t ref = ref_of(source);
  el_field_t f;
  if (!strncmp(p, "batteries.", 10) || !strncmp(p, "solar.", 6) || !strncmp(p, "chargers.", 9) || !strncmp(p, "alternators.", 12)) {
    int kind = p[0] == 'b' ? 0 : p[0] == 's' ? 1 : p[0] == 'c' ? 2 : 3;
    p = strchr(p, '.') + 1;
    if (!segment(&p, &seg, &n)) return;
    uint8_t inst = inst_of(seg, n);
    el_fn_t fn = kind == 1 ? EL_FN_PV : kind == 3 ? EL_FN_ALTERNATOR : EL_FN_CHARGER;
    if (!strcmp(p, "chargingMode") && kind != 0 && text) {   // an adopted charger, panel or alternator names its stage as text
      int code = state_code(text);
      if (code >= 0) observe_path(ref, inst, fn, EL_F_CONV_STATE, code, now);
    } else if (text) {
      return;
    } else if (kind == 0 && !strcmp(p, "power")) {
      observe(ref, inst, EL_F_POWER, num, now);
    } else if (kind == 0 && leaf_field(p, &f)) {
      observe(ref, inst, f, num, now);
    } else if (kind == 1 && !strcmp(p, "panelPower")) {
      observe_path(ref, inst, fn, EL_F_POWER, num, now);
    } else if (kind != 0 && leaf_field(p, &f) && (f == EL_F_VOLTAGE || f == EL_F_CURRENT)) {
      observe_path(ref, inst, fn, f, num, now);
    } else {
      return;
    }
  } else if (!strncmp(p, "converter.", 10)) {
    p += 10;
    if (!segment(&p, &seg, &n) || !segment(&p, &seg2, &n2) || strcmp(p, "operatingState") || !text) return;
    int code = state_code(text);
    if (code >= 0) observe(ref, inst_of(seg2, n2), EL_F_CONV_STATE, code, now);
  } else if (!strncmp(p, "ac.", 3)) {
    p += 3;
    if (!segment(&p, &seg, &n) || !segment(&p, &seg2, &n2) || strcmp(p, "phase.A.current") || text) return;
    observe(ref, inst_of(seg2, n2), EL_F_AC_CURRENT, num, now);
  } else if (!strncmp(p, "switches.bank.", 14)) {
    p += 14;
    if (!segment(&p, &seg, &n) || !segment(&p, &seg2, &n2) || strcmp(p, "state") || text) return;
    observe(ref, inst_of(seg2, n2), EL_F_SWITCH, num, now);   // the channel is the instance; banks are not told apart
  } else {
    return;
  }
  el_publish(res, now);
}

void sk_electrical_attach(el_t *e) {
  res = e;
  sk_set_observe_cb(e ? on_observe : NULL, NULL);
}

bool sk_electrical_load_sources(const char *json, size_t len) {
  cJSON *root = cJSON_ParseWithLength(json, len);
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return false;
  }
  if (res) {
    const cJSON *bus, *dev;
    cJSON_ArrayForEach(bus, root) {
      if (!cJSON_IsObject(bus)) continue;
      cJSON_ArrayForEach(dev, bus) {
        const cJSON *n2k = cJSON_GetObjectItem(dev, "n2k");
        if (!cJSON_IsObject(n2k)) continue;
        char source[64];
        snprintf(source, sizeof source, "%s.%s", bus->string, dev->string);
        el_identity_t id = {0};
        const cJSON *name = cJSON_GetObjectItem(n2k, "canName"), *model = cJSON_GetObjectItem(n2k, "modelId"),
                    *serial = cJSON_GetObjectItem(n2k, "modelSerialCode");
        if (cJSON_IsString(name)) id.can_name = strtoull(name->valuestring, NULL, 16);
        if (cJSON_IsString(model)) snprintf(id.model_id, sizeof id.model_id, "%s", model->valuestring);
        if (cJSON_IsString(serial)) snprintf(id.serial, sizeof id.serial, "%s", serial->valuestring);
        el_set_identity(res, FEED_SK, ref_of(source), &id);
        if (id.can_name) {   // newer servers stamp "$source" as <bus>.<canName> once a device's NAME is known
          snprintf(source, sizeof source, "%s.%016llx", bus->string, (unsigned long long)id.can_name);
          el_set_identity(res, FEED_SK, ref_of(source), &id);
        }
      }
    }
  }
  cJSON_Delete(root);
  return true;
}

void sk_electrical_feed_down(void) {
  if (res) el_feed_down(res, FEED_SK);
}
