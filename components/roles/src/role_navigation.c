#include "navigation_page.h"

// The page is in page_navigation.c. Until it exists these keep the role registered and drawable.
void navigation_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b) {
  (void)area; (void)page; (void)sc; (void)b;
}
void navigation_refresh(role_binding_t *b, int64_t now_ms) {
  (void)b; (void)now_ms;
}
void navigation_tile(const role_binding_t *b, int64_t now_ms, glance_tile_t *out) {
  (void)now_ms;
  out->title = b->def->title;
}

const role_def_t role_navigation = {
  .name = "navigation",
  .title = "NAVIGATION",
  .groups = NULL,
  .group_count = 0,
  .page_count = 1,
  .build_page = navigation_build_page,
  .refresh = navigation_refresh,
  .tile = navigation_tile,
};
