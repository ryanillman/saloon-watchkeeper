#include "depth_state.h"
#include "depth_page.h"

// Three depths, in the order the Depth role prefers them (depth_pick); they are never mixed and the one
// shown is always named. A sounder sends continuously, so silence is a fault.
static const role_slot_t depth_slots[] = {
  {"transducer", "belowTransducer", NULL, Q_DEPTH, 0, 0, DEPTH_MAX_AGE_MS, "DEPTH", 0},
  {"keel",       "belowKeel",       NULL, Q_DEPTH, 0, 0, DEPTH_MAX_AGE_MS, "DEPTH", 0},
  {"surface",    "belowSurface",    NULL, Q_DEPTH, 0, 0, DEPTH_MAX_AGE_MS, "DEPTH", 0},
};

// Tide comes from a tide plugin and is only as good as it is. Ages are judged in depth_page.c, because a
// missing or stale tide must not fault the role.
static const role_slot_t tide_slots[] = {
  {"height",   "heightNow",  NULL, Q_DEPTH, 0, 0, 0, "TIDE", 0},
  {"high",     "heightHigh", NULL, Q_DEPTH, 0, 0, 0, "HIGH", 0},
  {"low",      "heightLow",  NULL, Q_DEPTH, 0, 0, 0, "LOW", 0},
  {"timeHigh", "timeHigh",   NULL, Q_NONE,  0, 0, 0, "HIGH", 0},
  {"timeLow",  "timeLow",    NULL, Q_NONE,  0, 0, 0, "LOW", 0},
};

#define N(a) ((uint8_t)(sizeof a / sizeof a[0]))
// Both are fixed path sets (id_depth 0): there is one sea under the boat.
static const role_group_t groups[] = {
  {"depth", "environment.depth", 0, 1, depth_slots, N(depth_slots)},
  {"tide",  "environment.tide",  0, 1, tide_slots,  N(tide_slots)},
};

const role_def_t role_depth = {
  .name = "depth",
  .title = "DEPTH",
  .groups = groups,
  .group_count = N(groups),
  .page_count = 1,
  .build_page = depth_build_page,
  .refresh = depth_refresh,
  .tile = depth_tile,
};
