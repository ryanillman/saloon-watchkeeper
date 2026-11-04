#include "role.h"

#include "tanks.h"

// Levels are ratios; a steady level is healthy, so no slot has a max age.
static const role_slot_t slots[] = {
  {"level",    "currentLevel",  NULL, Q_RATIO,  0, 1, 0, "LEVEL", 0},
  {"volume",   "currentVolume", NULL, Q_VOLUME, 0, 0, 0, "VOLUME", 0},
  {"capacity", "capacity",      NULL, Q_VOLUME, 0, 0, 0, "CAPACITY", 0},
};

static const role_group_t groups[] = {
  {"tank", "tanks", 2, 8, slots, sizeof slots / sizeof slots[0]},
};

// Pages live in page_tanks.c.
void tanks_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b);
void tanks_refresh(role_binding_t *b, int64_t now_ms);
uint8_t tanks_pages(const role_binding_t *b, size_class_t sc);
void tanks_tile(const role_binding_t *b, int64_t now_ms, glance_tile_t *out);

const role_def_t role_tanks = {
  .name = "tanks",
  .title = "TANKS",
  .groups = groups,
  .group_count = 1,
  .page_count = 1,
  .pages = tanks_pages,
  .page_for = tanks_page_for,
  .build_page = tanks_build_page,
  .refresh = tanks_refresh,
  .tile = tanks_tile,
};
