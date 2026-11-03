#include "boatdata.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include <stdio.h>

typedef struct {
  char *path;
  bd_sample_t sample;
  bd_meta_t *meta;   // NULL until a source supplies one
} entry_t;

PLATFORM_EXT_BSS static entry_t entries[BD_MAX_PATHS];   // id N lives at entries[N - 1]
static uint16_t count;
static void *lock;

#define LOCK() platform_lock(lock)
#define UNLOCK() platform_unlock(lock)

static entry_t *get(bd_id_t id) { return (id >= 1 && id <= count) ? &entries[id - 1] : NULL; }

void bd_init(void) {
  if (!lock) lock = platform_lock_create();
  LOCK();
  for (uint16_t i = 0; i < count; i++) {
    free(entries[i].path);
    free(entries[i].meta);
  }
  memset(entries, 0, sizeof entries);
  count = 0;
  UNLOCK();
}

static bd_id_t find_locked(const char *path) {
  for (uint16_t i = 0; i < count; i++)
    if (strcmp(entries[i].path, path) == 0) return (bd_id_t)(i + 1);
  return BD_NONE;
}

bd_id_t bd_find(const char *path) {
  LOCK();
  bd_id_t id = find_locked(path);
  UNLOCK();
  return id;
}

bd_id_t bd_intern(const char *path) {
  LOCK();
  bd_id_t id = find_locked(path);
  if (id == BD_NONE && count >= BD_MAX_PATHS) {
    static bool warned;
    if (!warned) printf("boatdata: path table full (%d); ignoring %s and later paths\n", BD_MAX_PATHS, path);
    warned = true;
  }
  if (id == BD_NONE && count < BD_MAX_PATHS) {
    char *copy = strdup(path);
    if (copy) {
      entries[count].path = copy;
      id = ++count;
    }
  }
  UNLOCK();
  return id;
}

const char *bd_path(bd_id_t id) {
  entry_t *e = get(id);   // paths never move or change once interned
  return e ? e->path : NULL;
}

void bd_set_number(bd_id_t id, double v, uint8_t source, int64_t now_ms) {
  LOCK();
  entry_t *e = get(id);
  if (e) {
    e->sample.kind = BD_NUMBER;
    e->sample.num = v;
    e->sample.valid = isfinite(v);
    e->sample.rx_ms = now_ms;
    e->sample.source = source;
  }
  UNLOCK();
}

void bd_set_position(bd_id_t id, double lat, double lon, uint8_t source, int64_t now_ms) {
  LOCK();
  entry_t *e = get(id);
  if (e) {
    e->sample.kind = BD_POSITION;
    e->sample.pos.lat = lat;
    e->sample.pos.lon = lon;
    e->sample.valid = isfinite(lat) && isfinite(lon);
    e->sample.rx_ms = now_ms;
    e->sample.source = source;
  }
  UNLOCK();
}

void bd_set_text(bd_id_t id, const char *s, uint8_t source, int64_t now_ms) {
  LOCK();
  entry_t *e = get(id);
  if (e) {
    e->sample.kind = BD_TEXT;
    strncpy(e->sample.text, s, sizeof e->sample.text - 1);
    e->sample.text[sizeof e->sample.text - 1] = 0;
    e->sample.valid = true;
    e->sample.rx_ms = now_ms;
    e->sample.source = source;
  }
  UNLOCK();
}

void bd_set_invalid(bd_id_t id) {
  LOCK();
  entry_t *e = get(id);
  if (e) e->sample.valid = false;
  UNLOCK();
}

void bd_invalidate_source(uint8_t source) {
  LOCK();
  for (uint16_t i = 0; i < count; i++)
    if (entries[i].sample.source == source) entries[i].sample.valid = false;
  UNLOCK();
}

bool bd_get(bd_id_t id, bd_sample_t *out) {
  LOCK();
  entry_t *e = get(id);
  if (e) *out = e->sample;
  UNLOCK();
  return e != NULL;
}

bool bd_is_fresh(const bd_sample_t *s, int64_t max_age_ms, int64_t now_ms) {
  if (!s->valid) return false;
  return max_age_ms <= 0 || now_ms - s->rx_ms <= max_age_ms;
}

void bd_set_meta(bd_id_t id, const bd_meta_t *m) {
  LOCK();
  entry_t *e = get(id);
  if (e) {
    if (!e->meta) e->meta = malloc(sizeof *e->meta);
    if (e->meta) *e->meta = *m;
  }
  UNLOCK();
}

bool bd_get_meta(bd_id_t id, bd_meta_t *out) {
  LOCK();
  entry_t *e = get(id);
  bool ok = e && e->meta;
  if (ok) *out = *e->meta;
  UNLOCK();
  return ok;
}

// Segment-wise match; '*' is one whole segment.
static bool match(const char *pat, const char *path) {
  while (*pat && *path) {
    const char *pe = strchr(pat, '.'), *qe = strchr(path, '.');
    size_t pl = pe ? (size_t)(pe - pat) : strlen(pat);
    size_t ql = qe ? (size_t)(qe - path) : strlen(path);
    if (!(pl == 1 && pat[0] == '*') && (pl != ql || strncmp(pat, path, pl) != 0)) return false;
    pat += pl;
    path += ql;
    if (*pat == '.') pat++;
    else if (*pat) return false;
    if (*path == '.') path++;
    else if (*path) return false;
    if (!*pat != !*path) return false;   // one ran out of segments first
  }
  return !*pat && !*path;
}

size_t bd_match(const char *pattern, bd_id_t *out, size_t max) {
  size_t n = 0;
  LOCK();
  for (uint16_t i = 0; i < count && n < max; i++)
    if (match(pattern, entries[i].path)) out[n++] = (bd_id_t)(i + 1);
  UNLOCK();
  return n;
}

bd_id_t bd_count(void) {
  LOCK();
  bd_id_t n = count;
  UNLOCK();
  return n;
}
