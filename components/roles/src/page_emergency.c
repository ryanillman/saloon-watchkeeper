// The emergency script page. What is declared lives in emergency.c and the words in em_script_text; this draws them.
#include <stdio.h>
#include <string.h>
#include "emergency_page.h"
#include "nav_state.h"

static const char *(*vessel_name)(void);

static struct {
  lv_obj_t *send, *steps, *script;
  nav_ids_t ids;
  bool built;
  int32_t room;   // the height the call may take: from its top to the foot of the page
} w;

void emergency_page_set_vessel_name(const char *(*fn)(void)) { vessel_name = fn; }

// For someone who has never used the radio.
#define STEPS_TEXT "1. On the VHF radio, press and hold the red DISTRESS button for 3 seconds.\n2. Then speak into the microphone:"
#define NOTHING_TEXT "No emergency declared. Tap the top strip, then hold MOB, FIRE or SINKING."

static lv_obj_t *make_label(lv_obj_t *area, const lv_font_t *f, lv_color_t col, int32_t x, int32_t y, int32_t width) {
  lv_obj_t *l = lv_label_create(area);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, col, 0);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_width(l, width);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_label_set_text(l, "");
  return l;
}

void emergency_page_reset(void) { memset(&w, 0, sizeof w); }

void emergency_page_build(lv_obj_t *area, size_class_t sc, role_binding_t *b) {
  (void)b;
  (void)sc;
  emergency_page_reset();
  nav_ids_bind(&w.ids);
  lv_obj_update_layout(area);
  int32_t W = lv_obj_get_width(area), H = lv_obj_get_height(area), pad = 8;
  const ui_colors_t *c = ui_theme();

  w.send = make_label(area, &lv_font_montserrat_28, c->fg, pad, pad, W - 2 * pad);   // the status of this display's own press
  int32_t steps_y = pad + lv_font_get_line_height(&lv_font_montserrat_28) + 4;
  w.steps = make_label(area, &lv_font_montserrat_20, c->fg, pad, steps_y, W - 2 * pad);
  lv_label_set_text(w.steps, STEPS_TEXT);
  lv_obj_update_layout(area);
  int32_t script_y = steps_y + lv_obj_get_height(w.steps) + 8;
  w.script = make_label(area, &lv_font_montserrat_48, c->fg, pad, script_y, W - 2 * pad);
  w.room = H - pad - script_y;
  w.built = true;
}

lv_obj_t *emergency_page_part(const char *name) {
  if (!w.built) return NULL;
  if (!strcmp(name, "send")) return w.send;
  if (!strcmp(name, "steps")) return w.steps;
  if (!strcmp(name, "script")) return w.script;
  return NULL;
}

static void show(lv_obj_t *o, bool on) {
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void emergency_page_refresh(role_binding_t *b, int64_t now_ms) {
  (void)b;
  if (!w.built) return;
  em_state_t s;
  em_snapshot(&s);

  // Only this display's own press has a send status; something raised elsewhere has nothing to say about sending.
  const char *send = "";
  switch (s.send) {
    case EM_SENDING: send = "SENDING..."; break;
    case EM_SENT: send = "SENT"; break;
    case EM_NOT_SENT: send = "NOT SENT - TELL HELM"; break;
    default: break;
  }
  if (strcmp(lv_label_get_text(w.send), send)) lv_label_set_text(w.send, send);
  lv_obj_set_style_text_color(w.send, s.send == EM_NOT_SENT ? ui_theme()->alarm : ui_theme()->fg, 0);

  nav_view_t v;
  nav_view_build(&w.ids, now_ms, &v);
  char call[600];
  const char *name = vessel_name ? vessel_name() : "";
  if (s.nature == EM_NONE || !em_script_text(s.nature, name, v.have_pos, v.lat, v.lon, call, sizeof call)) snprintf(call, sizeof call, NOTHING_TEXT);
  show(w.steps, s.nature != EM_NONE);

  // The call, in the largest text that fits the page (pages must not scroll), never below 20 pt.
  if (strcmp(lv_label_get_text(w.script), call)) {
    static const lv_font_t *const FIT[] = {&lv_font_montserrat_48, &lv_font_montserrat_36, &lv_font_montserrat_28, &lv_font_montserrat_24,
                                           &lv_font_montserrat_20};
    lv_label_set_text(w.script, call);
    for (size_t i = 0; i < sizeof FIT / sizeof FIT[0]; i++) {
      lv_obj_set_style_text_font(w.script, FIT[i], 0);
      lv_obj_update_layout(w.script);
      if (lv_obj_get_height(w.script) <= w.room) break;
    }
  }
}
