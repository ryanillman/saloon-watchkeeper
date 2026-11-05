#pragma once
// The Glance page: one tile per role that has one. The app tells it which roles (glance_set_sources); the
// page asks each for its tile and draws state, headline, trend and context. It has no role-specific code.
#include "role.h"

// A role with a tile, and where it sits in the shell (a tap on its tile opens that role).
typedef struct {
  const role_binding_t *b;
  uint8_t role_index;
} glance_source_t;

#define GLANCE_MAX_TILES 6

// Copies `src`. The app calls it whenever roles are bound. A page already showing is not redrawn until its next refresh.
void glance_set_sources(const glance_source_t *src, uint8_t n);

// Which of `n` tiles fit in `max`: writes their indices in order to `out` and returns how many. While there are
// too many the lowest-priority tile that is not alerting goes first (priority: anchor, depth, navigation, power, engine,
// tanks, then any other role); only when every tile is alerting does an alerting one go, and an ALARM is last of all.
size_t glance_pick(const char *const *role_names, const gt_state_t *states, size_t n, size_t max, size_t *out);

// For tests: the parts of the built page.
typedef enum { GP_TILE, GP_EDGE, GP_TITLE, GP_WORD, GP_HEADLINE, GP_UNIT, GP_CONTEXT0, GP_CONTEXT1, GP_FAULT, GP_CHART, GP_SWING } glance_part_t;
uint8_t glance_tile_count(void);                              // tiles on the built page
lv_obj_t *glance_part(uint8_t tile, glance_part_t part);      // NULL when this board or this tile has no such part
uint16_t glance_trend_points(uint8_t tile);                   // points the tile's trend currently plots (0 = none)
lv_obj_t *glance_bar_pct(uint8_t tile, uint8_t bar);          // a tank bar's percent label, NULL if none
