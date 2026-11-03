#pragma once
#include <stdbool.h>
#include <stdint.h>

#define UI_MAX_ROLES 8

typedef enum { NAV_LEFT, NAV_RIGHT, NAV_UP, NAV_DOWN } nav_dir_t;

typedef struct {
  uint8_t role_count;
  uint8_t page_count[UI_MAX_ROLES];
  uint8_t role;
  uint8_t page;
} ui_nav_t;

void ui_nav_init(ui_nav_t *n, uint8_t role_count, const uint8_t *page_counts, uint8_t start_role);
bool ui_nav_move(ui_nav_t *n, nav_dir_t dir);
