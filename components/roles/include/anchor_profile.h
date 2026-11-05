#pragma once
// What each supported Signal K anchor-alarm plugin wants for drop, radius and raise. Pure: no I/O. The Anchor
// page builds a command here; app.c hands it to the command queue (sk_cmd), so roles does not depend on source_sk.
#include <stdbool.h>

typedef enum { ANCHOR_PLUGIN_HOEKEN, ANCHOR_PLUGIN_ANCHORALARM } anchor_plugin_t;
typedef enum { ANCHOR_CMD_DROP, ANCHOR_CMD_RADIUS, ANCHOR_CMD_RAISE } anchor_cmd_kind_t;

// put_number: a Signal K PUT of `value` to `path` (dotted, relative to vessels.self). Otherwise a POST of `body`
// (JSON, "" for none) to the server path `path`.
typedef struct {
  bool put_number;
  char path[96];
  char body[96];
  double value;
} anchor_cmd_t;

// The config names "hoekens-anchor-alarm" and "anchoralarm". False for anything else, leaving *out untouched.
bool anchor_plugin_from_name(const char *name, anchor_plugin_t *out);

// radius_m is used by drop and radius, ignored by raise.
void anchor_build_cmd(anchor_plugin_t p, anchor_cmd_kind_t k, double radius_m, anchor_cmd_t *out);
