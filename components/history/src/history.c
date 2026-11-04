#include "history.h"
#include <math.h>
#include <string.h>
#include "platform.h"

#define FRESH_MS 90000   // a value older than this is not sampled

typedef struct {
  bd_id_t id;
  float v[HIST_MINUTES];                 // indexed by minute % HIST_MINUTES
  uint8_t live[(HIST_MINUTES + 7) / 8];  // 1 = stored by hist_tick, which backfill never overwrites
} slot_t;

// 16 paths x 2.9 KB: PSRAM on the boards.
PLATFORM_EXT_BSS static struct {
  void *lock;
  slot_t s[HIST_MAX_PATHS];
  uint8_t n;
  bool started;
  int64_t cur, first;   // the newest minute recorded, and the oldest that may hold data
} h;

#define LOCK() platform_lock(h.lock)
#define UNLOCK() platform_unlock(h.lock)

static size_t at(int64_t minute) { return (size_t)(((minute % HIST_MINUTES) + HIST_MINUTES) % HIST_MINUTES); }
static bool live_bit(const slot_t *p, size_t i) { return p->live[i / 8] >> (i % 8) & 1; }
static void set_live(slot_t *p, size_t i, bool on) {
  if (on) p->live[i / 8] |= (uint8_t)(1u << (i % 8));
  else p->live[i / 8] &= (uint8_t)~(1u << (i % 8));
}

static slot_t *find(bd_id_t id) {
  for (uint8_t i = 0; i < h.n; i++)
    if (h.s[i].id == id) return &h.s[i];
  return NULL;
}

static void clear_slot(slot_t *p) {
  for (size_t i = 0; i < HIST_MINUTES; i++) p->v[i] = NAN;
  memset(p->live, 0, sizeof p->live);
}

// The oldest minute that can hold data, given the ring's depth.
static int64_t floor_minute(void) { return h.first > h.cur - (HIST_MINUTES - 1) ? h.first : h.cur - (HIST_MINUTES - 1); }

void hist_init(void) {
  void *lock = h.lock ? h.lock : platform_lock_create();
  memset(&h, 0, sizeof h);
  h.lock = lock;
}

bool hist_track(bd_id_t id) {
  LOCK();
  bool ok = true;
  if (!find(id)) {
    if (h.n >= HIST_MAX_PATHS) ok = false;
    else {
      slot_t *p = &h.s[h.n++];
      clear_slot(p);
      p->id = id;
    }
  }
  UNLOCK();
  return ok;
}

size_t hist_tracked(bd_id_t *out, size_t max) {
  LOCK();
  size_t n = h.n < max ? h.n : max;
  for (size_t i = 0; i < n; i++) out[i] = h.s[i].id;
  UNLOCK();
  return n;
}

static void start_record(int64_t cur, int64_t first) {
  h.started = true;
  h.cur = cur;
  h.first = first;
}

void hist_tick(int64_t now_ms) {
  int64_t m = now_ms / 60000;
  LOCK();
  if (!h.started) start_record(m - 1, m);   // the loop below records minute m, the first
  if (m > h.cur) {
    int64_t from = h.cur + 1 > m - (HIST_MINUTES - 1) ? h.cur + 1 : m - (HIST_MINUTES - 1);
    for (int64_t mm = from; mm <= m; mm++) {   // every minute since the last call; only m is sampled
      size_t i = at(mm);
      for (uint8_t k = 0; k < h.n; k++) {
        slot_t *p = &h.s[k];
        p->v[i] = NAN;
        set_live(p, i, false);
        if (mm != m) continue;
        bd_sample_t smp;
        if (bd_get(p->id, &smp) && smp.kind == BD_NUMBER && bd_is_fresh(&smp, FRESH_MS, now_ms) && isfinite(smp.num)) {
          p->v[i] = (float)smp.num;
          set_live(p, i, true);
        }
      }
    }
    h.cur = m;
  }
  UNLOCK();
}

void hist_backfill(bd_id_t id, const float *vals, size_t n) {
  LOCK();
  slot_t *p = find(id);
  if (p && n) {
    if (!h.started) start_record(platform_millis() / 60000, platform_millis() / 60000);
    for (size_t k = 0; k < n; k++) {
      int64_t minute = h.cur - (int64_t)(n - 1 - k);
      if (minute < h.cur - (HIST_MINUTES - 1)) continue;   // older than the ring
      size_t i = at(minute);
      if (isnan(vals[k]) || live_bit(p, i) || !isnan(p->v[i])) continue;
      p->v[i] = vals[k];
    }
    int64_t oldest = h.cur - (int64_t)(n - 1);
    if (oldest < h.first) h.first = oldest;
  }
  UNLOCK();
}

size_t hist_read(bd_id_t id, size_t minutes, float *out) {
  LOCK();
  slot_t *p = find(id);
  size_t n = 0;
  if (p && h.started) {
    size_t span = (size_t)(h.cur - floor_minute() + 1);
    n = minutes < span ? minutes : span;
    for (size_t k = 0; k < n; k++) out[k] = p->v[at(h.cur - (int64_t)(n - 1 - k))];
  }
  UNLOCK();
  return n;
}

bool hist_change(bd_id_t id, size_t minutes, float *delta) {
  PLATFORM_EXT_BSS static float w[HIST_MINUTES];   // 2.9 KB off the stack; only the UI task reads
  size_t want = minutes + 1 > HIST_MINUTES ? HIST_MINUTES : minutes + 1;
  size_t n = hist_read(id, want, w);
  float first = NAN, last = NAN;
  size_t valid = 0;
  for (size_t k = 0; k < n; k++) {
    if (isnan(w[k])) continue;
    if (!valid) first = w[k];
    last = w[k];
    valid++;
  }
  if (valid < 2) return false;
  *delta = last - first;
  return true;
}
