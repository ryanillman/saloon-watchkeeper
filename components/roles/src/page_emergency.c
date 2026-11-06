// The emergency script page. What is declared lives in emergency.c; this draws it, and turns a held button into a call.
#include <stdio.h>
#include <string.h>
#include "emergency_page.h"
#include "nav_state.h"

enum { B_MOB, B_FIRE, B_FLOOD, B_SINKING, B_OTHER, B_CANCEL, B_COUNT };
static const em_nature_t NATURE_OF[B_COUNT] = {EM_MOB, EM_FIRE, EM_FLOOD, EM_SINKING, EM_OTHER, EM_NONE};
static const char *const PART_OF[B_COUNT] = {"btn_mob", "btn_fire", "btn_flood", "btn_sinking", "btn_other", "btn_cancel"};

static const emergency_actions_t *act;

static struct {
  lv_obj_t *nature, *send, *steps, *script, *footer, *hint, *btn[B_COUNT], *fill[B_COUNT];
  nav_ids_t ids;
  bool built, wide;
} w;

static struct {
  int which;          // button index, -1 = none
  uint32_t start;
  bool fired;
  lv_timer_t *timer;
} hold = {-1, 0, false, NULL};

void emergency_page_set_actions(const emergency_actions_t *a) { act = a; }

static void end_hold(void) {
  if (hold.timer) {
    lv_timer_delete(hold.timer);
    hold.timer = NULL;
  }
  if (hold.which >= 0 && w.built && w.fill[hold.which] && lv_obj_is_valid(w.fill[hold.which])) lv_obj_set_width(w.fill[hold.which], 0);
  hold.which = -1;
  hold.fired = false;
}

static void hold_tick(lv_timer_t *t) {
  (void)t;
  if (hold.which < 0 || !w.built || !lv_obj_is_valid(w.btn[hold.which])) {
    end_hold();
    return;
  }
  uint32_t ms = lv_tick_elaps(hold.start);
  int32_t full = lv_obj_get_width(w.btn[hold.which]);
  lv_obj_set_width(w.fill[hold.which], ms >= EMERGENCY_HOLD_MS ? full : (int32_t)((uint64_t)full * ms / EMERGENCY_HOLD_MS));
  if (ms < EMERGENCY_HOLD_MS || hold.fired) return;
  hold.fired = true;   // once per hold, however long the finger stays
  if (!act) return;
  if (hold.which == B_CANCEL) {
    if (act->cancel) act->cancel();
  } else if (act->press) {
    act->press(NATURE_OF[hold.which]);
  }
}

static void on_pressed(lv_event_t *e) {
  end_hold();
  hold.which = (int)(uintptr_t)lv_event_get_user_data(e);
  hold.start = lv_tick_get();
  hold.timer = lv_timer_create(hold_tick, 30, NULL);
}

static void on_released(lv_event_t *e) {
  (void)e;
  end_hold();   // an early release, or the finger left the button: nothing is sent
}

static lv_obj_t *make_label(lv_obj_t *area, const lv_font_t *f, lv_color_t col, int32_t x, int32_t y, int32_t width) {
  lv_obj_t *l = lv_label_create(area);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, col, 0);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_width(l, width);
  lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
  lv_label_set_text(l, "");
  return l;
}

static lv_obj_t *make_button(lv_obj_t *area, const char *text, const lv_font_t *font, int which, int32_t x, int32_t y, int32_t bw, int32_t bh) {
  const ui_colors_t *c = ui_theme();
  lv_obj_t *b = lv_button_create(area);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, bw, bh);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_border_color(b, c->fg, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, c->strip_bg, 0);
  lv_obj_set_style_clip_corner(b, true, 0);
  lv_obj_t *fill = lv_obj_create(b);   // grows left to right while the button is held
  lv_obj_remove_style_all(fill);
  lv_obj_set_pos(fill, 0, 0);
  lv_obj_set_size(fill, 0, bh);
  lv_obj_set_style_bg_color(fill, c->accent, 0);
  lv_obj_set_style_bg_opa(fill, LV_OPA_60, 0);
  lv_obj_remove_flag(fill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, c->fg, 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, on_pressed, LV_EVENT_PRESSED, (void *)(uintptr_t)which);
  lv_obj_add_event_cb(b, on_released, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(b, on_released, LV_EVENT_PRESS_LOST, NULL);
  w.fill[which] = fill;
  return b;
}

void emergency_page_reset(void) {
  end_hold();
  memset(&w, 0, sizeof w);
}

// For someone who has never used the radio. The call itself is built in emergency.c from what is declared.
#define STEPS_TEXT "1. On the VHF radio, press and hold the red DISTRESS button for 3 seconds.\n2. Then speak into the microphone:"
#define NOTHING_TEXT "No emergency declared. Hold the matching button below, then read the radio call here."

void emergency_page_build(lv_obj_t *area, size_class_t sc, role_binding_t *b) {
  (void)b;
  emergency_page_reset();
  nav_ids_bind(&w.ids);
  lv_obj_update_layout(area);
  int32_t W = lv_obj_get_width(area), H = lv_obj_get_height(area), pad = 8;
  const ui_colors_t *c = ui_theme();
  w.wide = sc == SIZE_WIDE;
  const lv_font_t *big = &lv_font_montserrat_28, *mid = &lv_font_montserrat_20, *small = &lv_font_montserrat_14;

  // Wide: the nature on the left, the send status at the right. Square: the status goes under the nature.
  int32_t nat_w = w.wide ? W * 5 / 8 : W - 2 * pad, steps_y = w.wide ? 52 : 76;
  w.nature = make_label(area, big, c->alarm, pad, pad, nat_w);
  w.send = w.wide ? make_label(area, mid, c->fg, pad + nat_w, pad + 4, W - nat_w - 2 * pad) : make_label(area, mid, c->fg, pad, 46, W - 2 * pad);
  if (w.wide) lv_obj_set_style_text_align(w.send, LV_TEXT_ALIGN_RIGHT, 0);

  w.steps = make_label(area, w.wide ? mid : small, c->fg, pad, steps_y, W - 2 * pad);
  lv_label_set_long_mode(w.steps, LV_LABEL_LONG_WRAP);
  lv_label_set_text(w.steps, STEPS_TEXT);
  lv_obj_update_layout(area);
  int32_t script_y = steps_y + lv_obj_get_height(w.steps) + 6;
  w.script = make_label(area, w.wide ? big : mid, c->fg, pad, script_y, W - 2 * pad);
  lv_label_set_long_mode(w.script, LV_LABEL_LONG_WRAP);

  int32_t bh = 64, by = H - pad - bh;
  w.hint = make_label(area, small, c->dim, pad, by - 20, W - 2 * pad);
  w.footer = make_label(area, small, c->dim, pad, by - 20 - 22, W - 2 * pad);
  int32_t slot = (W - 2 * pad) / B_COUNT;
  static const char *const wide_text[B_COUNT] = {"MOB", "FIRE", "FLOOD", "SINKING", "OTHER", "CANCEL"};
  static const char *const square_text[B_COUNT] = {"MOB", "FIRE", "FLOOD", "SINK", "OTHER", "CANCEL"};
  for (int i = 0; i < B_COUNT; i++)
    w.btn[i] = make_button(area, w.wide ? wide_text[i] : square_text[i], w.wide ? mid : small, i, pad + i * slot, by, slot - 6, bh);
  w.built = true;
}

lv_obj_t *emergency_page_part(const char *name) {
  if (!w.built) return NULL;
  static const char *const names[] = {"nature", "send", "steps", "script", "footer", "hint"};
  lv_obj_t *const parts[] = {w.nature, w.send, w.steps, w.script, w.footer, w.hint};
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
    if (!strcmp(names[i], name)) return parts[i];
  for (int i = 0; i < B_COUNT; i++)
    if (!strcmp(PART_OF[i], name)) return w.btn[i];
  return NULL;
}

static void set_label(lv_obj_t *l, const char *text) {
  if (strcmp(lv_label_get_text(l), text)) lv_label_set_text(l, text);
}

static void show(lv_obj_t *o, bool on) {
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void emergency_page_refresh(role_binding_t *b, int64_t now_ms) {
  (void)b;
  if (!w.built) return;
  char t[96];
  em_state_t s;
  em_snapshot(&s);
  bool avail = act && act->available && act->available();
  if (!avail) end_hold();

  if (s.nature != EM_NONE) snprintf(t, sizeof t, "NATURE: %s", em_nature_label(s.nature));
  else snprintf(t, sizeof t, "NO EMERGENCY DECLARED");
  set_label(w.nature, t);
  lv_obj_set_style_text_color(w.nature, s.nature != EM_NONE ? ui_theme()->alarm : ui_theme()->dim, 0);

  const char *send = "";
  switch (s.send) {
    case EM_SENDING: send = "SENDING..."; break;
    case EM_SENT: send = "SENT"; break;
    case EM_NOT_SENT: send = "NOT SENT - TELL HELM"; break;
    default: send = s.origin == EM_ORIGIN_NOTIFICATION ? "RECEIVED FROM SIGNAL K" : ""; break;
  }
  set_label(w.send, send);
  lv_obj_set_style_text_color(w.send, s.send == EM_NOT_SENT ? ui_theme()->alarm : ui_theme()->fg, 0);

  const char *name = act && act->vessel_name ? act->vessel_name() : "", *mmsi = act && act->vessel_mmsi ? act->vessel_mmsi() : "";
  nav_view_t v;
  nav_view_build(&w.ids, now_ms, &v);

  // The call to read, in the largest text that still ends above the footer (pages must not scroll).
  char call[600];
  if (s.nature == EM_NONE || !em_script_text(s.nature, name, v.have_pos, v.lat, v.lon, call, sizeof call)) snprintf(call, sizeof call, NOTHING_TEXT);
  if (strcmp(lv_label_get_text(w.script), call)) {
    static const lv_font_t *const FIT[] = {&lv_font_montserrat_28, &lv_font_montserrat_20, &lv_font_montserrat_14};
    lv_label_set_text(w.script, call);
    int32_t room = lv_obj_get_y(w.footer) - lv_obj_get_y(w.script) - 4;
    for (int i = w.wide ? 0 : 1; i < 3; i++) {
      lv_obj_set_style_text_font(w.script, FIT[i], 0);
      lv_obj_update_layout(w.script);
      if (lv_obj_get_height(w.script) <= room) break;
    }
  }
  show(w.steps, s.nature != EM_NONE);

  // The numbers, for the chartplotter and the radio's own distress call.
  char foot[120];
  size_t at = 0;
  if (v.have_pos) {
    char la[20], lo[20];
    em_format_ddm(v.lat, true, la, sizeof la);
    em_format_ddm(v.lon, false, lo, sizeof lo);
    at = (size_t)snprintf(foot, sizeof foot, "%s %s | fix %d s old", la, lo, (int)(v.pos_age_ms / 1000));
  } else {
    at = (size_t)snprintf(foot, sizeof foot, "NO POSITION");
  }
  if (mmsi[0] && at < sizeof foot) snprintf(foot + at, sizeof foot - at, " | MMSI %s", mmsi);
  set_label(w.footer, foot);
  lv_obj_set_style_text_color(w.footer, v.have_pos ? ui_theme()->dim : ui_theme()->fault_label, 0);

  set_label(w.hint, avail ? "HOLD 1 s TO SEND" : "SIGNAL K OFFLINE - BUTTONS UNAVAILABLE");
  for (int i = 0; i < B_CANCEL; i++) {
    show(w.btn[i], avail);
    bool declared = s.nature == NATURE_OF[i];   // filled; not LV_STATE_CHECKED, which the default theme styles itself
    lv_obj_set_style_bg_color(w.btn[i], declared ? ui_theme()->alarm : ui_theme()->strip_bg, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(w.btn[i], 1), declared ? ui_theme()->bg : ui_theme()->fg, 0);   // night is one red: dark on the fill
  }
  show(w.btn[B_CANCEL], avail && s.nature != EM_NONE);
}
