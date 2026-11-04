// Signal K alerts on the glass. Pure presentation: the app hands over a snapshot
// every tick; button presses go back through alerts_request.
#include "ui_alerts.h"
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "ui_quick.h"
#include "ui_shell.h"

typedef struct {
  char id[64], message[96];
  uint8_t state;
  bool acked;
} rkey_t;

typedef struct {
  lv_obj_t *row, *state, *msg, *age, *ack;
  char id[64];
  rkey_t key;   // what the row shows now; restyle only when it changes
  int64_t changed_ms;   // when this row last started showing a different alert
} row_t;

// After the alarm under a button changes, taps are ignored for this long, so the second
// tap of a double tap (or a bounce) cannot acknowledge an alarm nobody has read yet.
#define TAP_GUARD_MS 1000

// What the banner shows now. update_banner restyles only when this changes, so an
// unchanged tick invalidates nothing.
typedef struct {
  char id[64], message[96];
  uint8_t state, n;
  bool shown, failed, lit;
  int8_t role;
  const ui_colors_t *theme;
} bkey_t;

typedef struct {
  uint8_t n, n_acked;
  bool source_up;
  int32_t status_w;
  const ui_colors_t *theme;
} mkey_t;

PLATFORM_EXT_BSS static struct {
  lv_obj_t *screen, *strip;
  size_class_t sc;
  ui_alert_role_fn role_of;
  ui_alert_page_fn page_of;
  lv_obj_t *banner, *msg, *corner, *ack, *silence, *go;
  lv_obj_t *marker, *marker_label, *acked_marker, *acked_label, *offline;
  lv_obj_t *list, *list_rows, *close;
  row_t rows[ALERTS_MAX];
  char ban_id[64];
  int8_t ban_role;
  bool list_open, list_dirty;
  int32_t corner_w, w;   // w: screen width (a hidden banner has no laid-out size)
  bkey_t bkey;
  mkey_t mkey;
  const ui_colors_t *list_theme;
  int64_t now_ms, ban_changed_ms;   // now_ms: the last tick's time
  char unacked[ALERTS_MAX][64];     // un-acked alarm/emergency ids at the last tick
  uint8_t n_unacked;
} u;

static bool settled(int64_t changed_ms) { return u.now_ms - changed_ms >= TAP_GUARD_MS; }

static const char *const STATE_NAME[] = {"NORMAL", "ALERT", "WARN", "ALARM", "EMERGENCY"};
#define BTN_ACK_W 100
#define BTN_SIL_W 140
#define BTN_GO_W 80
#define GAP 10
#define PAD 8

static void set_text(lv_obj_t *label, const char *txt) {
  if (strcmp(lv_label_get_text(label), txt) != 0) lv_label_set_text(label, txt);
}

static void show(lv_obj_t *o, bool on) {
  if (on != lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;   // no change: no invalidation
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_color_t on_alarm(const ui_colors_t *c) {   // text on the lit banner; night's alarm is its foreground colour
  return lv_color_eq(c->alarm, c->fg) ? c->bg : lv_color_white();
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int32_t w, int32_t h, lv_event_cb_t cb, void *arg) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, arg);
  return b;
}

static void style_button(lv_obj_t *b, lv_color_t bg, lv_color_t fg) {
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_border_color(b, fg, 0);
  lv_obj_set_style_text_color(b, fg, 0);
}

// ---- banner ----

static void on_ack(lv_event_t *e) {
  (void)e;
  if (u.ban_id[0] && settled(u.ban_changed_ms)) alerts_request(u.ban_id, AL_REQ_ACK);
}
static void on_silence(lv_event_t *e) {
  (void)e;
  if (u.ban_id[0] && settled(u.ban_changed_ms)) alerts_request(u.ban_id, AL_REQ_SILENCE);
}
static void on_go(lv_event_t *e) {
  (void)e;
  if (u.ban_role >= 0) {
    uint8_t page = u.page_of && u.ban_id[0] ? u.page_of(u.ban_id) : 0;
    ui_shell_goto((uint8_t)u.ban_role, page);
  }
}

static int32_t banner_bh(void) { return 44; }

// Wide: [message ... corner][ACK][SILENCE][GO] on one row. Square: message and corner
// on the first row, the buttons on the second. The corner is as wide as its text.
static void layout_banner(int32_t corner_w) {
  const lv_font_t *f = &lv_font_montserrat_20;
  int32_t w = u.w, lh = lv_font_get_line_height(f), bh = banner_bh();
  bool wide = u.sc == SIZE_WIDE;
  int32_t btns = BTN_ACK_W + BTN_SIL_W + BTN_GO_W + 2 * GAP;
  int32_t gap = corner_w ? GAP : 0;
  lv_obj_set_size(u.banner, w, wide ? PAD + bh + PAD : PAD + lh + 6 + bh + PAD);
  lv_obj_set_width(u.corner, corner_w);
  int32_t row_y = PAD, by = wide ? PAD : PAD + lh + 6;
  int32_t bx = wide ? w - PAD - btns : PAD;
  int32_t right = wide ? bx - GAP : w - PAD;   // the message row ends here
  if (wide) row_y = PAD + (bh - lh) / 2;
  lv_obj_set_pos(u.corner, right - corner_w, row_y);
  lv_obj_set_size(u.msg, right - corner_w - gap - PAD, lh);
  lv_obj_set_pos(u.msg, PAD, row_y);
  lv_obj_set_pos(u.ack, bx, by);
  lv_obj_set_pos(u.silence, bx + BTN_ACK_W + GAP, by);
  lv_obj_set_pos(u.go, bx + BTN_ACK_W + GAP + BTN_SIL_W + GAP, by);
  u.corner_w = corner_w;
}

static void build_banner(int32_t w, int32_t top) {
  u.w = w;
  u.banner = lv_obj_create(u.screen);
  lv_obj_remove_style_all(u.banner);
  lv_obj_set_size(u.banner, w, 10);
  lv_obj_set_pos(u.banner, 0, top);
  lv_obj_set_style_bg_opa(u.banner, LV_OPA_COVER, 0);
  lv_obj_remove_flag(u.banner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(u.banner, LV_OBJ_FLAG_HIDDEN);

  u.msg = lv_label_create(u.banner);
  lv_obj_set_style_text_font(u.msg, &lv_font_montserrat_20, 0);
  lv_label_set_long_mode(u.msg, LV_LABEL_LONG_DOT);

  u.corner = lv_label_create(u.banner);
  lv_obj_set_style_text_font(u.corner, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_align(u.corner, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(u.corner, LV_LABEL_LONG_CLIP);

  int32_t bh = banner_bh();
  u.ack = button(u.banner, "ACK", BTN_ACK_W, bh, on_ack, NULL);
  u.silence = button(u.banner, "SILENCE", BTN_SIL_W, bh, on_silence, NULL);
  u.go = button(u.banner, "GO", BTN_GO_W, bh, on_go, NULL);
  layout_banner(0);
}

static const alert_t *pick_alarm(const alerts_snapshot_t *s, uint8_t *n_alarms) {
  const alert_t *first = NULL;
  // An acknowledged alarm leaves the banner for the strip's bell, unless the source never
  // got the ACK: that stays up, steady, saying ACK NOT SENT.
  *n_alarms = 0;
  for (uint8_t i = 0; i < s->count; i++) {
    const alert_t *a = &s->a[i];
    if (a->state < AL_ALARM || (a->acked && !a->send_failed)) continue;
    (*n_alarms)++;
    if (!first) first = a;
  }
  return first;
}

static void update_banner(const alerts_snapshot_t *s, int64_t now_ms) {
  uint8_t n;
  const alert_t *a = pick_alarm(s, &n);
  bkey_t k;
  memset(&k, 0, sizeof k);
  k.role = -1;
  k.theme = ui_theme();
  if (a) {
    snprintf(k.id, sizeof k.id, "%s", a->id);
    snprintf(k.message, sizeof k.message, "%s", a->message);
    k.state = (uint8_t)a->state;
    k.n = n;
    k.shown = true;
    k.failed = a->send_failed;
    k.lit = a->acked || ((now_ms < 0 ? 0 : now_ms) / 500) % 2 == 0;   // 1 Hz until acked, then steady
    k.role = u.role_of ? u.role_of(a->id) : -1;
  }
  if (!memcmp(&k, &u.bkey, sizeof k)) return;
  if (strcmp(k.id, u.bkey.id) || k.state != u.bkey.state) u.ban_changed_ms = now_ms;   // a different alarm under ACK
  if (k.shown && !u.bkey.shown) ui_quick_close();   // the quick panel sits above the banner
  u.bkey = k;

  show(u.banner, k.shown);
  if (!a) {
    u.ban_id[0] = 0;
    u.ban_role = -1;
    return;
  }
  snprintf(u.ban_id, sizeof u.ban_id, "%s", k.id);
  u.ban_role = k.role;

  // Palette colours only: lit is alarm with a readable text colour; off is the palette's
  // background and foreground, so night stays red on black.
  const ui_colors_t *c = k.theme;
  lv_color_t bg = k.lit ? c->alarm : c->bg, fg = k.lit ? on_alarm(c) : c->fg;
  lv_obj_set_style_bg_color(u.banner, bg, 0);
  lv_obj_set_style_text_color(u.msg, fg, 0);
  lv_obj_set_style_text_color(u.corner, fg, 0);
  style_button(u.ack, bg, fg);
  style_button(u.silence, bg, fg);
  style_button(u.go, bg, fg);

  set_text(u.msg, k.message[0] ? k.message : STATE_NAME[k.state]);
  char corner[16] = "";   // "ACK NOT SENT" is 12
  if (k.failed) snprintf(corner, sizeof corner, "ACK NOT SENT");
  else if (n > 1) snprintf(corner, sizeof corner, "+%u", (unsigned)(n - 1));
  set_text(u.corner, corner);
  int32_t cw = corner[0] ? lv_text_get_width(corner, strlen(corner), &lv_font_montserrat_20, 0) + 4 : 0;
  if (cw != u.corner_w) layout_banner(cw);
  show(u.go, k.role >= 0);
}

// ---- marker and offline text ----

static void on_marker(lv_event_t *e) {
  (void)e;
  ui_quick_close();
  u.list_open = true;
  u.list_dirty = true;
  show(u.list, true);
}

static lv_obj_t *build_one_marker(lv_obj_t **label) {
  lv_obj_t *m = lv_obj_create(u.strip);
  lv_obj_remove_style_all(m);
  lv_obj_set_size(m, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(m, 10, 0);
  lv_obj_set_style_pad_ver(m, 1, 0);
  lv_obj_set_style_radius(m, 6, 0);
  lv_obj_set_style_border_width(m, 2, 0);
  lv_obj_add_flag(m, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(m, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_ext_click_area(m, 8);
  lv_obj_add_event_cb(m, on_marker, LV_EVENT_CLICKED, NULL);
  *label = lv_label_create(m);
  lv_obj_set_style_text_font(*label, &lv_font_montserrat_20, 0);
  lv_label_set_text(*label, "");
  return m;
}

static void build_marker(void) {
  u.marker = build_one_marker(&u.marker_label);
  lv_obj_set_style_border_color(u.marker, ui_theme()->warn, 0);
  lv_obj_set_style_text_color(u.marker, ui_theme()->warn, 0);
  u.acked_marker = build_one_marker(&u.acked_label);
  lv_obj_set_style_border_color(u.acked_marker, ui_theme()->alarm, 0);
  lv_obj_set_style_text_color(u.acked_marker, ui_theme()->alarm, 0);

  u.offline = lv_label_create(u.strip);
  lv_obj_set_style_text_font(u.offline, &lv_font_montserrat_14, 0);
  lv_label_set_text(u.offline, "");
  lv_obj_add_flag(u.offline, LV_OBJ_FLAG_HIDDEN);
}

// On square boards the strip has no room for a second label beside the page dots, so
// "SK ALERTS OFFLINE" takes over the status text. Wide boards show it beside the status.
static bool offline_in_status(void) { return u.sc == SIZE_SQUARE && ui_shell_status(); }

static void update_marker(const alerts_snapshot_t *s) {
  lv_obj_t *status = ui_shell_status();
  unsigned n = 0, na = 0;
  for (uint8_t i = 0; i < s->count; i++) {
    n += s->a[i].state == AL_ALERT || s->a[i].state == AL_WARN;
    na += s->a[i].state >= AL_ALARM && s->a[i].acked && !s->a[i].send_failed;
  }
  mkey_t k = {(uint8_t)n, (uint8_t)na, s->source_up, status ? lv_obj_get_width(status) : 0, ui_theme()};
  if (!memcmp(&k, &u.mkey, sizeof k)) return;
  u.mkey = k;

  const ui_colors_t *c = k.theme;
  char t[12] = "";
  if (n) snprintf(t, sizeof t, "%u", n);
  set_text(u.marker_label, t);
  lv_obj_set_style_border_color(u.marker, c->warn, 0);
  lv_obj_set_style_text_color(u.marker, c->warn, 0);
  show(u.marker, n > 0);
  char ta[16] = "";
  if (na) snprintf(ta, sizeof ta, LV_SYMBOL_BELL " %u", na);
  set_text(u.acked_label, ta);
  lv_obj_set_style_border_color(u.acked_marker, c->alarm, 0);
  lv_obj_set_style_text_color(u.acked_marker, c->alarm, 0);
  show(u.acked_marker, na > 0);

  bool in_status = offline_in_status();
  if (in_status) {
    ui_shell_set_status_override(s->source_up ? NULL : "SK ALERTS OFFLINE", c->alarm);
    set_text(u.offline, "");
    show(u.offline, false);
  } else {
    set_text(u.offline, s->source_up ? "" : "SK ALERTS OFFLINE");
    lv_obj_set_style_text_color(u.offline, c->warn, 0);
    show(u.offline, !s->source_up);
  }

  // Sit left of the status text, which changes width; the offline text goes left of the marker.
  lv_obj_update_layout(u.strip);
  // Right to left: status, acknowledged-alarm bell, alert/warn count, offline text.
  lv_obj_t *left = status;
  if (na) {
    if (left) lv_obj_align_to(u.acked_marker, left, LV_ALIGN_OUT_LEFT_MID, -12, 0);
    else lv_obj_align(u.acked_marker, LV_ALIGN_RIGHT_MID, -12, 0);
    left = u.acked_marker;
  }
  if (left) lv_obj_align_to(u.marker, left, LV_ALIGN_OUT_LEFT_MID, -12, 0);
  else lv_obj_align(u.marker, LV_ALIGN_RIGHT_MID, -12, 0);
  if (n) lv_obj_align_to(u.offline, u.marker, LV_ALIGN_OUT_LEFT_MID, -12, 0);
  else if (na) lv_obj_align_to(u.offline, u.acked_marker, LV_ALIGN_OUT_LEFT_MID, -12, 0);
  else if (status) lv_obj_align_to(u.offline, status, LV_ALIGN_OUT_LEFT_MID, -12, 0);
  else lv_obj_align(u.offline, LV_ALIGN_RIGHT_MID, -12, 0);
  u.mkey.status_w = status ? lv_obj_get_width(status) : 0;   // the override changed it
}

// ---- list ----

static void on_close(lv_event_t *e) {
  (void)e;
  u.list_open = false;
  show(u.list, false);
}

static void on_row_ack(lv_event_t *e) {
  row_t *r = lv_event_get_user_data(e);
  if (r->id[0] && settled(r->changed_ms)) alerts_request(r->id, AL_REQ_ACK);
}

static void build_list(int32_t w, int32_t h) {
  bool wide = u.sc == SIZE_WIDE;
  u.list = lv_obj_create(u.screen);
  lv_obj_remove_style_all(u.list);
  lv_obj_set_size(u.list, w, h);
  lv_obj_set_style_bg_opa(u.list, LV_OPA_COVER, 0);
  lv_obj_remove_flag(u.list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(u.list, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *title = lv_label_create(u.list);
  lv_label_set_text(title, "ALERTS");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  lv_obj_set_pos(title, 12, 10);
  u.close = button(u.list, "CLOSE", 110, 44, on_close, NULL);
  lv_obj_set_pos(u.close, w - 12 - 110, 6);

  u.list_rows = lv_obj_create(u.list);
  lv_obj_remove_style_all(u.list_rows);
  lv_obj_set_size(u.list_rows, w, h - 60);
  lv_obj_set_pos(u.list_rows, 0, 60);
  lv_obj_set_flex_flow(u.list_rows, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_hor(u.list_rows, 8, 0);
  lv_obj_set_style_pad_row(u.list_rows, 4, 0);
  lv_obj_add_flag(u.list_rows, LV_OBJ_FLAG_SCROLLABLE);

  int32_t sw = wide ? 130 : 100;
  for (int i = 0; i < ALERTS_MAX; i++) {
    row_t *r = &u.rows[i];
    r->row = lv_obj_create(u.list_rows);
    lv_obj_remove_style_all(r->row);
    lv_obj_set_size(r->row, LV_PCT(100), 48);
    lv_obj_set_flex_flow(r->row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r->row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r->row, 8, 0);
    lv_obj_set_style_border_width(r->row, 1, 0);
    lv_obj_set_style_border_side(r->row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_remove_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r->row, LV_OBJ_FLAG_HIDDEN);

    r->state = lv_label_create(r->row);
    lv_obj_set_width(r->state, sw);
    lv_obj_set_style_text_font(r->state, &lv_font_montserrat_20, 0);
    r->msg = lv_label_create(r->row);
    lv_obj_set_flex_grow(r->msg, 1);
    lv_label_set_long_mode(r->msg, LV_LABEL_LONG_DOT);
    lv_obj_set_height(r->msg, lv_font_get_line_height(&lv_font_montserrat_20));
    lv_obj_set_style_text_font(r->msg, &lv_font_montserrat_20, 0);
    r->age = lv_label_create(r->row);
    lv_obj_set_width(r->age, 64);
    lv_obj_set_style_text_font(r->age, &lv_font_montserrat_20, 0);
    r->ack = button(r->row, "ACK", 80, 40, on_row_ack, r);
  }
}

static void update_list(const alerts_snapshot_t *s, int64_t now_ms) {
  if (s->count == 0) {   // nothing left to list
    u.list_open = false;
    show(u.list, false);
  }
  if (!u.list_open) return;
  const ui_colors_t *c = ui_theme();
  bool all = u.list_dirty || u.list_theme != c;   // just opened, or the palette changed
  if (all) {
    lv_obj_set_style_bg_color(u.list, c->bg, 0);
    lv_obj_set_style_text_color(u.list, c->fg, 0);
    style_button(u.close, c->strip_bg, c->fg);
  }
  for (uint8_t i = 0; i < ALERTS_MAX; i++) {
    row_t *r = &u.rows[i];
    show(r->row, i < s->count);
    if (i >= s->count) continue;
    const alert_t *a = &s->a[i];
    rkey_t k;
    memset(&k, 0, sizeof k);
    snprintf(k.id, sizeof k.id, "%s", a->id);
    snprintf(k.message, sizeof k.message, "%s", a->message);
    k.state = (uint8_t)a->state;
    k.acked = a->acked;
    if (all || memcmp(&k, &r->key, sizeof k)) {
      if (strcmp(k.id, r->key.id) || k.state != r->key.state) r->changed_ms = now_ms;
      r->key = k;
      snprintf(r->id, sizeof r->id, "%s", a->id);
      set_text(r->state, STATE_NAME[a->state]);
      lv_obj_set_style_text_color(r->state, a->acked ? c->dim : a->state >= AL_ALARM ? c->alarm : c->warn, 0);
      set_text(r->msg, a->message[0] ? a->message : a->id);
      lv_obj_set_style_border_color(r->row, c->dim, 0);
      style_button(r->ack, c->strip_bg, c->fg);
      show(r->ack, !a->acked);
    }
    int64_t sec = (now_ms - a->since_ms) / 1000;   // the age is the only thing that moves each second
    if (sec < 0) sec = 0;
    char age[16];
    snprintf(age, sizeof age, "%02d:%02d", (int)(sec / 60 % 100), (int)(sec % 60));
    set_text(r->age, age);
  }
  u.list_dirty = false;
  u.list_theme = c;
}

// ---- public ----

void ui_alerts_attach(lv_obj_t *screen, lv_obj_t *strip, size_class_t sc, ui_alert_role_fn role_of,
                      ui_alert_page_fn page_of) {
  memset(&u, 0, sizeof u);
  u.screen = screen;
  u.strip = strip;
  u.sc = sc;
  u.role_of = role_of;
  u.page_of = page_of;
  u.ban_role = -1;
  u.bkey.role = -1;
  lv_obj_update_layout(screen);
  int32_t w = lv_obj_get_width(screen), h = lv_obj_get_height(screen);
  build_marker();
  build_banner(w, lv_obj_get_height(strip));
  build_list(w, h);
}

// The quick panel sits above the banner and has no timeout, so any newly raised (or
// re-raised) un-acked alarm closes it, even while the banner already shows another.
static void close_quick_on_new_alarm(const alerts_snapshot_t *s) {
  char now[ALERTS_MAX][64];
  uint8_t n = 0;
  bool fresh = false;
  for (uint8_t i = 0; i < s->count && n < ALERTS_MAX; i++) {
    const alert_t *a = &s->a[i];
    if (a->state < AL_ALARM || a->acked) continue;
    snprintf(now[n], sizeof now[n], "%s", a->id);
    bool seen = false;
    for (uint8_t j = 0; j < u.n_unacked && !seen; j++) seen = strcmp(u.unacked[j], now[n]) == 0;
    fresh |= !seen;
    n++;
  }
  memcpy(u.unacked, now, sizeof now[0] * n);
  u.n_unacked = n;
  if (fresh) ui_quick_close();
}

void ui_alerts_update(const alerts_snapshot_t *s, int64_t now_ms) {
  if (!u.screen) return;
  u.now_ms = now_ms;
  close_quick_on_new_alarm(s);
  update_banner(s, now_ms);
  update_marker(s);
  update_list(s, now_ms);
}

bool ui_alerts_banner_visible(void) { return u.banner && !lv_obj_has_flag(u.banner, LV_OBJ_FLAG_HIDDEN); }
const char *ui_alerts_banner_text(void) { return u.msg ? lv_label_get_text(u.msg) : ""; }
bool ui_alerts_go_visible(void) { return ui_alerts_banner_visible() && !lv_obj_has_flag(u.go, LV_OBJ_FLAG_HIDDEN); }
const char *ui_alerts_acked_text(void) { return u.acked_label ? lv_label_get_text(u.acked_label) : ""; }
const char *ui_alerts_marker_text(void) { return u.marker_label ? lv_label_get_text(u.marker_label) : ""; }
const char *ui_alerts_offline_text(void) {
  if (offline_in_status()) return strcmp(lv_label_get_text(ui_shell_status()), "SK ALERTS OFFLINE") ? "" : "SK ALERTS OFFLINE";
  return u.offline ? lv_label_get_text(u.offline) : "";
}
const char *ui_alerts_corner_text(void) { return u.corner ? lv_label_get_text(u.corner) : ""; }
bool ui_alerts_list_visible(void) { return u.list && !lv_obj_has_flag(u.list, LV_OBJ_FLAG_HIDDEN); }
uint8_t ui_alerts_list_rows(void) {
  uint8_t n = 0;
  for (int i = 0; i < ALERTS_MAX; i++) n += u.rows[i].row && !lv_obj_has_flag(u.rows[i].row, LV_OBJ_FLAG_HIDDEN);
  return n;
}

lv_obj_t *ui_alerts_part(ui_alerts_part_t p) {
  switch (p) {
    case UI_ALERTS_BANNER: return u.banner;
    case UI_ALERTS_ACK: return u.ack;
    case UI_ALERTS_SILENCE: return u.silence;
    case UI_ALERTS_GO: return u.go;
    case UI_ALERTS_MARKER: return u.marker;
    case UI_ALERTS_CLOSE: return u.close;
    case UI_ALERTS_CORNER: return u.corner;
    case UI_ALERTS_OFFLINE: return offline_in_status() ? ui_shell_status() : u.offline;
    case UI_ALERTS_ROW0_ACK: return u.rows[0].ack;
    case UI_ALERTS_ACKED_MARKER: return u.acked_marker;
  }
  return NULL;
}
