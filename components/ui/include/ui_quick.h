#pragma once
// Quick panel from the top strip: palette (DAY, DUSK, NIGHT, AUTO), brightness, the buzzer,
// RESET SCREEN, and SETUP.
// Pure presentation: the app owns the values and receives the choices through callbacks.
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

typedef struct {
  void (*on_palette)(const char *choice);   // "day" | "dusk" | "night" | "auto"
  void (*on_brightness)(uint8_t percent);
  void (*on_setup)(void);   // SETUP tapped; NULL hides the button
  void (*on_buzzer)(bool on);
  bool (*buzzer_state)(void);   // NULL: the board has no buzzer, so no switch
  void (*on_reset_screen)(void);   // RESET SCREEN tapped (the panel closes first); NULL hides the button
  // The emergency row, at the bottom below a gap and a divider: MOB, FIRE and SINKING, and CANCEL while something is
  // declared. Each is hold-to-confirm (UI_HOLD_MS). Shown only when `emergency_available` is set and returns true when the
  // panel opens. A completed hold closes the panel, then calls `on_emergency` with a UI_QUICK_EM_* value.
  bool (*emergency_available)(void);
  int (*emergency_declared)(void);   // UI_QUICK_EM_NONE, or what is declared: its button is filled; any declared shows CANCEL
  void (*on_emergency)(int which);
} ui_quick_cb_t;

#define UI_QUICK_EM_NONE (-1)
enum { UI_QUICK_EM_MOB, UI_QUICK_EM_FIRE, UI_QUICK_EM_SINKING,
       UI_QUICK_EM_OTHER,    // only from emergency_declared: flooding or another nature raised elsewhere, no button here
       UI_QUICK_EM_CANCEL }; // only to on_emergency

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
  UI_QUICK_SETUP,     // NULL when on_setup is NULL
  UI_QUICK_BUZZER,    // NULL when the board has no buzzer
  UI_QUICK_RESET,     // NULL when on_reset_screen is NULL
  UI_QUICK_SEPARATOR, UI_QUICK_EM_LABEL, UI_QUICK_BTN_MOB, UI_QUICK_BTN_FIRE, UI_QUICK_BTN_SINKING, UI_QUICK_BTN_CANCEL,   // NULL without the emergency row
} ui_quick_part_t;
lv_obj_t *ui_quick_part(ui_quick_part_t p);   // for tests; NULL when closed
