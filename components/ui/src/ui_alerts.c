// Signal K alerts on the glass. Pure presentation: the app hands over a snapshot
// every tick; button presses go back through alerts_request.
#include "ui_alerts.h"
#include <stdio.h>
#include <string.h>
#include "ui_shell.h"

typedef struct {
  lv_obj_t *row, *state, *msg, *age, *ack;
  char id[64];
} row_t;

static struct {
  lv_obj_t *screen, *strip;
  size_class_t sc;
  ui_alert_role_fn role_of;
  lv_obj_t *banner, *msg, *corner, *ack, *silence, *go;
  lv_obj_t *marker, *marker_label, *offline;
  lv_obj_t *list, *list_rows, *close;
  row_t rows[ALERTS_MAX];
  char ban_id[64];
  int8_t ban_role;
  bool list_open;
} u;

static const char *const STATE_NAME[] = {"NORMAL", "ALERT", "WARN", "ALARM", "EMERGENCY"};

static void set_text(lv_obj_t *label, const char *txt) {
  if (strcmp(lv_label_get_text(label), txt) != 0) lv_label_set_text(label, txt);
}

static void show(lv_obj_t *o, bool on) {
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_color_t on_alarm(void) {   // text on the red banner; night's alarm is its foreground colour
  return lv_color_eq(ui_theme()->alarm, ui_theme()->fg) ? ui_theme()->bg : lv_color_white();
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int32_t x, int32_t y, int32_t w, int32_t h,
                        lv_event_cb_t cb, void *arg) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(b, w, h);
  lv_obj_set_pos(b, x, y);
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

static void on_ack(lv_event_t *e) { (void)e; if (u.ban_id[0]) alerts_request(u.ban_id, AL_REQ_ACK); }
static void on_silence(lv_event_t *e) { (void)e; if (u.ban_id[0]) alerts_request(u.ban_id, AL_REQ_SILENCE); }
static void on_go(lv_event_t *e) {
  (void)e;
  if (u.ban_role >= 0) ui_shell_goto_role((uint8_t)u.ban_role);
}

static void build_banner(int32_t w, int32_t top) {
  bool wide = u.sc == SIZE_WIDE;
  const lv_font_t *f = wide ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
  int32_t lh = lv_font_get_line_height(f), bh = wide ? 48 : 44, pad = 8;
  u.banner = lv_obj_create(u.screen);
  lv_obj_remove_style_all(u.banner);
  lv_obj_set_size(u.banner, w, pad + 2 * lh + pad + bh + pad);
  lv_obj_set_pos(u.banner, 0, top);
  lv_obj_set_style_bg_opa(u.banner, LV_OPA_COVER, 0);
  lv_obj_remove_flag(u.banner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(u.banner, LV_OBJ_FLAG_HIDDEN);

  int32_t corner_w = 120;
  u.msg = lv_label_create(u.banner);
  lv_obj_set_style_text_font(u.msg, f, 0);
  lv_label_set_long_mode(u.msg, LV_LABEL_LONG_DOT);
  lv_obj_set_size(u.msg, w - 2 * pad - corner_w - pad, 2 * lh);
  lv_obj_set_pos(u.msg, pad, pad);

  u.corner = lv_label_create(u.banner);
  lv_obj_set_style_text_font(u.corner, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_align(u.corner, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(u.corner, LV_LABEL_LONG_CLIP);
  lv_obj_set_size(u.corner, corner_w, lv_font_get_line_height(&lv_font_montserrat_20));
  lv_obj_set_pos(u.corner, w - pad - corner_w, pad);

  int32_t y = pad + 2 * lh + pad, x = pad;
  u.ack = button(u.banner, "ACK", x, y, 100, bh, on_ack, NULL);
  x += 100 + 10;
  u.silence = button(u.banner, "SILENCE", x, y, 140, bh, on_silence, NULL);
  x += 140 + 10;
  u.go = button(u.banner, "GO", x, y, 80, bh, on_go, NULL);
}

static const alert_t *pick_alarm(const alerts_snapshot_t *s, uint8_t *n_alarms) {
  const alert_t *first = NULL, *unacked = NULL;
  *n_alarms = 0;
  for (uint8_t i = 0; i < s->count; i++) {
    const alert_t *a = &s->a[i];
    if (a->state < AL_ALARM) continue;
    (*n_alarms)++;
    if (!first) first = a;
    if (!unacked && !a->acked) unacked = a;
  }
  return unacked ? unacked : first;
}

static void update_banner(const alerts_snapshot_t *s, int64_t now_ms) {
  uint8_t n;
  const alert_t *a = pick_alarm(s, &n);
  show(u.banner, a != NULL);
  if (!a) {
    u.ban_id[0] = 0;
    u.ban_role = -1;
    return;
  }
  snprintf(u.ban_id, sizeof u.ban_id, "%s", a->id);
  u.ban_role = u.role_of ? u.role_of(a->id) : -1;

  const ui_colors_t *c = ui_theme();
  bool lit = a->acked || ((now_ms < 0 ? 0 : now_ms) / 500) % 2 == 0;   // 1 Hz until acked, then steady
  lv_color_t txt = on_alarm();
  lv_obj_set_style_bg_color(u.banner, lit ? c->alarm : lv_color_black(), 0);
  lv_obj_set_style_text_color(u.msg, lit ? txt : lv_color_white(), 0);
  lv_obj_set_style_text_color(u.corner, lit ? txt : lv_color_white(), 0);
  for (int i = 0; i < 3; i++) {
    lv_obj_t *b = i == 0 ? u.ack : i == 1 ? u.silence : u.go;
    style_button(b, lit ? c->alarm : lv_color_black(), lit ? txt : lv_color_white());
  }

  set_text(u.msg, a->message[0] ? a->message : STATE_NAME[a->state]);
  char corner[16] = "";   // "ACK NOT SENT" is 12
  if (a->send_failed) snprintf(corner, sizeof corner, "ACK NOT SENT");
  else if (n > 1) snprintf(corner, sizeof corner, "+%u", (unsigned)(n - 1));
  set_text(u.corner, corner);
  show(u.go, u.ban_role >= 0);
}

// ---- marker and offline text ----

static void on_marker(lv_event_t *e) {
  (void)e;
  u.list_open = true;
  show(u.list, true);
}

static void build_marker(void) {
  u.marker = lv_obj_create(u.strip);
  lv_obj_remove_style_all(u.marker);
  lv_obj_set_size(u.marker, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(u.marker, 10, 0);
  lv_obj_set_style_pad_ver(u.marker, 1, 0);
  lv_obj_set_style_radius(u.marker, 6, 0);
  lv_obj_set_style_border_width(u.marker, 2, 0);
  lv_obj_set_style_border_color(u.marker, ui_theme()->warn, 0);
  lv_obj_set_style_text_color(u.marker, ui_theme()->warn, 0);
  lv_obj_add_flag(u.marker, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(u.marker, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_ext_click_area(u.marker, 8);
  lv_obj_add_event_cb(u.marker, on_marker, LV_EVENT_CLICKED, NULL);
  u.marker_label = lv_label_create(u.marker);
  lv_obj_set_style_text_font(u.marker_label, &lv_font_montserrat_20, 0);
  lv_label_set_text(u.marker_label, "");

  u.offline = lv_label_create(u.strip);
  lv_obj_set_style_text_font(u.offline, &lv_font_montserrat_14, 0);
  lv_label_set_text(u.offline, "");
  lv_obj_add_flag(u.offline, LV_OBJ_FLAG_HIDDEN);
}

static void update_marker(const alerts_snapshot_t *s) {
  unsigned n = 0;
  for (uint8_t i = 0; i < s->count; i++) n += s->a[i].state == AL_ALERT || s->a[i].state == AL_WARN;
  char t[12] = "";
  if (n) snprintf(t, sizeof t, "%u", n);
  set_text(u.marker_label, t);
  lv_obj_set_style_border_color(u.marker, ui_theme()->warn, 0);
  lv_obj_set_style_text_color(u.marker, ui_theme()->warn, 0);
  show(u.marker, n > 0);

  set_text(u.offline, s->source_up ? "" : "SK ALERTS OFFLINE");
  lv_obj_set_style_text_color(u.offline, ui_theme()->warn, 0);
  show(u.offline, !s->source_up);

  // Sit left of the status text, which changes width; the offline text goes left of the marker.
  lv_obj_t *status = ui_shell_status();
  lv_obj_t *anchor = status ? status : u.strip;
  lv_obj_update_layout(u.strip);
  if (status) lv_obj_align_to(u.marker, anchor, LV_ALIGN_OUT_LEFT_MID, -12, 0);
  else lv_obj_align(u.marker, LV_ALIGN_RIGHT_MID, -12, 0);
  if (n) lv_obj_align_to(u.offline, u.marker, LV_ALIGN_OUT_LEFT_MID, -12, 0);
  else if (status) lv_obj_align_to(u.offline, anchor, LV_ALIGN_OUT_LEFT_MID, -12, 0);
  else lv_obj_align(u.offline, LV_ALIGN_RIGHT_MID, -12, 0);
}

// ---- list ----

static void on_close(lv_event_t *e) {
  (void)e;
  u.list_open = false;
  show(u.list, false);
}

static void on_row_ack(lv_event_t *e) {
  row_t *r = lv_event_get_user_data(e);
  if (r->id[0]) alerts_request(r->id, AL_REQ_ACK);
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
  u.close = button(u.list, "CLOSE", w - 12 - 110, 6, 110, 44, on_close, NULL);

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
    r->ack = button(r->row, "ACK", 0, 0, 80, 40, on_row_ack, r);
  }
}

static void update_list(const alerts_snapshot_t *s, int64_t now_ms) {
  if (s->count == 0) {   // nothing left to list
    u.list_open = false;
    show(u.list, false);
  }
  if (!u.list_open) return;
  const ui_colors_t *c = ui_theme();
  lv_obj_set_style_bg_color(u.list, c->bg, 0);
  lv_obj_set_style_text_color(u.list, c->fg, 0);
  style_button(u.close, c->strip_bg, c->fg);
  for (uint8_t i = 0; i < ALERTS_MAX; i++) {
    row_t *r = &u.rows[i];
    show(r->row, i < s->count);
    if (i >= s->count) continue;
    const alert_t *a = &s->a[i];
    snprintf(r->id, sizeof r->id, "%s", a->id);
    set_text(r->state, STATE_NAME[a->state]);
    lv_obj_set_style_text_color(r->state, a->state >= AL_ALARM ? c->alarm : c->warn, 0);
    set_text(r->msg, a->message[0] ? a->message : a->id);
    int64_t sec = (now_ms - a->since_ms) / 1000;
    if (sec < 0) sec = 0;
    char age[16];
    snprintf(age, sizeof age, "%02d:%02d", (int)(sec / 60 % 100), (int)(sec % 60));
    set_text(r->age, age);
    lv_obj_set_style_border_color(r->row, c->dim, 0);
    style_button(r->ack, c->strip_bg, c->fg);
    show(r->ack, !a->acked);
  }
}

// ---- public ----

void ui_alerts_attach(lv_obj_t *screen, lv_obj_t *strip, size_class_t sc, ui_alert_role_fn role_of) {
  memset(&u, 0, sizeof u);
  u.screen = screen;
  u.strip = strip;
  u.sc = sc;
  u.role_of = role_of;
  u.ban_role = -1;
  lv_obj_update_layout(screen);
  int32_t w = lv_obj_get_width(screen), h = lv_obj_get_height(screen);
  build_marker();
  build_banner(w, lv_obj_get_height(strip));
  build_list(w, h);
}

void ui_alerts_update(const alerts_snapshot_t *s, int64_t now_ms) {
  if (!u.screen) return;
  update_banner(s, now_ms);
  update_marker(s);
  update_list(s, now_ms);
}

bool ui_alerts_banner_visible(void) { return u.banner && !lv_obj_has_flag(u.banner, LV_OBJ_FLAG_HIDDEN); }
const char *ui_alerts_banner_text(void) { return u.msg ? lv_label_get_text(u.msg) : ""; }
bool ui_alerts_go_visible(void) { return ui_alerts_banner_visible() && !lv_obj_has_flag(u.go, LV_OBJ_FLAG_HIDDEN); }
const char *ui_alerts_marker_text(void) { return u.marker_label ? lv_label_get_text(u.marker_label) : ""; }
const char *ui_alerts_offline_text(void) { return u.offline ? lv_label_get_text(u.offline) : ""; }
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
  }
  return NULL;
}
