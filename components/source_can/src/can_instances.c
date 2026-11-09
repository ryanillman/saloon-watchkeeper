#include "can_instances.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "boatdata.h"
#include "platform.h"
#include "cJSON.h"

// The leaves source_can writes, as boatdata patterns ('*' is one segment).
static const char *const patterns[] = {
  "propulsion.*.revolutions", "propulsion.*.boostPressure", "propulsion.*.oilPressure", "propulsion.*.oilTemperature",
  "propulsion.*.temperature", "propulsion.*.alternatorVoltage", "propulsion.*.fuel.rate", "propulsion.*.runTime",
  "electrical.batteries.*.voltage", "electrical.batteries.*.current", "electrical.batteries.*.temperature",
  "electrical.batteries.*.capacity.stateOfCharge", "electrical.batteries.*.capacity.timeRemaining",
  "tanks.*.*.currentLevel", "tanks.*.*.capacity",
};
#define NPAT (sizeof patterns / sizeof patterns[0])

// Does `path` look like `pattern`: the same segments, '*' standing for any one non-empty segment.
static bool matches(const char *pattern, const char *path) {
  while (*pattern && *path) {
    const char *pe = strchr(pattern, '.'), *se = strchr(path, '.');
    size_t pl = pe ? (size_t)(pe - pattern) : strlen(pattern), sl = se ? (size_t)(se - path) : strlen(path);
    if (pl == 1 && *pattern == '*') {
      if (sl == 0) return false;
    } else if (pl != sl || strncmp(pattern, path, pl) != 0) {
      return false;
    }
    pattern += pl;
    path += sl;
    if (*pattern == '.' && *path == '.') {
      pattern++;
      path++;
    } else {
      break;
    }
  }
  return *pattern == 0 && *path == 0;
}

// A path this cache may hold: one the bus writes, in a plain character set (it goes into a JSON file as it is).
static bool path_ok(const char *path) {
  size_t n = strlen(path);
  if (n == 0 || n >= CI_PATH_LEN) return false;
  for (size_t i = 0; i < n; i++) {
    char c = path[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
      return false;
  }
  for (size_t i = 0; i < NPAT; i++)
    if (matches(patterns[i], path)) return true;
  return false;
}

static bool has(const can_instances_t *c, const char *path) {
  for (uint16_t i = 0; i < c->count; i++)
    if (!strcmp(c->path[i], path)) return true;
  return false;
}

static bool add(can_instances_t *c, const char *path) {
  if (c->count >= CI_MAX_PATHS || has(c, path)) return false;
  snprintf(c->path[c->count++], CI_PATH_LEN, "%s", path);
  return true;
}

void can_instances_scan(can_instances_t *out) {
  out->count = 0;
  bd_id_t ids[32];
  for (size_t p = 0; p < NPAT; p++) {
    size_t n = bd_match(patterns[p], ids, 32);
    for (size_t i = 0; i < n; i++) {
      bd_sample_t s;
      if (!bd_get(ids[i], &s) || !s.valid || s.source != BD_SRC_SK) continue;
      const char *path = bd_path(ids[i]);
      if (path_ok(path)) add(out, path);
    }
  }
}

bool can_instances_update(can_instances_t *cache, const can_instances_t *live, bool settled) {
  bool changed = false;
  for (uint16_t i = 0; i < live->count; i++) changed |= add(cache, live->path[i]);
  if (settled) {
    for (uint16_t i = 0; i < cache->count;) {
      if (has(live, cache->path[i])) {
        i++;
        continue;
      }
      memmove(cache->path[i], cache->path[i + 1], (size_t)(cache->count - i - 1) * CI_PATH_LEN);
      cache->count--;
      changed = true;
    }
  }
  return changed;
}

static bool read_one(const char *file, can_instances_t *out) {
  FILE *f = fopen(file, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0 || size > 16 * 1024) {
    fclose(f);
    return false;
  }
  char *buf = malloc((size_t)size + 1);
  if (!buf) {
    fclose(f);
    return false;
  }
  size_t n = fread(buf, 1, (size_t)size, f);
  fclose(f);
  buf[n] = 0;
  cJSON *root = cJSON_Parse(buf);
  free(buf);
  const cJSON *paths = cJSON_GetObjectItem(root, "paths");
  bool ok = cJSON_IsArray(paths);
  if (ok) {
    out->count = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, paths)
      if (cJSON_IsString(it) && path_ok(it->valuestring)) add(out, it->valuestring);
  }
  cJSON_Delete(root);
  return ok;
}

bool can_instances_load(const char *dir, can_instances_t *out) {
  if (!dir) return false;
  char file[160];
  can_instances_t c;
  snprintf(file, sizeof file, "%s/instances.json", dir);
  if (!read_one(file, &c)) {
    snprintf(file, sizeof file, "%s/instances.bak.json", dir);
    if (!read_one(file, &c)) return false;
  }
  *out = c;
  return true;
}

bool can_instances_save(const char *dir, const can_instances_t *in) {
  if (!dir) return false;
  char tmp[160], main_[160], bak[160];
  snprintf(tmp, sizeof tmp, "%s/instances.tmp", dir);
  snprintf(main_, sizeof main_, "%s/instances.json", dir);
  snprintf(bak, sizeof bak, "%s/instances.bak.json", dir);
  FILE *f = fopen(tmp, "w");
  if (!f) return false;
  bool ok = fputs("{\n  \"paths\": [", f) >= 0;
  for (uint16_t i = 0; ok && i < in->count; i++) ok = fprintf(f, "%s\n    \"%s\"", i ? "," : "", in->path[i]) > 0;
  ok = ok && fputs("\n  ]\n}\n", f) >= 0;
  ok = ok && fflush(f) == 0 && fsync(fileno(f)) == 0;
  ok = fclose(f) == 0 && ok;
  if (!ok) {
    remove(tmp);
    return false;
  }
  remove(bak);   // FAT will not rename over an existing file
  if (rename(main_, bak) != 0 && errno != ENOENT) fprintf(stderr, "instances: rename %s: %s\n", main_, strerror(errno));
  if (rename(tmp, main_) != 0) {
    fprintf(stderr, "instances: save failed: rename %s: %s (the old cache is in %s)\n", tmp, strerror(errno), bak);
    return false;
  }
  return true;
}

size_t can_instances_restore(const can_instances_t *in) {
  size_t n = 0;
  for (uint16_t i = 0; i < in->count; i++)
    if (path_ok(in->path[i]) && bd_intern(in->path[i]) != BD_NONE) n++;
  return n;
}

size_t can_instances_init(can_instances_state_t *s, const char *dir) {
  memset(s, 0, sizeof *s);
  s->live_since_ms = -1;
  s->last_scan_ms = -1;
  if (!can_instances_load(dir, &s->cache)) return 0;
  return can_instances_restore(&s->cache);
}

bool can_instances_tick(can_instances_state_t *s, bool signalk_live, int64_t now_ms, const char *dir) {
  if (!signalk_live) {
    s->live_since_ms = -1;   // a disconnect restarts the settling clock
    return false;
  }
  if (s->live_since_ms < 0) s->live_since_ms = now_ms;
  if (s->last_scan_ms >= 0 && now_ms - s->last_scan_ms < CI_SCAN_MS) return false;
  s->last_scan_ms = now_ms;
  PLATFORM_EXT_BSS static can_instances_t live;   // ~4 KB: not on the UI task's stack, and not internal RAM (called from one task only)
  can_instances_scan(&live);
  bool settled = now_ms - s->live_since_ms >= CI_SETTLE_MS;
  if (!can_instances_update(&s->cache, &live, settled)) return false;
  return can_instances_save(dir, &s->cache);
}
