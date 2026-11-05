#include "anchor_profile.h"
#include <stdio.h>
#include <string.h>

bool anchor_plugin_from_name(const char *name, anchor_plugin_t *out) {
  if (!name) return false;
  if (!strcmp(name, "hoekens-anchor-alarm")) *out = ANCHOR_PLUGIN_HOEKEN;
  else if (!strcmp(name, "anchoralarm")) *out = ANCHOR_PLUGIN_ANCHORALARM;
  else return false;
  return true;
}

void anchor_build_cmd(anchor_plugin_t p, anchor_cmd_kind_t k, double radius_m, anchor_cmd_t *out) {
  memset(out, 0, sizeof *out);
  const char *base = p == ANCHOR_PLUGIN_HOEKEN ? "/plugins/hoekens-anchor-alarm/" : "/plugins/anchoralarm/";
  if (k == ANCHOR_CMD_RAISE) {
    snprintf(out->path, sizeof out->path, "%sraiseAnchor", base);
    return;
  }
  if (p == ANCHOR_PLUGIN_ANCHORALARM && k == ANCHOR_CMD_RADIUS) {   // its radius is a spec path, set by PUT
    out->put_number = true;
    snprintf(out->path, sizeof out->path, "navigation.anchor.maxRadius");
    out->value = radius_m;
    return;
  }
  // Only Hoeken's radius reaches here (anchoralarm's returned above): it moves the watch zone.
  snprintf(out->path, sizeof out->path, "%s%s", base, k == ANCHOR_CMD_DROP ? "dropAnchor" : "setZone");
  if (p == ANCHOR_PLUGIN_HOEKEN)
    snprintf(out->body, sizeof out->body, "{\"zone\":{\"type\":\"circle\",\"radius\":%g}}", radius_m);
  else
    snprintf(out->body, sizeof out->body, "{\"radius\":%g}", radius_m);
}
