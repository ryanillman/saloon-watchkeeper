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
  void *ctx;
} ui_role_view_t;

// Builds the shell on the display's active screen. `roles` must outlive the shell.
void ui_shell_create(lv_display_t *disp, size_class_t sc, palette_t pal,
                     const ui_role_view_t *roles, uint8_t role_count, uint8_t start_role);

// Moves between roles and pages. Used by gestures, keys, and snapshot runs.
void ui_shell_move(nav_dir_t dir);

void ui_shell_set_palette(palette_t pal);

// Current position, for snapshot file names.
const ui_nav_t *ui_shell_nav(void);
