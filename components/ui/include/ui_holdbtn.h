#pragma once
// A button that acts only after being held for UI_HOLD_MS: a fill grows across it while the finger stays down, and
// letting go (or sliding off) before the end does nothing. A tap never acts. Used for the emergency buttons, where a
// stray touch must not raise an alarm.
#include <stdbool.h>
#include "lvgl.h"

#define UI_HOLD_MS 1000

typedef void (*ui_holdbtn_fn)(void *ctx);

// `on_held` runs once per hold, on the LVGL task, the moment the hold completes (the finger may still be down).
lv_obj_t *ui_holdbtn_create(lv_obj_t *parent, const char *text, const lv_font_t *font, int32_t w, int32_t h, ui_holdbtn_fn on_held,
                            void *ctx);
// Fills the button in the alarm colour (this nature is the one declared). The label flips to the dark colour: night is one
// red, so red text on a red fill would vanish.
void ui_holdbtn_set_declared(lv_obj_t *btn, bool declared);

// Re-applies the current palette (a panel that stays open while the palette changes), keeping the declared state.
void ui_holdbtn_restyle(lv_obj_t *btn);

lv_obj_t *ui_holdbtn_fill(lv_obj_t *btn);    // the progress fill, for tests
lv_obj_t *ui_holdbtn_label(lv_obj_t *btn);
