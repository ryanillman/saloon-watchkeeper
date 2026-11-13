// What Setup > Power lists: the endpoints the resolver has published, read from their leaves. No binding is needed, so a
// device the role has not bound yet (or an endpoint without a NAME) still shows.
#include <stdio.h>
#include <string.h>
#include "power_page.h"

#define MAX_DEV 24

static bool text_at(const char *stem, const char *leaf, char *out, size_t n) {
  char path[112];
  snprintf(path, sizeof path, "%s%s", stem, leaf);
  bd_id_t id = bd_find(path);
  bd_sample_t s;
  if (id == BD_NONE || !bd_get(id, &s) || !s.valid || s.kind != BD_TEXT) return false;
  snprintf(out, n, "%s", s.text);
  return true;
}

static bool num_at(const char *stem, const char *leaf, double *out) {
  char path[112];
  snprintf(path, sizeof path, "%s%s", stem, leaf);
  bd_id_t id = bd_find(path);
  bd_sample_t s;
  if (id == BD_NONE || !bd_get(id, &s) || !s.valid || s.kind != BD_NUMBER) return false;
  *out = s.num;
  return true;
}

uint8_t power_rows(ui_power_row_t *out, uint8_t max) {
  char solar_dev[MAX_DEV][17];
  uint8_t n_solar = 0, n = 0;
  bd_id_t ids[UI_POWER_ROWS];
  size_t m = bd_match("electrical.resolved.solar.*.ref", ids, UI_POWER_ROWS);
  for (size_t i = 0; i < m && n_solar < MAX_DEV; i++) {
    bd_sample_t s;
    if (bd_get(ids[i], &s) && s.valid && s.kind == BD_TEXT && strlen(s.text) >= 16) {
      snprintf(solar_dev[n_solar], sizeof solar_dev[0], "%.16s", s.text);
      n_solar++;
    }
  }
  static const struct { const char *group; uint8_t fn, grp; } G[] = {
    {"batteries", UI_PW_BATTERY, 0}, {"chargers", UI_PW_OUTPUT, 0}, {"alternators", UI_PW_OUTPUT, 1}};
  for (size_t g = 0; g < sizeof G / sizeof G[0]; g++) {
    char pat[96];
    snprintf(pat, sizeof pat, "electrical.resolved.%s.*.label", G[g].group);
    m = bd_match(pat, ids, UI_POWER_ROWS);   // the label is published with every endpoint, a ref only once its NAME is known
    for (size_t i = 0; i < m && n < max; i++) {
      const char *path = bd_path(ids[i]);
      char stem[96];
      bd_sample_t lab;
      if (!path || strlen(path) < 6) continue;
      if (!bd_get(ids[i], &lab) || !lab.valid) continue;   // the resolver withdrew it (re-keyed, or a mirror merged away)
      snprintf(stem, sizeof stem, "%s", path);
      stem[strlen(stem) - 5] = 0;   // drop "label"
      ui_power_row_t *r = &out[n];
      memset(r, 0, sizeof *r);
      r->fn = G[g].fn;
      r->group = G[g].grp;
      text_at(stem, "label", r->label, sizeof r->label);
      text_at(stem, "ref", r->ref, sizeof r->ref);
      if (G[g].fn == UI_PW_BATTERY) {
        r->value_ok = num_at(stem, "voltage", &r->value);
      } else {
        double v, a;
        r->value_ok = num_at(stem, "voltage", &v) && num_at(stem, "current", &a);
        if (r->value_ok) r->value = v * a;
        bool panel = false;
        for (uint8_t k = 0; k < n_solar; k++) panel |= !strncmp(r->ref, solar_dev[k], 16) && r->ref[0];
        snprintf(r->proposed, sizeof r->proposed, "%s", G[g].grp == 1 ? "engine" : panel ? "solar" : "shore");
      }
      n++;
    }
  }
  return n;
}
