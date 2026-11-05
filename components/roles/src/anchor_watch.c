#include "anchor_watch.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "alerts.h"

enum { ALERT_NONE, ALERT_LOST, ALERT_NOT_RESTORED };

void anchor_watch_init(anchor_watch_t *w, const char *dir, int64_t boot_ms, int32_t lost_after_ms) {
  memset(w, 0, sizeof *w);
  w->boot_ms = boot_ms;
  w->lost_after_ms = lost_after_ms > 0 ? lost_after_ms : 60000;
  w->stale_since_ms = -1;
  w->have_sd = dir && dir[0];
  anchor_record_t r;
  if (w->have_sd && anchor_record_load(dir, &r)) {
    w->rec = r;
    w->restoring = r.armed;
  }
}

bool anchor_watch_restoring(const anchor_watch_t *w) { return w->restoring; }

static void save(anchor_watch_t *w, const char *dir, bool armed, double limit_m) {
  w->rec.armed = armed;
  if (limit_m > 0) w->rec.limit_m = limit_m;
  if (!w->have_sd || !dir || !dir[0]) return;
  if (anchor_record_save(dir, &w->rec)) w->writes++;
  else w->have_sd = false;   // do not retry every tick; the page shows NO SD
}

static void raise_alert(anchor_watch_t *w, int8_t kind, int64_t now_ms) {
  if (w->alert == kind) return;   // already up: leave its acknowledge alone
  alert_t a = {0};
  snprintf(a.id, sizeof a.id, "%s", ANCHOR_WATCH_ALERT_ID);
  a.state = AL_ALARM;
  a.audible = true;
  snprintf(a.message, sizeof a.message, "%s", kind == ALERT_LOST ? "ANCHOR WATCH LOST" : "ANCHOR WATCH NOT RESTORED");
  alerts_upsert_local(&a, now_ms);
  w->alert = kind;
}

static void clear_alert(anchor_watch_t *w) {
  if (w->alert != ALERT_NONE) alerts_clear(ANCHOR_WATCH_ALERT_ID);
  w->alert = ALERT_NONE;
}

void anchor_watch_tick(anchor_watch_t *w, const anchor_view_t *v, bool source_live, int64_t now_ms, const char *dir) {
  bool restored = v->mode == AM_HOLDING || v->mode == AM_WARN || v->mode == AM_DRAGGING;   // position, limit and distance
  bool set_fresh = restored || v->mode == AM_NO_LIMIT;
  bool none = v->mode == AM_NOT_SET && source_live;   // the server, live, says there is no anchor
  bool stale = v->mode == AM_NO_DATA;

  if (w->restoring) {
    if (!restored) {
      if (none || now_ms - w->boot_ms >= ANCHOR_RESTORE_WAIT_MS) raise_alert(w, ALERT_NOT_RESTORED, now_ms);
      return;   // otherwise keep waiting: the plugin may still be catching up after a restart
    }
    w->restoring = false;
    clear_alert(w);
  }

  if (set_fresh) {
    w->was_set = true;
    w->stale_since_ms = -1;
    if (w->alert == ALERT_LOST) clear_alert(w);
    double limit = isnan(v->limit_m) ? 0 : v->limit_m;
    if (!w->rec.armed || (limit > 0 && fabs(limit - w->rec.limit_m) > 0.5)) save(w, dir, true, limit);
  } else if (none) {   // raised from the plugin's own page, say: nothing to watch, nothing lost
    w->was_set = false;
    w->stale_since_ms = -1;
    if (w->alert == ALERT_LOST) clear_alert(w);
    if (w->rec.armed) save(w, dir, false, 0);
  } else if (stale && w->was_set) {
    if (w->stale_since_ms < 0) w->stale_since_ms = now_ms;
    else if (now_ms - w->stale_since_ms >= w->lost_after_ms) raise_alert(w, ALERT_LOST, now_ms);
  }
}

void anchor_watch_user_cleared(anchor_watch_t *w, const char *dir) {
  w->restoring = false;
  w->was_set = false;
  w->stale_since_ms = -1;
  clear_alert(w);
  if (w->rec.armed) save(w, dir, false, 0);
}
