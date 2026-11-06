#include "boatdata.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include <stdio.h>

typedef struct {
  const char *path;   // points into path_store
  bd_sample_t sample;
  bool has_meta;      // false until a source supplies one
} entry_t;

// Paths and metadata live in fixed PSRAM tables: per-path malloc would take about 270 B of internal RAM
// each, which a boat with a Cerbo, engines and tanks (100-200 paths) turns into all of lcd7's headroom.
#define PATH_STORE_BYTES (48 * 1024)   // 512 paths of up to ~90 characters
PLATFORM_EXT_BSS static entry_t entries[BD_MAX_PATHS];   // id N lives at entries[N - 1]
PLATFORM_EXT_BSS static bd_meta_t metas[BD_MAX_PATHS];   // same index as entries
PLATFORM_EXT_BSS static char path_store[PATH_STORE_BYTES];
static size_t path_used;
static uint16_t count;
static int64_t newest_src_s;   // largest source timestamp set (bd_newest_source_time)
static void *lock;

static struct { char name[12]; uint8_t rank; bd_src_state_t state; } sources[BD_MAX_SOURCES];
static int32_t hold_ms = BD_HOLD_MS_DEFAULT;

#define LOCK() platform_lock(lock)
#define UNLOCK() platform_unlock(lock)

static entry_t *get(bd_id_t id) { return (id >= 1 && id <= count) ? &entries[id - 1] : NULL; }

void bd_init(void) {
  if (!lock) lock = platform_lock_create();
  LOCK();
  memset(entries, 0, sizeof entries);
  count = 0;
  path_used = 0;
  newest_src_s = 0;
  memset(sources, 0, sizeof sources);
  hold_ms = BD_HOLD_MS_DEFAULT;
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
    size_t n = strlen(path) + 1;
    if (n <= PATH_STORE_BYTES - path_used) {
      char *copy = memcpy(path_store + path_used, path, n);
      path_used += n;
      entries[count].path = copy;
      id = ++count;
    } else {
      static bool warned_store;
      if (!warned_store) printf("boatdata: path storage full (%d B); ignoring %s and later paths\n", PATH_STORE_BYTES, path);
      warned_store = true;
    }
  }
  UNLOCK();
  return id;
}

const char *bd_path(bd_id_t id) {
  entry_t *e = get(id);   // paths never move or change once interned
  return e ? e->path : NULL;
}

static uint8_t rank_of(uint8_t source) { return source < BD_MAX_SOURCES ? sources[source].rank : 0; }

// Whether `source` may overwrite the entry now. A weak write (a value that is not a reading, or a replayed one)
// never displaces a fresh sample from a lower-ranked source: a null from Signal K must not hide a working CAN sensor.
static bool admit_locked(const entry_t *e, uint8_t source, bool weak, int64_t now_ms) {
  const bd_sample_t *cur = &e->sample;
  if (!cur->valid || cur->source == source) return true;
  if (now_ms - cur->rx_ms > hold_ms) return true;
  uint8_t rs = rank_of(source), rc = rank_of(cur->source);
  return rs == rc || (rs > rc && !weak);
}

static bool replay_locked(int64_t src_s) { return src_s > 0 && newest_src_s > 0 && newest_src_s - src_s > BD_REPLAY_S; }

bool bd_admit(bd_id_t id, uint8_t source, int64_t src_s, int64_t now_ms) {
  LOCK();
  entry_t *e = get(id);
  bool ok = e && admit_locked(e, source, replay_locked(src_s), now_ms);
  UNLOCK();
  return ok;
}

void bd_source_register(uint8_t source, const char *name, uint8_t rank) {
  if (source >= BD_MAX_SOURCES) return;
  LOCK();
  snprintf(sources[source].name, sizeof sources[source].name, "%s", name ? name : "");
  sources[source].rank = rank;
  if (sources[source].state == BD_SRC_NOT_CONFIGURED) sources[source].state = BD_SRC_OFFLINE;
  UNLOCK();
}

void bd_source_set_state(uint8_t source, bd_src_state_t st) {
  if (source >= BD_MAX_SOURCES) return;
  LOCK();
  sources[source].state = st;
  UNLOCK();
}

bd_src_state_t bd_source_state(uint8_t source) {
  if (source >= BD_MAX_SOURCES) return BD_SRC_NOT_CONFIGURED;
  LOCK();
  bd_src_state_t st = sources[source].state;
  UNLOCK();
  return st;
}

const char *bd_source_name(uint8_t source) { return source < BD_MAX_SOURCES ? sources[source].name : ""; }

void bd_set_hold_ms(int32_t ms) {
  LOCK();
  hold_ms = ms > 0 ? ms : BD_HOLD_MS_DEFAULT;
  UNLOCK();
}

void bd_set_number(bd_id_t id, double v, uint8_t source, int64_t now_ms) {
  LOCK();
  entry_t *e = get(id);
  if (e && admit_locked(e, source, !isfinite(v), now_ms)) {
    e->sample.kind = BD_NUMBER;
    e->sample.num = v;
    e->sample.valid = isfinite(v);
    e->sample.rx_ms = now_ms;
    e->sample.source = source;
    e->sample.src_s = 0;
    e->sample.invalidated = false;
  }
  UNLOCK();
}

void bd_set_position(bd_id_t id, double lat, double lon, uint8_t source, int64_t now_ms) {
  LOCK();
  entry_t *e = get(id);
  if (e && admit_locked(e, source, !(isfinite(lat) && isfinite(lon)), now_ms)) {
    e->sample.kind = BD_POSITION;
    e->sample.pos.lat = lat;
    e->sample.pos.lon = lon;
    e->sample.valid = isfinite(lat) && isfinite(lon);
    e->sample.rx_ms = now_ms;
    e->sample.source = source;
    e->sample.src_s = 0;
    e->sample.invalidated = false;
  }
  UNLOCK();
}

void bd_set_text(bd_id_t id, const char *s, uint8_t source, int64_t now_ms) {
  LOCK();
  entry_t *e = get(id);
  if (e && admit_locked(e, source, false, now_ms)) {
    e->sample.kind = BD_TEXT;
    strncpy(e->sample.text, s, sizeof e->sample.text - 1);
    e->sample.text[sizeof e->sample.text - 1] = 0;
    e->sample.valid = true;
    e->sample.rx_ms = now_ms;
    e->sample.source = source;
    e->sample.src_s = 0;
    e->sample.invalidated = false;
  }
  UNLOCK();
}

int64_t bd_newest_source_time(void) {
  LOCK();
  int64_t t = newest_src_s;
  UNLOCK();
  return t;
}

void bd_set_source_time(bd_id_t id, int64_t epoch_s) {
  LOCK();
  entry_t *e = get(id);
  if (e) e->sample.src_s = epoch_s;
  if (e && epoch_s > newest_src_s) newest_src_s = epoch_s;
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
    if (entries[i].sample.source == source && entries[i].sample.valid) {
      entries[i].sample.valid = false;
      entries[i].sample.invalidated = true;
    }
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
    metas[id - 1] = *m;
    e->has_meta = true;
  }
  UNLOCK();
}

bool bd_get_meta(bd_id_t id, bd_meta_t *out) {
  LOCK();
  entry_t *e = get(id);
  bool ok = e && e->has_meta;
  if (ok) *out = metas[id - 1];
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
