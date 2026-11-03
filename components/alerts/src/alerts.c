#include "alerts.h"
#include <stdio.h>
#include <string.h>
#include "platform.h"

#define QUEUE_MAX 8

typedef struct {
  alert_t a;                       // a.acked / a.silenced hold the merged (shown) values
  bool l_ack, l_sil, s_ack, s_sil; // local and source-reported flags
} entry_t;
typedef struct { char id[64]; alert_req_t req; } req_t;

static void *lock;
static entry_t list[ALERTS_MAX];
static uint8_t count;
static bool source_up;
static req_t queue[QUEUE_MAX];
static uint8_t qhead, qcount;

#define LOCK() platform_lock(lock)
#define UNLOCK() platform_unlock(lock)

static void ensure_lock(void) {
  if (!lock) lock = platform_lock_create();
}

void alerts_init(void) {
  ensure_lock();
  LOCK();
  count = 0;
  qhead = qcount = 0;
  source_up = false;
  UNLOCK();
}

static int find(const char *id) {
  for (int i = 0; i < count; i++)
    if (strcmp(list[i].a.id, id) == 0) return i;
  return -1;
}
static void remove_at(int i) {
  list[i] = list[--count];
}
static void merge(entry_t *e) {
  e->a.acked = e->l_ack || e->s_ack;
  e->a.silenced = e->l_sil || e->s_sil;
}

void alerts_upsert(const alert_t *in, int64_t now_ms) {
  ensure_lock();
  LOCK();
  int i = find(in->id);
  if (in->state == AL_NORMAL) {
    if (i >= 0) remove_at(i);
    UNLOCK();
    return;
  }
  if (i < 0) {
    if (count == ALERTS_MAX) {
      // Drop the least severe (oldest among equals) if the newcomer is more severe.
      int v = 0;
      for (int k = 1; k < count; k++)
        if (list[k].a.state < list[v].a.state ||
            (list[k].a.state == list[v].a.state && list[k].a.since_ms < list[v].a.since_ms))
          v = k;
      if (in->state <= list[v].a.state) { UNLOCK(); return; }
      remove_at(v);
    }
    i = count++;
    memset(&list[i], 0, sizeof list[i]);
    list[i].a.since_ms = now_ms;
  } else if (in->state > list[i].a.state) {
    list[i].l_ack = list[i].l_sil = false;
    list[i].a.since_ms = now_ms;
    list[i].a.send_failed = false;
  }
  entry_t *e = &list[i];
  snprintf(e->a.id, sizeof e->a.id, "%s", in->id);
  e->a.state = in->state;
  snprintf(e->a.message, sizeof e->a.message, "%s", in->message);
  snprintf(e->a.ref, sizeof e->a.ref, "%s", in->ref);
  e->a.audible = in->audible;
  e->s_ack = in->acked;
  e->s_sil = in->silenced;
  merge(e);
  UNLOCK();
}

void alerts_clear(const char *id) {
  ensure_lock();
  LOCK();
  int i = find(id);
  if (i >= 0) remove_at(i);
  UNLOCK();
}

void alerts_source_up(bool up) {
  ensure_lock();
  LOCK();
  source_up = up;
  if (!up) {
    count = 0;
    qhead = qcount = 0;
  }
  UNLOCK();
}

void alerts_snapshot(alerts_snapshot_t *out) {
  ensure_lock();
  LOCK();
  out->count = count;
  out->source_up = source_up;
  for (int i = 0; i < count; i++) out->a[i] = list[i].a;
  UNLOCK();
  for (int i = 1; i < out->count; i++) {   // insertion sort: severity desc, then newest first
    alert_t t = out->a[i];
    int j = i - 1;
    while (j >= 0 && (out->a[j].state < t.state ||
                      (out->a[j].state == t.state && out->a[j].since_ms < t.since_ms))) {
      out->a[j + 1] = out->a[j];
      j--;
    }
    out->a[j + 1] = t;
  }
}

bool alerts_get(const char *id, alert_t *out) {
  ensure_lock();
  LOCK();
  int i = find(id);
  if (i >= 0) *out = list[i].a;
  UNLOCK();
  return i >= 0;
}

alert_state_t alerts_for_path(const char *path) {
  ensure_lock();
  LOCK();
  int i = find(path);
  alert_state_t s = i >= 0 ? list[i].a.state : AL_NORMAL;
  UNLOCK();
  return s;
}

void alerts_request(const char *id, alert_req_t req) {
  if (req == AL_REQ_NONE) return;
  ensure_lock();
  LOCK();
  int i = find(id);
  if (i < 0) { UNLOCK(); return; }
  entry_t *e = &list[i];
  if (req == AL_REQ_ACK) e->l_ack = true;
  else e->l_sil = true;
  merge(e);
  if (!source_up || e->a.ref[0] == '\0') {
    e->a.send_failed = true;
    UNLOCK();
    return;
  }
  e->a.send_failed = false;
  if (qcount == QUEUE_MAX) {
    int d = find(queue[qhead].id);
    if (d >= 0) list[d].a.send_failed = true;
    qhead = (qhead + 1) % QUEUE_MAX;
    qcount--;
  }
  req_t *q = &queue[(qhead + qcount) % QUEUE_MAX];
  snprintf(q->id, sizeof q->id, "%s", id);
  q->req = req;
  qcount++;
  UNLOCK();
}

bool alerts_next_request(char id[64], alert_req_t *req) {
  ensure_lock();
  LOCK();
  bool have = qcount > 0;
  if (have) {
    snprintf(id, 64, "%s", queue[qhead].id);
    *req = queue[qhead].req;
    qhead = (qhead + 1) % QUEUE_MAX;
    qcount--;
  }
  UNLOCK();
  return have;
}

void alerts_request_result(const char *id, bool ok) {
  ensure_lock();
  LOCK();
  int i = find(id);
  if (i >= 0) list[i].a.send_failed = !ok;
  UNLOCK();
}

bool alerts_buzzer_on(const alerts_snapshot_t *s, bool enabled, int64_t now_ms) {
  if (!enabled) return false;
  alert_state_t worst = AL_NORMAL;
  for (int i = 0; i < s->count; i++) {
    const alert_t *a = &s->a[i];
    if (a->audible && !a->acked && !a->silenced && a->state >= AL_ALARM && a->state > worst) worst = a->state;
  }
  if (worst == AL_NORMAL) return false;
  if (now_ms < 0) now_ms = 0;
  if (worst == AL_EMERGENCY) return now_ms % 300 < 150;
  return now_ms % 1000 < 200;
}
