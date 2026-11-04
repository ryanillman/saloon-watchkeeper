#pragma once
#include "lvgl.h"
#include "ui_nav.h"
#include "ui_theme.h"

// A role's pages. build_page fills `area` and must not rely on scrolling:
// the shell turns scrolling off on everything it builds, so swipes always
// change role or page.
typedef struct {
  const char *name;          // shown in the top strip, e.g. "ENGINE"
  uint8_t page_count;
  void (*build_page)(lv_obj_t *area, uint8_t page, size_class_t sc, void *ctx);
  void (*refresh)(void *ctx);   // updates the visible page's widgets; may be NULL
  void *ctx;
} ui_role_view_t;

// Builds the shell on the display's active screen. `roles` must outlive the shell.
void ui_shell_create(lv_display_t *disp, size_class_t sc, palette_t pal,
                     const ui_role_view_t *roles, uint8_t role_count, uint8_t start_role);

// Moves between roles and pages. Used by gestures, keys, and snapshot runs.
void ui_shell_move(nav_dir_t dir);

void ui_shell_set_palette(palette_t pal);

// Rebuilds the visible page (after a role rebinds).
void ui_shell_rebuild(void);

// The number of roles changed (setup added or removed one). `roles` passed to
// ui_shell_create now holds role_count entries; the current role is kept if it still exists.
void ui_shell_set_roles(uint8_t role_count);

// Calls the visible role's refresh. The app calls this about 10 times a second.
void ui_shell_refresh(void);

// Source status at the right of the top strip, e.g. "SK LIVE".
void ui_shell_set_status(const char *text, lv_color_t color);

// Current position, for snapshot file names.
const ui_nav_t *ui_shell_nav(void);

// GO from an alarm: that role, on `page` (clamped to the role's last page).
void ui_shell_goto(uint8_t role, uint8_t page);

// The top strip and its status text, for the alert marker.
lv_obj_t *ui_shell_strip(void);
lv_obj_t *ui_shell_status(void);
lv_obj_t *ui_shell_dots(void);

// A tap on the left part of the top strip (the role name) calls `fn`; NULL clears it.
// The alert marker on the right keeps its own tap. The object is invisible, for tests.
void ui_shell_set_strip_tap(void (*fn)(void));
lv_obj_t *ui_shell_strip_tap(void);

// Shows `text` in the status label in place of the app's status until called with NULL.
void ui_shell_set_status_override(const char *text, lv_color_t color);
