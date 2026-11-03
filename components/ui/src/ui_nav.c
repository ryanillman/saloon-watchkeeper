#include <string.h>
#include "ui_nav.h"

void ui_nav_init(ui_nav_t *n, uint8_t role_count, const uint8_t *page_counts, uint8_t start_role) {
  memset(n, 0, sizeof *n);
  if (role_count > UI_MAX_ROLES) role_count = UI_MAX_ROLES;
  n->role_count = role_count;
  for (uint8_t i = 0; i < role_count; i++) n->page_count[i] = page_counts[i] ? page_counts[i] : 1;
  n->role = start_role < role_count ? start_role : 0;
}

void ui_nav_set_pages(ui_nav_t *n, const uint8_t *page_counts) {
  for (uint8_t i = 0; i < n->role_count; i++) n->page_count[i] = page_counts[i] ? page_counts[i] : 1;
  if (n->role_count && n->page >= n->page_count[n->role]) n->page = (uint8_t)(n->page_count[n->role] - 1);
}

bool ui_nav_move(ui_nav_t *n, nav_dir_t dir) {
  if (n->role_count == 0) return false;
  uint8_t pages = n->page_count[n->role];

  switch (dir) {
    case NAV_LEFT:   // finger moves left: next role comes in from the right
    case NAV_RIGHT:
      if (n->role_count == 1) return false;
      n->role = dir == NAV_LEFT ? (n->role + 1) % n->role_count
                                : (n->role + n->role_count - 1) % n->role_count;
      n->page = 0;
      return true;
    case NAV_UP:
    case NAV_DOWN:
      if (pages <= 1) return false;
      n->page = dir == NAV_UP ? (n->page + 1) % pages : (n->page + pages - 1) % pages;
      return true;
  }
  return false;
}
