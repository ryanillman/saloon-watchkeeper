// Engine pages. Overview: a hero RPM dial (one needle per engine, two at most),
// the fuel-tank level on the open bottom of the dial's sweep, and strips for coolant, oil, and volts.
// A square screen puts that dial on top and only coolant and oil in horizontal
// strips under it. Detail: every slot for every engine.
#include <ctype.h>
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

static int cmp_fuel(const void *a, const void *b) {
  const char *pa = bd_path(*(const bd_id_t *)a);
  const char *pb = bd_path(*(const bd_id_t *)b);
  return strcmp(pa ? pa : "", pb ? pb : "");
}

// "tanks.fuel.port.currentLevel" -> "PORT"; "...starboard..." -> "STBD"; "...1..." -> "2" (ids count from 0).
static void fuel_name(const char *path, char *out, size_t n) {
  const char *s = path ? path : "";
  if (strncmp(s, "tanks.fuel.", 11) == 0) s += 11;
  size_t len = strcspn(s, ".");
  if (len == 4 && strncmp(s, "port", 4) == 0) snprintf(out, n, "PORT");
  else if (len >= 4 && strncmp(s, "star", 4) == 0) snprintf(out, n, "STBD");
  else if (len && isdigit((unsigned char)s[0])) snprintf(out, n, "%d", atoi(s) + 1);
  else snprintf(out, n, "%.*s", (int)len, s);
}

// The tank scale in the open bottom of the RPM dial: tank level, not fuel rate. One pointer per tank, two at
// most, in path order (port before starboard, 0 before 1). Bands are the first tank's meta.zones.
static void fuel_setup(lv_obj_t *dial, const bd_id_t *ids, size_t c);
static lv_obj_t *seen_dial;   // the dial fuel_scale last set up; cleared when a page build makes a new one (the address can be reused)

// The match, sort, names and bands are redone only when the path table grew, the dial is a new one, or 5 s have passed
// (a zone can arrive for a path that already exists); every call still sets the levels.
static void fuel_scale(lv_obj_t *dial, int64_t now) {
  static bd_id_t ids[4];
  static size_t c, seen_count;
  static int64_t setup_ms;
  if (dial != seen_dial || bd_count() != seen_count || now - setup_ms >= 5000) {
    seen_dial = dial;
    seen_count = bd_count();
    setup_ms = now;
    c = bd_match("tanks.fuel.*.currentLevel", ids, 4);
    if (c > 2) c = 2;
    qsort(ids, c, sizeof ids[0], cmp_fuel);
    fuel_setup(dial, ids, c);
  }
  for (size_t i = 0; i < c; i++) {
    bd_sample_t s;
    bool ok = bd_get(ids[i], &s) && s.kind == BD_NUMBER && s.valid;
    ui_dial_fuel_set(dial, (uint8_t)i, ok ? s.num : 0, ok);
  }
}

static void fuel_setup(lv_obj_t *dial, const bd_id_t *ids, size_t c) {
  char names[2][12] = {"", ""};
  const char *labels[2] = {names[0], names[1]};
  for (size_t i = 0; i < c && c > 1; i++) fuel_name(bd_path(ids[i]), names[i], sizeof names[i]);
  ui_band_t bands[6];
  uint8_t nb = 0;
  bd_meta_t m;
  if (c && bd_get_meta(ids[0], &m)) {
    const ui_colors_t *col = ui_theme();
    for (uint8_t z = 0; z < m.zone_count && nb < 6; z++) {
      if (m.zones[z].state == 0) continue;   // normal zones are not drawn
      bands[nb++] = (ui_band_t){m.zones[z].lower, m.zones[z].upper, m.zones[z].state >= 3 ? col->alarm : col->warn};
    }
  }
  ui_dial_fuel_setup(dial, (uint8_t)c, labels, bands, nb);
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
  seen_dial = NULL;
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
  seen_dial = NULL;
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

static bool sample(const role_binding_t *b, uint8_t i, uint8_t k, bd_sample_t *out) {
  return b->g[0].ids[i][k] != BD_NONE && bd_get(b->g[0].ids[i][k], out);
}

static bool read(const role_binding_t *b, uint8_t i, uint8_t k, int64_t now, double *out) {
  bd_sample_t s;
  if (!sample(b, i, k, &s) || s.kind != BD_NUMBER) return false;
  if (!bd_is_fresh(&s, b->def->groups[0].slots[k].max_age_ms, now)) return false;
  *out = units_from_si(b->def->groups[0].slots[k].q, s.num, b->units);
  return true;
}

static engine_status_t assess(const role_binding_t *b, uint8_t i, int64_t now) {
  bd_sample_t r, o, st;
  bool hr = sample(b, i, S_RPM, &r), ho = sample(b, i, S_OIL, &o), hs = sample(b, i, S_STATE, &st);
  return engine_assess(hr ? &r : NULL, ho ? &o : NULL, hs ? &st : NULL, now);
}

void engine_refresh(role_binding_t *b, int64_t now) {
  if (v.dial) fuel_scale(v.dial, now);   // stays live when an engine is off: a tank level does not depend on it
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

// The Glance tile. Running engines give their RPM, joined by "/"; all stopped reads STOPPED. The state is the
// worst over the engines: an engine that is off is OFF, one we cannot tell about is NO DATA, a running one
// takes the worst of its RPM, coolant, oil and volts (alerts and zones from Signal K).
void engine_tile(const role_binding_t *b, int64_t now_ms, glance_tile_t *out) {
  out->title = "ENGINE";
  const role_group_t *g = &b->def->groups[0];
  uint8_t n = b->g[0].inst_count;
  if (n == 0) {
    glance_tile_state(out, GT_NO_DATA);
    return;
  }
  gt_state_t st[ROLE_MAX_INST];
  uint8_t running = 0, off = 0;
  double coolant_max = -1e9, oil_min = 1e9, x;
  bool have_coolant = false, have_oil = false;
  for (uint8_t i = 0; i < n; i++) {
    engine_status_t es = assess(b, i, now_ms);
    static const uint8_t watched[] = {S_RPM, S_COOLANT, S_OIL, S_VOLTS};
    gt_state_t alert = GT_OK;   // an alarm counts whatever the engine is doing: ignition on, engine off, oil alarm
    for (size_t w = 0; w < sizeof watched; w++) {
      gt_state_t as = glance_alert_state(b->g[0].ids[i][watched[w]]);
      if (as > alert) alert = as;
    }
    if (es.mode == ENGINE_OFF) {
      st[i] = alert > GT_OK ? alert : GT_OFF;
      off++;
      continue;
    }
    if (es.mode != ENGINE_RUNNING) {
      st[i] = alert > GT_NO_DATA ? alert : GT_NO_DATA;
      continue;
    }
    gt_state_t worst = es.rpm_suspect ? GT_NO_DATA : GT_OK;
    for (size_t w = 0; w < sizeof watched; w++) {
      bd_id_t id = b->g[0].ids[i][watched[w]];
      if (id == BD_NONE) continue;
      gt_state_t ps = glance_path_state(id, g->slots[watched[w]].max_age_ms, now_ms);
      if (ps > worst) worst = ps;
    }
    st[i] = worst;
    running++;
    if (read(b, i, S_RPM, now_ms, &x) && !es.rpm_suspect) {
      size_t len = strlen(out->headline);
      snprintf(out->headline + len, sizeof out->headline - len, "%s%.0f", len ? "/" : "", x);
    }
    if (read(b, i, S_COOLANT, now_ms, &x) && (!have_coolant || x > coolant_max)) coolant_max = x, have_coolant = true;
    if (read(b, i, S_OIL, now_ms, &x) && (!have_oil || x < oil_min)) oil_min = x, have_oil = true;
  }
  glance_tile_state(out, glance_worst(st, n));
  if (running) {
    snprintf(out->unit, sizeof out->unit, "%s", units_label(Q_ROTATION, b->units));
    if (have_coolant) snprintf(out->context[0], sizeof out->context[0], "COOLANT %.0f %s", coolant_max, units_label(Q_TEMPERATURE, b->units));
    if (have_oil) snprintf(out->context[1], sizeof out->context[1], "OIL %.1f %s", oil_min, units_label(Q_PRESSURE, b->units));
    return;
  }
  if (off == n) snprintf(out->headline, sizeof out->headline, "STOPPED");   // every engine is off, whatever the alarms say
  for (uint8_t i = 0; i < n; i++) {   // stopped: engine hours, and the coolant while it cools
    if (!out->context[0][0] && read(b, i, S_HOURS, now_ms, &x))
      snprintf(out->context[0], sizeof out->context[0], "HOURS %.0f %s", x, units_label(Q_DURATION, b->units));
    if (!out->context[1][0] && read(b, i, S_COOLANT, now_ms, &x))
      snprintf(out->context[1], sizeof out->context[1], "COOLANT %.0f %s", x, units_label(Q_TEMPERATURE, b->units));
  }
}
