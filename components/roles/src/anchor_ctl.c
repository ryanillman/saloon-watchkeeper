#include "anchor_ctl.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "alerts.h"

#define HOLD_MS 1500
#define STAGE_MS 1500
#define CONFIRM_MS 10000
#define MESSAGE_MS 5000
#define DEFAULT_RADIUS_M 30
#define MIN_RADIUS_M 5
#define MAX_RADIUS_M 300

enum { CMD_DROP, CMD_RAISE, CMD_RADIUS, CMD_N };
typedef struct {
  bool active;           // sent and not settled yet
  uint16_t handle;
  bool ok_seen;          // the server accepted it
  int64_t ok_at_ms;
  double value_m;        // radius commands: what was asked for
} pending_t;

static struct {
  const anchor_ops_t *ops;
  anchor_plugin_t plugin;
  anchor_watch_t *watch;
  const char *dir;
  const cfg_units_t *units;
  anchor_ids_t ids;
  bool ids_bound;
  double staged_m, default_m;
  bool staged, staged_sent;
  int64_t staged_at_ms;
  bool hold, hold_sent;
  int64_t hold_start_ms;
  bool press_valid;      // a press on the main button is in progress, so the tap that ends it is judged by what it was at press time
  anchor_btn_t press_btn;
  pending_t pend[CMD_N];
  char msg[16];
  int64_t msg_until_ms;
  anchor_ctl_state_t st;
} c;

static bool feet(void) { return c.units && !strcmp(c.units->depth, "ft"); }
static double step_m(void) { return feet() ? 15 * 0.3048 : 5; }
static bool is_set(anchor_mode_t m) { return m == AM_HOLDING || m == AM_WARN || m == AM_DRAGGING || m == AM_NO_LIMIT; }

void anchor_ctl_setup(const anchor_ops_t *ops, anchor_plugin_t plugin, anchor_watch_t *watch, const char *dir,
                      const cfg_units_t *units) {
  memset(&c, 0, sizeof c);
  c.ops = ops;
  c.plugin = plugin;
  c.watch = watch;
  c.dir = dir;
  c.units = units;
  c.default_m = DEFAULT_RADIUS_M;
  anchor_ids_bind(&c.ids);
  c.ids_bound = true;
  c.st.message = c.msg;
}

void anchor_build_view(int64_t now_ms, anchor_view_t *v, anchor_pt_t *track, size_t track_cap) {
  if (!c.ids_bound) {
    anchor_ids_bind(&c.ids);
    c.ids_bound = true;
  }
  anchor_view_build(&c.ids, c.watch && anchor_watch_restoring(c.watch), alerts_source_is_up(), now_ms, v, track, track_cap);
}

static void say(const char *text, int64_t now_ms) {
  snprintf(c.msg, sizeof c.msg, "%s", text);
  c.msg_until_ms = now_ms + MESSAGE_MS;
}

static const char *failure_label(int res) {
  switch (res) {
    case ANCHOR_RES_NO_ACCESS: return "NO ACCESS";
    case ANCHOR_RES_NO_PLUGIN: return "NO PLUGIN";
    case ANCHOR_RES_REJECTED: return "REJECTED";
    default: return "OFFLINE";
  }
}

// Sends a command and tracks it. A refused submit (nothing queued while disconnected) is OFFLINE at once.
static bool send(int slot, anchor_cmd_kind_t kind, double radius_m, bool replaces, int64_t now_ms) {
  anchor_cmd_t cmd;
  anchor_build_cmd(c.plugin, kind, radius_m, &cmd);
  uint16_t h = c.ops ? c.ops->submit(&cmd, replaces) : 0;
  if (!h) {
    say("OFFLINE", now_ms);
    return false;
  }
  c.pend[slot] = (pending_t){.active = true, .handle = h, .value_m = radius_m};
  return true;
}

static void poll_results(int64_t now_ms) {
  for (int k = 0; k < CMD_N; k++) {
    pending_t *p = &c.pend[k];
    if (!p->active || p->ok_seen) continue;
    int r = c.ops->result(p->handle);
    if (r == ANCHOR_RES_PENDING) continue;
    if (r == ANCHOR_RES_OK) {
      p->ok_seen = true;
      p->ok_at_ms = now_ms;
      continue;
    }
    say(failure_label(r), now_ms);
    p->active = false;
    if (k == CMD_RADIUS) c.staged = false;   // back to what the server has
  }
}

// Accepted by the server: wait for the plugin to show it, up to 10 s.
static void check_confirmed(const anchor_view_t *v, int64_t now_ms) {
  for (int k = 0; k < CMD_N; k++) {
    pending_t *p = &c.pend[k];
    if (!p->active || !p->ok_seen) continue;
    bool seen = k == CMD_DROP ? is_set(v->mode)
              : k == CMD_RAISE ? v->mode == AM_NOT_SET
              : is_set(v->mode) && !isnan(v->limit_m) && fabs(v->limit_m - p->value_m) < 0.5;
    if (!seen && now_ms < p->ok_at_ms + CONFIRM_MS) continue;
    if (!seen) say("NOT CONFIRMED", now_ms);
    p->active = false;
    if (k == CMD_RADIUS && c.staged_sent) c.staged = false;
  }
}

void anchor_ctl_update(int64_t now_ms, anchor_pt_t *track, size_t track_cap) {
  c.st.message = c.msg;   // safe before anchor_ctl_setup too (the simulator draws the page without commands)
  if (c.default_m <= 0) c.default_m = DEFAULT_RADIUS_M;
  anchor_view_t *v = &c.st.view;
  anchor_build_view(now_ms, v, track, track_cap);
  if (is_set(v->mode) && !isnan(v->limit_m)) c.default_m = v->limit_m;

  poll_results(now_ms);
  check_confirmed(v, now_ms);

  bool main_ok = v->mode != AM_NO_DATA;
  if (c.hold && !c.hold_sent && now_ms - c.hold_start_ms >= HOLD_MS) {
    c.hold_sent = true;
    if (v->mode == AM_RESTORING && c.watch) anchor_watch_user_cleared(c.watch, c.dir);   // a stuck record must clear
    if (send(CMD_RAISE, ANCHOR_CMD_RAISE, 0, false, now_ms)) c.staged = false;
  }
  if (c.staged && !c.staged_sent && is_set(v->mode) && v->circle && now_ms - c.staged_at_ms >= STAGE_MS) {
    c.staged_sent = true;
    if (!send(CMD_RADIUS, ANCHOR_CMD_RADIUS, c.staged_m, true, now_ms)) c.staged = false;
  }
  if (c.msg[0] && now_ms >= c.msg_until_ms) c.msg[0] = 0;

  c.st.button = v->mode == AM_NO_DATA ? ANCHOR_BTN_NONE : v->mode == AM_NOT_SET ? ANCHOR_BTN_DROP : ANCHOR_BTN_RAISE;
  c.st.main_enabled = main_ok;
  c.st.radius_enabled = v->circle && v->mode != AM_NO_DATA && v->mode != AM_RESTORING;
  c.st.limit_staged = c.staged;
  c.st.shown_limit_m = c.staged ? c.staged_m : is_set(v->mode) && !isnan(v->limit_m) ? v->limit_m : c.default_m;
  c.st.hold_pct = !c.hold ? 0 : c.hold_sent ? 100 : (int)((now_ms - c.hold_start_ms) * 100 / HOLD_MS);
  if (c.st.hold_pct > 100) c.st.hold_pct = 100;
  c.st.no_sd = c.watch && !c.watch->have_sd;
}

const anchor_ctl_state_t *anchor_ctl_state(void) {
  c.st.message = c.msg;
  return &c.st;
}

void anchor_ctl_tap_main(int64_t now_ms) {
  if (c.press_valid) {   // LVGL sends CLICKED on release even after a long press: only a press that began on DROP is a Drop
    bool began_on_drop = c.press_btn == ANCHOR_BTN_DROP;
    c.press_valid = false;
    if (!began_on_drop) return;
  }
  if (!c.st.main_enabled || c.st.button != ANCHOR_BTN_DROP || c.pend[CMD_DROP].active) return;
  double radius = c.st.shown_limit_m;
  if (c.watch) anchor_watch_user_cleared(c.watch, c.dir);   // a new anchor replaces any stuck record
  if (send(CMD_DROP, ANCHOR_CMD_DROP, radius, false, now_ms)) {
    c.default_m = radius;
    c.staged = false;
  }
}

void anchor_ctl_hold_begin(int64_t now_ms) {
  c.press_valid = true;
  c.press_btn = c.st.button;
  if (!c.st.main_enabled || c.st.button != ANCHOR_BTN_RAISE || c.hold) return;
  c.hold = true;
  c.hold_sent = false;
  c.hold_start_ms = now_ms;
}

void anchor_ctl_hold_end(int64_t now_ms) {
  (void)now_ms;
  c.hold = false;
  c.hold_sent = false;
}

void anchor_ctl_step(int dir, int64_t now_ms) {
  if (!c.st.radius_enabled) return;
  double v = (c.staged ? c.staged_m : c.st.shown_limit_m) + (dir < 0 ? -step_m() : step_m());
  c.staged_m = v < MIN_RADIUS_M ? MIN_RADIUS_M : v > MAX_RADIUS_M ? MAX_RADIUS_M : v;
  c.staged = true;
  c.staged_sent = false;
  c.staged_at_ms = now_ms;
  c.st.limit_staged = true;
  c.st.shown_limit_m = c.staged_m;
}

const char *anchor_distance_unit(const cfg_units_t *units) { return units && !strcmp(units->depth, "ft") ? "ft" : "m"; }

void anchor_format_distance(char *out, size_t n, double metres, const cfg_units_t *units, bool with_unit) {
  if (isnan(metres)) {
    snprintf(out, n, "--");
    return;
  }
  bool ft = units && !strcmp(units->depth, "ft");
  snprintf(out, n, with_unit ? (ft ? "%.0f ft" : "%.0f m") : "%.0f", ft ? metres / 0.3048 : metres);
}

void anchor_format_ago(char *out, size_t n, int64_t seconds) {
  long mins = (long)(seconds / 60);
  if (mins < 60) snprintf(out, n, "%ld min", mins);
  else snprintf(out, n, "%ldh%02ldm", mins / 60, mins % 60);
}

void anchor_ctl_page_left(void) {
  c.hold = false;
  c.hold_sent = false;
  c.press_valid = false;
  if (c.staged && !c.staged_sent) c.staged = false;   // an unsent radius must not go out minutes later, on return
}
