// Engine pages. Overview: a hero RPM dial (one needle per engine, two at most)
// and strips for coolant, oil, and volts. Detail: every slot for every engine.
#include <stdio.h>
#include <string.h>
#include "engine_state.h"
#include "role.h"
#include "ui_gauge.h"

enum { S_RPM, S_COOLANT, S_OIL, S_VOLTS, S_HOURS, S_FUEL, S_STATE };   // order of slots in role_engine.c
static const uint8_t DECIMALS[] = {0, 0, 1, 1, 1, 1, 0};

static struct {
  uint8_t page;
  lv_obj_t *dial;
  lv_obj_t *w[ROLE_MAX_INST][ROLE_MAX_SLOTS];
  ui_gauge_spec_t spec[ROLE_MAX_INST][ROLE_MAX_SLOTS];
  ui_band_t bands[ROLE_MAX_INST][ROLE_MAX_SLOTS][6];
  char label[ROLE_MAX_INST][ROLE_MAX_SLOTS][32];
  const char *needle_label[2];
} v;

static lv_color_t state_color(uint8_t state) {
  const ui_colors_t *c = ui_theme();
  return state >= 3 ? c->alarm : c->warn;   // alarm/emergency red; alert/warn amber
}

// Builds the gauge spec for one slot of one engine, in display units.
static const ui_gauge_spec_t *spec_for(role_binding_t *b, uint8_t i, uint8_t k) {
  const role_slot_t *sl = &b->def->slots[k];
  ui_gauge_spec_t *s = &v.spec[i][k];
  memset(s, 0, sizeof *s);
  snprintf(v.label[i][k], sizeof v.label[i][k], "%s", sl->fail_label);   // the column header names the engine
  s->label = v.label[i][k];
  s->unit = units_label(sl->q, b->units);
  s->lo = units_from_si(sl->q, b->min[i][k], b->units);
  s->hi = units_from_si(sl->q, b->max[i][k], b->units);
  s->sweep = SWEEP_THREE_QUARTER;
  s->decimals = DECIMALS[k];
  bd_meta_t m;
  if (b->ids[i][k] != BD_NONE && bd_get_meta(b->ids[i][k], &m)) {
    for (uint8_t z = 0; z < m.zone_count && s->band_count < 6; z++) {
      if (m.zones[z].state == 0) continue;   // normal zones are not drawn
      ui_band_t *band = &v.bands[i][k][s->band_count++];
      band->lo = units_from_si(sl->q, m.zones[z].lower, b->units);
      band->hi = units_from_si(sl->q, m.zones[z].upper, b->units);
      band->color = state_color(m.zones[z].state);
    }
    s->bands = v.bands[i][k];
  }
  return s;
}

static void column_header(lv_obj_t *area, const char *text, int32_t x, int32_t y, int32_t w) {
  lv_obj_t *l = lv_label_create(area);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(l, ui_theme()->accent, 0);
  lv_obj_set_width(l, w);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(l, x, y);
}

static lv_obj_t *place(lv_obj_t *o, int32_t x, int32_t y, int32_t w, int32_t h) {
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  return o;
}

static void overview(lv_obj_t *area, size_class_t sc, role_binding_t *b) {
  int32_t W = lv_obj_get_width(area), H = lv_obj_get_height(area), pad = 8;
  uint8_t n = b->inst_count, needles = n > 2 ? 2 : n;
  for (uint8_t i = 0; i < needles; i++) v.needle_label[i] = n > 1 ? b->inst_label[i] : NULL;
  const uint8_t rows[] = {S_COOLANT, S_OIL, S_VOLTS};

  int32_t dial, sx, sy, sw, sh;
  if (sc == SIZE_WIDE) {   // dial left, strips right
    dial = H - 2 * pad < W / 2 ? H - 2 * pad : W / 2;
    sx = dial + 2 * pad;
    sy = pad;
    sw = (W - sx - pad - (n - 1) * pad) / n;
    sh = (H - 2 * pad - 2 * pad) / 3;
  } else {                 // dial top, strips below
    dial = H * 6 / 10;
    sx = pad;
    sy = dial + pad;
    sw = (W - 2 * pad - (n - 1) * pad) / n;
    sh = (H - sy - pad - 2 * 4) / 3;
  }
  int32_t hdr = n > 1 ? 18 : 0;   // PORT / STBD over each column of strips
  sy += hdr;
  if (sc == SIZE_WIDE) sh = (H - sy - pad - 2 * pad) / 3;
  else sh = (H - sy - pad - 2 * 4) / 3;
  for (uint8_t i = 0; n > 1 && i < n; i++) column_header(area, b->inst_label[i], sx + i * (sw + pad), sy - hdr, sw);
  v.dial = ui_dial_create(area, spec_for(b, 0, S_RPM), needles, v.needle_label);
  if (sc == SIZE_WIDE) place(v.dial, pad, (H - dial) / 2, dial, dial);
  else place(v.dial, (W - dial) / 2, 0, dial, dial);
  int32_t gap = sc == SIZE_WIDE ? pad : 4;
  for (uint8_t i = 0; i < n; i++)
    for (uint8_t r = 0; r < 3; r++)
      v.w[i][rows[r]] = place(ui_strip_create(area, spec_for(b, i, rows[r]), false),
                              sx + i * (sw + pad), sy + r * (sh + gap), sw, sh);
}

static void detail(lv_obj_t *area, role_binding_t *b) {
  int32_t W = lv_obj_get_width(area), H = lv_obj_get_height(area), pad = 8;
  uint8_t n = b->inst_count;
  const uint8_t rows[] = {S_RPM, S_COOLANT, S_OIL, S_VOLTS, S_FUEL};
  int32_t hdr = n > 1 ? 18 : 0;
  int32_t cw = (W - 2 * pad - (n - 1) * pad) / n, rh = (H - pad - hdr) / 6;
  for (uint8_t i = 0; i < n; i++) {
    int32_t x = pad + i * (cw + pad);
    if (n > 1) column_header(area, b->inst_label[i], x, 0, cw);
    for (uint8_t r = 0; r < 5; r++)
      v.w[i][rows[r]] = place(ui_strip_create(area, spec_for(b, i, rows[r]), false), x, hdr + pad / 2 + r * rh, cw, rh - 4);
    const role_slot_t *hs = &b->def->slots[S_HOURS];
    spec_for(b, i, S_HOURS);
    v.w[i][S_HOURS] = place(ui_readout_create(area, v.label[i][S_HOURS], units_label(hs->q, b->units), 1),
                            x, hdr + pad / 2 + 5 * rh, cw, rh - 4);
  }
}

void engine_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b) {
  memset(&v, 0, sizeof v);
  v.page = page;
  lv_obj_update_layout(area);   // sizes are only real after a layout pass
  if (page == 0) overview(area, sc, b);
  else detail(area, b);
}

static bool sample(role_binding_t *b, uint8_t i, uint8_t k, bd_sample_t *out) {
  return b->ids[i][k] != BD_NONE && bd_get(b->ids[i][k], out);
}

static bool read(role_binding_t *b, uint8_t i, uint8_t k, int64_t now, double *out) {
  bd_sample_t s;
  if (!sample(b, i, k, &s) || s.kind != BD_NUMBER) return false;
  if (!bd_is_fresh(&s, b->def->slots[k].max_age_ms, now)) return false;
  *out = units_from_si(b->def->slots[k].q, s.num, b->units);
  return true;
}

static engine_status_t assess(role_binding_t *b, uint8_t i, int64_t now) {
  bd_sample_t r, o, st;
  bool hr = sample(b, i, S_RPM, &r), ho = sample(b, i, S_OIL, &o), hs = sample(b, i, S_STATE, &st);
  return engine_assess(hr ? &r : NULL, ho ? &o : NULL, hs ? &st : NULL, now);
}

void engine_refresh(role_binding_t *b, int64_t now) {
  for (uint8_t i = 0; i < b->inst_count; i++) {
    engine_status_t es = assess(b, i, now);
    for (uint8_t k = 0; k < b->def->slot_count; k++) {
      bool is_dial = k == S_RPM && v.dial && i < 2;
      lv_obj_t *w = v.w[i][k];
      if (!w && !is_dial) continue;
      // Off: the running values are dim "OFF", not a fault. Hours stay as they are.
      if (es.mode == ENGINE_OFF && k != S_HOURS) {
        if (is_dial) ui_dial_set_off(v.dial, i);
        if (w) ui_strip_set_off(w);
        continue;
      }
      double x = 0;
      bool ok = read(b, i, k, now, &x);
      if (k == S_RPM && es.rpm_suspect) ok = false;   // oil pressure says it is running: RPM is wrong
      if (is_dial) ui_dial_set(v.dial, i, x, ok);
      if (!w) continue;
      if (k == S_HOURS) ui_readout_set(w, x, ok);
      else ui_strip_set(w, x, ok);
    }
  }
}
