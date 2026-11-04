#include "ui_quick.h"
#include <string.h>
#include "ui_shell.h"

#define BRIGHT_MIN 10   // a black screen cannot be undone from the screen

static const char *const NAMES[] = {"day", "dusk", "night", "auto"};
static const char *const LABELS[] = {"DAY", "DUSK", "NIGHT", "AUTO"};

static struct {
  lv_obj_t *root, *card, *btn[4], *slider, *on, *close, *setup, *bright_label, *buzzer;
  const ui_quick_cb_t *cb;
  uint8_t sel;
} q;

static bool open_now(void) { return q.root && lv_obj_is_valid(q.root); }

static void style(void) {
  const ui_colors_t *c = ui_theme();
  lv_obj_set_style_bg_color(q.card, c->strip_bg, 0);
  lv_obj_set_style_border_color(q.card, c->fg, 0);
  lv_obj_set_style_text_color(q.card, c->fg, 0);
  for (int i = 0; i < 4; i++) {
    bool sel = i == q.sel;
    lv_obj_set_style_bg_color(q.btn[i], sel ? c->fg : c->strip_bg, 0);
    lv_obj_set_style_border_color(q.btn[i], sel ? c->fg : c->dim, 0);
    lv_obj_set_style_text_color(q.btn[i], sel ? c->bg : c->fg, 0);
  }
  lv_obj_t *plain[] = {q.close, q.setup};
  for (int i = 0; i < 2; i++) {
    if (!plain[i]) continue;
    lv_obj_set_style_bg_color(plain[i], c->strip_bg, 0);
    lv_obj_set_style_border_color(plain[i], c->dim, 0);
    lv_obj_set_style_text_color(plain[i], c->fg, 0);
  }
  if (q.slider) {
    lv_obj_set_style_bg_color(q.slider, c->dim, LV_PART_MAIN);
    lv_obj_set_style_bg_color(q.slider, c->accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(q.slider, c->fg, LV_PART_KNOB);
  }
}

static void on_choice(lv_event_t *e) {
  int i = (int)(uintptr_t)lv_event_get_user_data(e);
  q.sel = (uint8_t)i;
  if (q.cb && q.cb->on_palette) q.cb->on_palette(NAMES[i]);   // the app applies it; the panel then follows the new palette
  if (open_now()) style();
}

static void on_slider(lv_event_t *e) {
  if (q.cb && q.cb->on_brightness) q.cb->on_brightness((uint8_t)lv_slider_get_value(lv_event_get_target(e)));
}

static void on_close(lv_event_t *e) { (void)e; ui_quick_close(); }
static void on_buzzer(lv_event_t *e) {   // the switch flips itself; report what it shows
  if (q.cb && q.cb->on_buzzer) q.cb->on_buzzer(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}
static void on_setup(lv_event_t *e) {
  (void)e;
  if (q.cb && q.cb->on_setup) q.cb->on_setup();
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int32_t h) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(b, h);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
  lv_obj_center(l);
  return b;
}

void ui_quick_open(lv_obj_t *screen, const char *choice, uint8_t brightness, bool dims, const ui_quick_cb_t *cb) {
  if (open_now()) return;
  memset(&q, 0, sizeof q);
  q.cb = cb;
  q.sel = 0;
  for (int i = 0; i < 4; i++)
    if (choice && !strcmp(choice, NAMES[i])) q.sel = (uint8_t)i;

  lv_obj_update_layout(screen);
  int32_t w = lv_obj_get_width(screen), h = lv_obj_get_height(screen);
  lv_obj_t *strip = ui_shell_strip();
  int32_t top = strip ? lv_obj_get_height(strip) : 0;

  // A transparent scrim under the card, below the strip so the strip (and the alert marker)
  // stays live. It is created last, so it sits above the banner and the alert list.
  q.root = lv_obj_create(screen);
  lv_obj_remove_style_all(q.root);
  lv_obj_set_size(q.root, w, h - top);
  lv_obj_set_pos(q.root, 0, top);
  lv_obj_add_flag(q.root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(q.root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(q.root, on_close, LV_EVENT_CLICKED, NULL);

  q.card = lv_obj_create(q.root);
  lv_obj_remove_style_all(q.card);
  int32_t cw = w >= 700 ? 560 : w - 16;
  lv_obj_set_size(q.card, cw, LV_SIZE_CONTENT);
  lv_obj_set_pos(q.card, (w - cw) / 2, 8);
  lv_obj_set_style_bg_opa(q.card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(q.card, 2, 0);
  lv_obj_set_style_radius(q.card, 8, 0);
  lv_obj_set_style_pad_all(q.card, 12, 0);
  lv_obj_set_style_pad_row(q.card, 12, 0);
  lv_obj_set_flex_flow(q.card, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(q.card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(q.card, LV_OBJ_FLAG_CLICKABLE);   // taps on the card's padding do not close it

  lv_obj_t *row = lv_obj_create(q.card);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  for (int i = 0; i < 4; i++) {
    q.btn[i] = button(row, LABELS[i], 56);
    lv_obj_set_flex_grow(q.btn[i], 1);
    lv_obj_add_event_cb(q.btn[i], on_choice, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
  }

  lv_obj_t *brow = lv_obj_create(q.card);
  lv_obj_remove_style_all(brow);
  lv_obj_set_size(brow, lv_pct(100), 40);
  lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(brow, 16, 0);
  lv_obj_remove_flag(brow, LV_OBJ_FLAG_SCROLLABLE);
  q.bright_label = lv_label_create(brow);
  lv_label_set_text(q.bright_label, w >= 700 ? "BRIGHTNESS" : "BRIGHT");
  lv_obj_set_style_text_font(q.bright_label, &lv_font_montserrat_20, 0);
  if (dims) {
    q.slider = lv_slider_create(brow);
    lv_slider_set_range(q.slider, BRIGHT_MIN, 100);
    lv_slider_set_value(q.slider, brightness < BRIGHT_MIN ? BRIGHT_MIN : brightness, LV_ANIM_OFF);
    lv_obj_set_height(q.slider, 16);
    lv_obj_set_flex_grow(q.slider, 1);
    lv_obj_set_ext_click_area(q.slider, 16);
    lv_obj_add_event_cb(q.slider, on_slider, LV_EVENT_VALUE_CHANGED, NULL);
  } else {
    q.on = lv_label_create(brow);
    lv_label_set_text(q.on, "ON");
    lv_obj_set_style_text_font(q.on, &lv_font_montserrat_20, 0);
  }

  if (cb && cb->buzzer_state) {
    const ui_colors_t *c = ui_theme();
    lv_obj_t *zrow = lv_obj_create(q.card);
    lv_obj_remove_style_all(zrow);
    lv_obj_set_size(zrow, lv_pct(100), 44);
    lv_obj_set_flex_flow(zrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(zrow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(zrow, 16, 0);
    lv_obj_remove_flag(zrow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *zl = lv_label_create(zrow);
    lv_label_set_text(zl, "BUZZER");
    lv_obj_set_style_text_font(zl, &lv_font_montserrat_20, 0);
    q.buzzer = lv_switch_create(zrow);
    lv_obj_set_size(q.buzzer, 72, 40);
    if (cb->buzzer_state()) lv_obj_add_state(q.buzzer, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(q.buzzer, c->dim, LV_PART_MAIN);
    lv_obj_set_style_bg_color(q.buzzer, c->accent, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(q.buzzer, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(q.buzzer, c->fg, LV_PART_KNOB);
    lv_obj_add_event_cb(q.buzzer, on_buzzer, LV_EVENT_VALUE_CHANGED, NULL);
  }

  lv_obj_t *crow = lv_obj_create(q.card);
  lv_obj_remove_style_all(crow);
  lv_obj_set_size(crow, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(crow, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(crow, 8, 0);
  lv_obj_remove_flag(crow, LV_OBJ_FLAG_SCROLLABLE);
  if (cb && cb->on_setup) {
    q.setup = button(crow, "SETUP", 48);
    lv_obj_set_flex_grow(q.setup, 1);
    lv_obj_add_event_cb(q.setup, on_setup, LV_EVENT_CLICKED, NULL);
  }
  q.close = button(crow, "CLOSE", 48);
  lv_obj_set_flex_grow(q.close, 1);
  lv_obj_add_event_cb(q.close, on_close, LV_EVENT_CLICKED, NULL);
  style();
}

void ui_quick_close(void) {
  if (!open_now()) return;
  lv_obj_delete_async(q.root);   // may be called from an event on one of its own children
  memset(&q, 0, sizeof q);
}

bool ui_quick_is_open(void) { return open_now(); }

lv_obj_t *ui_quick_part(ui_quick_part_t p) {
  if (!open_now()) return NULL;
  switch (p) {
    case UI_QUICK_DAY: case UI_QUICK_DUSK: case UI_QUICK_NIGHT: case UI_QUICK_AUTO: return q.btn[p];
    case UI_QUICK_SLIDER: return q.slider;
    case UI_QUICK_ON_LABEL: return q.on;
    case UI_QUICK_CLOSE: return q.close;
    case UI_QUICK_SETUP: return q.setup;
    case UI_QUICK_BUZZER: return q.buzzer;
  }
  return NULL;
}
