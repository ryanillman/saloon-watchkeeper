// Gauge specs from a binding: display-unit range and meta.zones as bands.
#include <stdio.h>
#include <string.h>
#include "role.h"

static lv_color_t state_color(uint8_t state) {
  const ui_colors_t *c = ui_theme();
  return state >= 3 ? c->alarm : c->warn;   // alarm/emergency red; alert/warn amber
}

const ui_gauge_spec_t *role_gauge_spec(role_binding_t *b, uint8_t g, uint8_t i, uint8_t k, ui_gauge_spec_t *spec,
                                       ui_band_t bands[6], char label[32]) {
  const role_slot_t *sl = &b->def->groups[g].slots[k];
  const role_group_binding_t *gb = &b->g[g];
  memset(spec, 0, sizeof *spec);
  snprintf(label, 32, "%s", sl->fail_label);   // the column header names the instance
  spec->label = label;
  spec->unit = units_label(sl->q, b->units);
  spec->lo = units_from_si(sl->q, gb->min[i][k], b->units);
  spec->hi = units_from_si(sl->q, gb->max[i][k], b->units);
  spec->sweep = SWEEP_THREE_QUARTER;
  bd_meta_t m;
  if (gb->ids[i][k] != BD_NONE && bd_get_meta(gb->ids[i][k], &m)) {
    for (uint8_t z = 0; z < m.zone_count && spec->band_count < 6; z++) {
      if (m.zones[z].state == 0) continue;   // normal zones are not drawn
      ui_band_t *band = &bands[spec->band_count++];
      band->lo = units_from_si(sl->q, m.zones[z].lower, b->units);
      band->hi = units_from_si(sl->q, m.zones[z].upper, b->units);
      band->color = state_color(m.zones[z].state);
    }
    spec->bands = bands;
  }
  return spec;
}

void role_field_alert(lv_obj_t *field, bd_id_t id) {
  const char *path = id != BD_NONE ? bd_path(id) : NULL;
  ui_field_set_alert(field, path ? (uint8_t)alerts_for_path(path) : 0);
}
