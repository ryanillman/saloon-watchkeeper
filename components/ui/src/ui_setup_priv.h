#pragma once
// What ui_setup.c shares with the setup screens in their own files (ui_setup_power.c). Not a public header.
#include "ui_setup.h"

cfg_t *setup_work(void);                       // the edited copy
const ui_setup_cb_t *setup_cb(void);
bool setup_wide(void);
lv_obj_t *setup_body(void);
lv_obj_t *setup_button(lv_obj_t *parent, const char *text, lv_event_cb_t fn, void *arg);
lv_obj_t *setup_row(lv_obj_t *parent);
lv_obj_t *setup_text(lv_obj_t *parent, const char *t, lv_color_t col);
void setup_part(const char *name, lv_obj_t *o);
// The on-screen keyboard over the page: `done` gets the text when OK is pressed.
void setup_kb(const char *prompt, const char *initial, uint32_t max_len, void (*done)(const char *, uintptr_t), uintptr_t arg);
// A choice menu over the page (what a dropdown is here): `done` gets the index picked; a tap outside closes it.
void setup_menu(const char *title, const char *const *options, uint8_t n, uint8_t selected,
                void (*done)(uint32_t sel, uintptr_t arg), uintptr_t arg);
void setup_rebuild(void);                      // rebuild the current screen after the running event

// Setup > Power (ui_setup_power.c)
void setup_build_power(void);
bool setup_power_can_save(void);               // false, with the reason on the page, while the edited topology is invalid
void setup_power_tick(void);                   // rebuilds the page when what the display has seen changed
