#include "sk_parse.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alerts.h"
#include "cJSON.h"

static char self_ctx[96];   // e.g. "vessels.urn:mrn:signalk:uuid:..."
static const char *const *discovery_prefixes;   // NULL = everything

void sk_parse_set_discovery_prefixes(const char *const *p) { discovery_prefixes = p; }

static bool wanted(const char *path) {
  if (!discovery_prefixes) return true;
  for (size_t i = 0; discovery_prefixes[i]; i++) {
    size_t n = strlen(discovery_prefixes[i]);
    if (!strncmp(path, discovery_prefixes[i], n) && (path[n] == '.' || path[n] == 0)) return true;
  }
  return false;
}

void sk_parse_reset(void) { self_ctx[0] = 0; }

static void set_self(const char *ctx) { snprintf(self_ctx, sizeof self_ctx, "%s", ctx); }

static bool is_self(const cJSON *context) {
  if (!cJSON_IsString(context)) return true;   // no context means self
  const char *c = context->valuestring;
  return strcmp(c, "vessels.self") == 0 || (self_ctx[0] && strcmp(c, self_ctx) == 0);
}

static uint8_t state_of(const char *s) {
  if (!s) return SK_STATE_NORMAL;
  if (!strcmp(s, "alert")) return SK_STATE_ALERT;
  if (!strcmp(s, "warn")) return SK_STATE_WARN;
  if (!strcmp(s, "alarm")) return SK_STATE_ALARM;
  if (!strcmp(s, "emergency")) return SK_STATE_EMERGENCY;
  return SK_STATE_NORMAL;   // "normal", "nominal", anything unknown
}

static void store_meta(bd_id_t id, const cJSON *m) {
  if (!cJSON_IsObject(m)) return;
  bd_meta_t meta = {0};
  bd_get_meta(id, &meta);   // a partial meta update keeps the rest
  const cJSON *v;
  if (cJSON_IsString(v = cJSON_GetObjectItem(m, "units")))
    snprintf(meta.units, sizeof meta.units, "%s", v->valuestring);
  if (cJSON_IsString(v = cJSON_GetObjectItem(m, "displayName")))
    snprintf(meta.display_name, sizeof meta.display_name, "%s", v->valuestring);
  const cJSON *ds = cJSON_GetObjectItem(m, "displayScale");
  const cJSON *lo = cJSON_GetObjectItem(ds, "lower"), *hi = cJSON_GetObjectItem(ds, "upper");
  if (cJSON_IsNumber(lo) && cJSON_IsNumber(hi) && hi->valuedouble > lo->valuedouble) {
    meta.has_scale = true;
    meta.scale_lo = lo->valuedouble;
    meta.scale_hi = hi->valuedouble;
  }
  const cJSON *zones = cJSON_GetObjectItem(m, "zones");
  if (cJSON_IsArray(zones)) {
    meta.zone_count = 0;
    const cJSON *z;
    cJSON_ArrayForEach(z, zones) {
      if (meta.zone_count >= sizeof meta.zones / sizeof meta.zones[0]) break;
      const cJSON *zl = cJSON_GetObjectItem(z, "lower"), *zu = cJSON_GetObjectItem(z, "upper");
      const cJSON *zs = cJSON_GetObjectItem(z, "state");
      meta.zones[meta.zone_count++] = (bd_zone_t){
        .lower = cJSON_IsNumber(zl) ? zl->valuedouble : -INFINITY,
        .upper = cJSON_IsNumber(zu) ? zu->valuedouble : INFINITY,
        .state = state_of(cJSON_IsString(zs) ? zs->valuestring : NULL),
      };
    }
  }
  bd_set_meta(id, &meta);
}

#define NOTE_PREFIX "notifications."

static void store_notification(const char *path, const cJSON *v, int64_t now) {
  const char *id = path + strlen(NOTE_PREFIX);
  if (!*id) return;
  const cJSON *state = cJSON_IsObject(v) ? cJSON_GetObjectItem(v, "state") : NULL;
  if (!cJSON_IsString(state) || state_of(state->valuestring) == SK_STATE_NORMAL) {
    alerts_clear(id);
    return;
  }
  alert_t a = {0};
  snprintf(a.id, sizeof a.id, "%s", id);
  a.state = (alert_state_t)state_of(state->valuestring);
  const cJSON *m = cJSON_GetObjectItem(v, "message");
  if (cJSON_IsString(m)) snprintf(a.message, sizeof a.message, "%s", m->valuestring);
  const cJSON *method;
  cJSON_ArrayForEach(method, cJSON_GetObjectItem(v, "method"))
    if (cJSON_IsString(method) && !strcmp(method->valuestring, "sound")) a.audible = true;
  const cJSON *ref = cJSON_GetObjectItem(v, "id");
  if (cJSON_IsString(ref)) snprintf(a.ref, sizeof a.ref, "%s", ref->valuestring);
  const cJSON *st = cJSON_GetObjectItem(v, "status");
  a.acked = cJSON_IsTrue(cJSON_GetObjectItem(st, "acknowledged"));
  a.silenced = cJSON_IsTrue(cJSON_GetObjectItem(st, "silenced"));
  alerts_upsert(&a, now);
}

static void store_value(const char *path, const cJSON *v, int64_t now) {
  if (!strncmp(path, NOTE_PREFIX, strlen(NOTE_PREFIX))) {
    store_notification(path, v, now);
    return;
  }
  bd_id_t id = bd_intern(path);
  if (id == BD_NONE) return;
  if (cJSON_IsNumber(v)) {
    bd_set_number(id, v->valuedouble, SK_SOURCE_ID, now);
  } else if (cJSON_IsString(v)) {
    bd_set_text(id, v->valuestring, SK_SOURCE_ID, now);
  } else {
    const cJSON *lat = cJSON_GetObjectItem(v, "latitude"), *lon = cJSON_GetObjectItem(v, "longitude");
    if (cJSON_IsObject(v) && cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
      bd_set_position(id, lat->valuedouble, lon->valuedouble, SK_SOURCE_ID, now);
    } else {
      // null, booleans, and other objects are not readings. Mark the field
      // failed, with this source as the writer.
      bd_set_number(id, NAN, SK_SOURCE_ID, now);
    }
  }
}

void sk_parse_message(const char *json, size_t len, int64_t now) {
  cJSON *root = cJSON_ParseWithLength(json, len);
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return;
  }
  const cJSON *self = cJSON_GetObjectItem(root, "self");
  if (cJSON_IsString(self)) set_self(self->valuestring);   // server hello

  const cJSON *updates = cJSON_GetObjectItem(root, "updates");
  if (cJSON_IsArray(updates) && is_self(cJSON_GetObjectItem(root, "context"))) {
    const cJSON *u, *e;
    cJSON_ArrayForEach(u, updates) {
      cJSON_ArrayForEach(e, cJSON_GetObjectItem(u, "values")) {
        const cJSON *p = cJSON_GetObjectItem(e, "path");
        if (cJSON_IsString(p) && p->valuestring[0]) store_value(p->valuestring, cJSON_GetObjectItem(e, "value"), now);
      }
      cJSON_ArrayForEach(e, cJSON_GetObjectItem(u, "meta")) {
        const cJSON *p = cJSON_GetObjectItem(e, "path");
        if (!cJSON_IsString(p)) continue;
        bd_id_t id = bd_intern(p->valuestring);
        if (id != BD_NONE) store_meta(id, cJSON_GetObjectItem(e, "value"));
      }
    }
  }
  cJSON_Delete(root);
}

// Walks the full model. An object with a "value" member is a leaf.
static void walk(const cJSON *node, char *path, size_t len, size_t cap) {
  const cJSON *child;
  cJSON_ArrayForEach(child, node) {
    if (!cJSON_IsObject(child) || !child->string) continue;
    size_t n = (size_t)snprintf(path + len, cap - len, "%s%s", len ? "." : "", child->string);
    if (len + n >= cap) continue;
    if (len == 0 && !wanted(path)) {   // a whole top-level group no role reads
      path[len] = 0;
      continue;
    }
    if (cJSON_GetObjectItem(child, "value")) {
      bd_id_t id = bd_intern(path);
      if (id != BD_NONE) store_meta(id, cJSON_GetObjectItem(child, "meta"));
    } else {
      walk(child, path, len + n, cap);
    }
    path[len] = 0;
  }
}

bool sk_parse_full(const char *json, size_t len) {
  cJSON *root = cJSON_ParseWithLength(json, len);
  bool ok = cJSON_IsObject(root);
  if (ok) {
    const cJSON *uuid = cJSON_GetObjectItem(root, "uuid"), *mmsi = cJSON_GetObjectItem(root, "mmsi");
    char ctx[96];
    if (cJSON_IsString(uuid)) {
      snprintf(ctx, sizeof ctx, "vessels.%s", uuid->valuestring);
      set_self(ctx);
    } else if (cJSON_IsString(mmsi)) {
      snprintf(ctx, sizeof ctx, "vessels.urn:mrn:imo:mmsi:%s", mmsi->valuestring);
      set_self(ctx);
    }
    char path[160] = "";
    walk(root, path, 0, sizeof path);
  }
  cJSON_Delete(root);
  return ok;
}

char *sk_build_subscribe(const sk_sub_t *subs, size_t n) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "context", "vessels.self");
  cJSON *arr = cJSON_AddArrayToObject(root, "subscribe");
  for (size_t i = 0; i < n; i++) {
    const char *p = bd_path(subs[i].id);
    if (!p) continue;
    cJSON *s = cJSON_CreateObject();
    cJSON_AddStringToObject(s, "path", p);
    if (subs[i].resend_ms) {
      cJSON_AddStringToObject(s, "policy", "fixed");
      cJSON_AddNumberToObject(s, "period", subs[i].resend_ms);
    } else {
      cJSON_AddStringToObject(s, "policy", "instant");
    }
    cJSON_AddItemToArray(arr, s);
  }
  cJSON *s = cJSON_CreateObject();
  cJSON_AddStringToObject(s, "path", "notifications.*");
  cJSON_AddStringToObject(s, "policy", "instant");
  cJSON_AddItemToArray(arr, s);
  char *out = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return out;
}

bool sk_build_alert_request(const alert_t *a, alert_req_t req, char *path_out, size_t path_n) {
  const char *verb = req == AL_REQ_ACK ? "acknowledge" : req == AL_REQ_SILENCE ? "silence" : NULL;
  if (!a || !a->ref[0] || !verb) return false;
  int n = snprintf(path_out, path_n, "/signalk/v2/api/notifications/%s/%s", a->ref, verb);
  return n > 0 && (size_t)n < path_n;
}
