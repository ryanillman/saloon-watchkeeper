#pragma once
// Quick panel from the top strip: palette (DAY, DUSK, NIGHT, AUTO) and brightness.
// Pure presentation: the app owns the values and receives the choices through callbacks.
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

typedef struct {
  void (*on_palette)(const char *choice);   // "day" | "dusk" | "night" | "auto"
  void (*on_brightness)(uint8_t percent);
} ui_quick_cb_t;

// Opens the panel on `screen`, above everything else on it (an alarm banner included),
// below the top strip. `dims` false: the backlight is on/off only, so the panel shows a
// fixed ON instead of the slider. `cb` must outlive the panel.
void ui_quick_open(lv_obj_t *screen, const char *current_choice, uint8_t brightness, bool dims,
                   const ui_quick_cb_t *cb);
void ui_quick_close(void);
bool ui_quick_is_open(void);

typedef enum {
  UI_QUICK_DAY, UI_QUICK_DUSK, UI_QUICK_NIGHT, UI_QUICK_AUTO,
  UI_QUICK_SLIDER,    // NULL when the backlight does not dim
  UI_QUICK_ON_LABEL,  // NULL when it does
  UI_QUICK_CLOSE,
} ui_quick_part_t;
lv_obj_t *ui_quick_part(ui_quick_part_t p);   // for tests; NULL when closed
