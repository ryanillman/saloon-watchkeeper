#include "ui_holdbtn.h"
#include "ui_theme.h"

typedef struct {
  ui_holdbtn_fn on_held;
  void *ctx;
  lv_obj_t *btn, *fill, *label;
  lv_timer_t *timer;
  uint32_t start;
  bool fired;
} hold_t;

static void end_hold(hold_t *h) {
  if (h->timer) {
    lv_timer_delete(h->timer);
    h->timer = NULL;
  }
  if (h->fill) lv_obj_set_width(h->fill, 0);
  h->fired = false;
}

static void tick(lv_timer_t *t) {
  hold_t *h = lv_timer_get_user_data(t);
  uint32_t ms = lv_tick_elaps(h->start);
  int32_t full = lv_obj_get_width(h->btn);
  lv_obj_set_width(h->fill, ms >= UI_HOLD_MS ? full : (int32_t)((uint64_t)full * ms / UI_HOLD_MS));
  if (ms < UI_HOLD_MS || h->fired) return;
  h->fired = true;   // once per hold, however long the finger stays
  if (h->on_held) h->on_held(h->ctx);
}

static void on_pressed(lv_event_t *e) {
  hold_t *h = lv_event_get_user_data(e);
  end_hold(h);
  h->start = lv_tick_get();
  h->timer = lv_timer_create(tick, 30, h);
}

static void on_released(lv_event_t *e) { end_hold(lv_event_get_user_data(e)); }   // early, or the finger left: nothing is sent

static void on_delete(lv_event_t *e) {
  hold_t *h = lv_event_get_user_data(e);
  if (h->timer) lv_timer_delete(h->timer);   // the panel closed mid-hold
  lv_free(h);
}

lv_obj_t *ui_holdbtn_create(lv_obj_t *parent, const char *text, const lv_font_t *font, int32_t w, int32_t h, ui_holdbtn_fn on_held,
                            void *ctx) {
  const ui_colors_t *c = ui_theme();
  hold_t *s = lv_malloc_zeroed(sizeof *s);
  s->on_held = on_held;
  s->ctx = ctx;
  lv_obj_t *b = lv_button_create(parent);
  s->btn = b;
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_border_color(b, c->fg, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, c->strip_bg, 0);
  lv_obj_set_style_clip_corner(b, true, 0);
  s->fill = lv_obj_create(b);   // grows left to right while the button is held
  lv_obj_remove_style_all(s->fill);
  lv_obj_set_pos(s->fill, 0, 0);
  lv_obj_set_size(s->fill, 0, h);
  lv_obj_set_style_bg_color(s->fill, c->accent, 0);
  lv_obj_set_style_bg_opa(s->fill, LV_OPA_60, 0);
  lv_obj_remove_flag(s->fill, LV_OBJ_FLAG_CLICKABLE);
  s->label = lv_label_create(b);
  lv_label_set_text(s->label, text);
  lv_obj_set_style_text_font(s->label, font, 0);
  lv_obj_set_style_text_color(s->label, c->fg, 0);
  lv_obj_center(s->label);
  lv_obj_add_event_cb(b, on_pressed, LV_EVENT_PRESSED, s);
  lv_obj_add_event_cb(b, on_released, LV_EVENT_RELEASED, s);
  lv_obj_add_event_cb(b, on_released, LV_EVENT_PRESS_LOST, s);
  lv_obj_add_event_cb(b, on_delete, LV_EVENT_DELETE, s);
  return b;
}

static hold_t *state_of(lv_obj_t *btn) {
  uint32_t n = lv_obj_get_event_count(btn);
  for (uint32_t i = 0; i < n; i++) {
    lv_event_dsc_t *d = lv_obj_get_event_dsc(btn, i);
    if (lv_event_dsc_get_cb(d) == on_pressed) return lv_event_dsc_get_user_data(d);
  }
  return NULL;
}

void ui_holdbtn_set_declared(lv_obj_t *btn, bool declared) {
  const ui_colors_t *c = ui_theme();
  hold_t *h = state_of(btn);
  lv_obj_set_style_bg_color(btn, declared ? c->alarm : c->strip_bg, 0);
  if (h) lv_obj_set_style_text_color(h->label, declared ? c->bg : c->fg, 0);
}

lv_obj_t *ui_holdbtn_fill(lv_obj_t *btn) { hold_t *h = state_of(btn); return h ? h->fill : NULL; }
lv_obj_t *ui_holdbtn_label(lv_obj_t *btn) { hold_t *h = state_of(btn); return h ? h->label : NULL; }
