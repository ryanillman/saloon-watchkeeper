// Engine pages. Overview: a hero RPM dial (one needle per engine, two at most),
// fuel-tank percent under the digits, and strips for coolant, oil, and volts.
// A square screen puts that dial on top and only coolant and oil in horizontal
// strips under it. Detail: every slot for every engine.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "boatdata.h"
#include "engine_state.h"
#include "platform.h"
#include "role.h"
#include "tanks.h"
#include "ui_gauge.h"

enum { S_RPM, S_COOLANT, S_OIL, S_VOLTS, S_HOURS, S_FUEL, S_STATE };   // order of slots in role_engine.c
static const uint8_t DECIMALS[] = {0, 0, 1, 1, 1, 1, 0};

PLATFORM_EXT_BSS static struct {
  uint8_t page;
  lv_obj_t *dial;
  lv_obj_t *w[ROLE_MAX_INST][ROLE_MAX_SLOTS];
  ui_gauge_spec_t spec[ROLE_MAX_INST][ROLE_MAX_SLOTS];
  ui_band_t bands[ROLE_MAX_INST][ROLE_MAX_SLOTS][6];
  char label[ROLE_MAX_INST][ROLE_MAX_SLOTS][32];
  const char *needle_label[2];
} v;

// Builds the gauge spec for one slot of one engine, in display units.
static const ui_gauge_spec_t *spec_for(role_binding_t *b, uint8_t i, uint8_t k) {
  ui_gauge_spec_t *s = &v.spec[i][k];
  role_gauge_spec(b, 0, i, k, s, v.bands[i][k], v.label[i][k]);
  s->decimals = DECIMALS[k];
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

// "tanks.fuel.0.currentLevel" -> "fuel.0", which tank_label understands.
static void fuel_inst(const char *path, char *out, size_t n) {
  const char *s = path ? path : "";
  if (strncmp(s, "tanks.", 6) == 0) s += 6;
  snprintf(out, n, "%s", s);
  char *dot = strchr(out, '.');
  if (dot && (dot = strchr(dot + 1, '.'))) *dot = 0;
}

static int cmp_fuel(const void *a, const void *b) {
  const char *pa = bd_path(*(const bd_id_t *)a);
  const char *pb = bd_path(*(const bd_id_t *)b);
  return strcmp(pa ? pa : "", pb ? pb : "");
}

// Tank level, not fuel rate. One tank reads "FUEL 55%"; several are named.
static void fuel_footer(const cfg_units_t *u, char *out, size_t n) {
  bd_id_t ids[4];
  size_t c = bd_match("tanks.fuel.*.currentLevel", ids, 4);
  out[0] = 0;
  if (!c) return;
  qsort(ids, c, sizeof ids[0], cmp_fuel);
  for (size_t i = 0; i < c; i++) {
    bd_sample_t s;
    bool ok = bd_get(ids[i], &s) && s.kind == BD_NUMBER && s.valid;
    char bit[24];
    if (c == 1) {
      if (ok) snprintf(bit, sizeof bit, "FUEL %.0f%%", units_from_si(Q_RATIO, s.num, u));
      else snprintf(bit, sizeof bit, "FUEL --");
    } else {
      char id[16], name[16];
      fuel_inst(bd_path(ids[i]), id, sizeof id);
      tank_label(id, (uint8_t)c, name, sizeof name);
      if (ok) snprintf(bit, sizeof bit, "%s %.0f%%", name, units_from_si(Q_RATIO, s.num, u));
      else snprintf(bit, sizeof bit, "%s --", name);
    }
    size_t used = strlen(out);
    if (used && used + 1 < n) {
      out[used++] = ' ';
      out[used] = 0;
    }
    snprintf(out + used, n - used, "%s", bit);
  }
}

static void overview_wide(lv_obj_t *area, role_binding_t *b, uint8_t n, uint8_t needles) {
  int32_t W = lv_obj_get_width(area), H = lv_obj_get_height(area), pad = 8;
  const uint8_t rows[] = {S_COOLANT, S_OIL, S_VOLTS};
  int32_t dial = H - 2 * pad < W / 2 ? H - 2 * pad : W / 2;
  int32_t sx = dial + 2 * pad, hdr = n > 1 ? 18 : 0;
  int32_t sy = pad + hdr;
  int32_t sw = (W - sx - pad - (n - 1) * pad) / n;
  int32_t sh = (H - sy - pad - 2 * pad) / 3;
  for (uint8_t i = 0; n > 1 && i < n; i++) column_header(area, b->g[0].inst_label[i], sx + i * (sw + pad), sy - hdr, sw);
  v.dial = ui_dial_create(area, spec_for(b, 0, S_RPM), needles, v.needle_label);
  place(v.dial, pad, (H - dial) / 2, dial, dial);
  for (uint8_t i = 0; i < n; i++)
    for (uint8_t r = 0; r < 3; r++)
      v.w[i][rows[r]] = place(ui_strip_create(area, spec_for(b, i, rows[r]), false),
                              sx + i * (sw + pad), sy + r * (sh + pad), sw, sh);
}

// Dial on top. Coolant and oil run horizontally underneath; volts stays on the detail page.
static void overview_square(lv_obj_t *area, role_binding_t *b, uint8_t n, uint8_t needles) {
  int32_t W = lv_obj_get_width(area), H = lv_obj_get_height(area), pad = 8, gap = 4;
  const uint8_t rows[] = {S_COOLANT, S_OIL};
  int32_t dial = H * 6 / 10;
  int32_t hdr = n > 1 ? 18 : 0;
  int32_t sx = pad, sy = dial + pad + hdr;
  int32_t sw = (W - 2 * pad - (n - 1) * pad) / n;
  int32_t sh = (H - sy - pad - gap) / 2;
  for (uint8_t i = 0; n > 1 && i < n; i++)
    column_header(area, b->g[0].inst_label[i], sx + i * (sw + pad), sy - hdr, sw);
  v.dial = ui_dial_create(area, spec_for(b, 0, S_RPM), needles, v.needle_label);
  place(v.dial, (W - dial) / 2, 0, dial, dial);
  for (uint8_t i = 0; i < n; i++)
    for (uint8_t r = 0; r < 2; r++)
      v.w[i][rows[r]] = place(ui_strip_create(area, spec_for(b, i, rows[r]), false),
                              sx + i * (sw + pad), sy + r * (sh + gap), sw, sh);
}

static void overview(lv_obj_t *area, size_class_t sc, role_binding_t *b) {
  uint8_t n = b->g[0].inst_count, needles = n > 2 ? 2 : n;
  for (uint8_t i = 0; i < needles; i++) v.needle_label[i] = n > 1 ? b->g[0].inst_label[i] : NULL;
  if (sc == SIZE_WIDE) overview_wide(area, b, n, needles);
  else overview_square(area, b, n, needles);
}

static void detail(lv_obj_t *area, role_binding_t *b) {
  int32_t W = lv_obj_get_width(area), H = lv_obj_get_height(area), pad = 8;
  uint8_t n = b->g[0].inst_count;
  const uint8_t rows[] = {S_RPM, S_COOLANT, S_OIL, S_VOLTS, S_FUEL};
  int32_t hdr = n > 1 ? 18 : 0;
  int32_t cw = (W - 2 * pad - (n - 1) * pad) / n, rh = (H - pad - hdr) / 6;
  for (uint8_t i = 0; i < n; i++) {
    int32_t x = pad + i * (cw + pad);
    if (n > 1) column_header(area, b->g[0].inst_label[i], x, 0, cw);
    for (uint8_t r = 0; r < 5; r++)
      v.w[i][rows[r]] = place(ui_strip_create(area, spec_for(b, i, rows[r]), false), x, hdr + pad / 2 + r * rh, cw, rh - 4);
    const role_slot_t *hs = &b->def->groups[0].slots[S_HOURS];
    spec_for(b, i, S_HOURS);
    v.w[i][S_HOURS] = place(ui_readout_create(area, v.label[i][S_HOURS], units_label(hs->q, b->units), 1),
                            x, hdr + pad / 2 + 5 * rh, cw, rh - 4);
  }
}

// The overview is the role's first page. Every slot, including the one that alarmed, is on the detail page.
uint8_t engine_page_for(const role_binding_t *b, size_class_t sc, const char *path) {
  (void)b;
  (void)sc;
  (void)path;
  return 1;
}

void engine_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b) {
  memset(&v, 0, sizeof v);
  v.page = page;
  lv_obj_update_layout(area);   // sizes are only real after a layout pass
  if (page == 0) overview(area, sc, b);
  else detail(area, b);
}

static bool sample(role_binding_t *b, uint8_t i, uint8_t k, bd_sample_t *out) {
  return b->g[0].ids[i][k] != BD_NONE && bd_get(b->g[0].ids[i][k], out);
}

static bool read(role_binding_t *b, uint8_t i, uint8_t k, int64_t now, double *out) {
  bd_sample_t s;
  if (!sample(b, i, k, &s) || s.kind != BD_NUMBER) return false;
  if (!bd_is_fresh(&s, b->def->groups[0].slots[k].max_age_ms, now)) return false;
  *out = units_from_si(b->def->groups[0].slots[k].q, s.num, b->units);
  return true;
}

static engine_status_t assess(role_binding_t *b, uint8_t i, int64_t now) {
  bd_sample_t r, o, st;
  bool hr = sample(b, i, S_RPM, &r), ho = sample(b, i, S_OIL, &o), hs = sample(b, i, S_STATE, &st);
  return engine_assess(hr ? &r : NULL, ho ? &o : NULL, hs ? &st : NULL, now);
}

void engine_refresh(role_binding_t *b, int64_t now) {
  if (v.dial) {
    char cap[48];
    fuel_footer(b->units, cap, sizeof cap);
    ui_dial_set_footer(v.dial, cap);
  }
  for (uint8_t i = 0; i < b->g[0].inst_count; i++) {
    engine_status_t es = assess(b, i, now);
    for (uint8_t k = 0; k < b->def->groups[0].slot_count; k++) {
      bool is_dial = k == S_RPM && v.dial && i < 2;
      lv_obj_t *w = v.w[i][k];
      if (!w && !is_dial) continue;
      // Off: the running values are dim "OFF", not a fault. Hours stay as they are.
      if (es.mode == ENGINE_OFF && k != S_HOURS) {
        if (is_dial) ui_dial_set_off(v.dial, i);
        if (w) {
          ui_strip_set_off(w);
          ui_field_set_alert(w, 0);   // no stale alert colour on an OFF field
        }
        continue;
      }
      double x = 0;
      bool ok = read(b, i, k, now, &x);
      if (k == S_RPM && es.rpm_suspect) ok = false;   // oil pressure says it is running: RPM is wrong
      if (is_dial) ui_dial_set(v.dial, i, x, ok);
      if (!w) continue;
      role_field_alert(w, b->g[0].ids[i][k]);
      if (k == S_HOURS) ui_readout_set(w, x, ok);
      else ui_strip_set(w, x, ok);
    }
  }
}
