// Tanks page: every tank a vertical strip side by side. Name above, percent large
// on the strip, volume small below. Square boards show 4 tanks per page.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "history.h"
#include "role.h"
#include "tanks.h"
#include "platform.h"
#include "ui_gauge.h"

enum { S_LEVEL, S_VOLUME, S_CAPACITY };
#define PER_SQUARE_PAGE 4

PLATFORM_EXT_BSS static struct {
  uint8_t first, count;   // tanks on this page: b->g[0] indices first .. first+count-1
  lv_obj_t *strip[ROLE_MAX_INST];
  lv_obj_t *vol[ROLE_MAX_INST];
  ui_gauge_spec_t spec[ROLE_MAX_INST];
  ui_band_t bands[ROLE_MAX_INST][6];
  char label[ROLE_MAX_INST][32];
  char vol_label[ROLE_MAX_INST][12];
  char vol_unit[ROLE_MAX_INST][8];
} v;

tank_kind_t tank_kind(const char *id) {
  static const struct { const char *prefix; tank_kind_t kind; } known[] = {
    {"freshWater", TANK_FRESH}, {"fuel", TANK_FUEL}, {"blackWater", TANK_BLACK}, {"wasteWater", TANK_GREY},
  };
  size_t n = strcspn(id, ".");
  for (size_t i = 0; i < sizeof known / sizeof known[0]; i++)
    if (strlen(known[i].prefix) == n && strncmp(known[i].prefix, id, n) == 0) return known[i].kind;
  return TANK_OTHER;
}

void tank_label(const char *id, uint8_t of_type, char *out, size_t n) {
  static const char *const names[] = {"FRESH", "FUEL", "BLACK", "GREY"};
  tank_kind_t k = tank_kind(id);
  char name[16] = "";
  if (k != TANK_OTHER) snprintf(name, sizeof name, "%s", names[k]);
  else {
    size_t len = strcspn(id, "."), i = 0;
    for (; i < len && i + 1 < sizeof name; i++) name[i] = (char)toupper((unsigned char)id[i]);
    name[i] = 0;
  }
  const char *dot = strchr(id, '.');
  if (of_type > 1 && dot && isdigit((unsigned char)dot[1])) snprintf(out, n, "%s %d", name, atoi(dot + 1) + 1);
  else snprintf(out, n, "%s", name);
}

bool tank_volume(const bd_sample_t *level, const bd_sample_t *volume, const bd_sample_t *capacity, double *m3) {
  if (volume && volume->kind == BD_NUMBER && volume->valid) {
    *m3 = volume->num;
    return true;
  }
  if (level && level->kind == BD_NUMBER && level->valid && capacity && capacity->kind == BD_NUMBER &&
      capacity->valid) {
    *m3 = level->num * capacity->num;
    return true;
  }
  return false;
}

uint8_t tank_pages(uint8_t tanks, size_class_t sc) {
  if (sc == SIZE_WIDE) return 1;
  uint8_t p = (uint8_t)((tanks + PER_SQUARE_PAGE - 1) / PER_SQUARE_PAGE);
  return p ? p : 1;
}

uint8_t tanks_pages(const role_binding_t *b, size_class_t sc) { return tank_pages(b->g[0].inst_count, sc); }

uint8_t tanks_page_for(const role_binding_t *b, size_class_t sc, const char *path) {
  if (!b || !path) return 0;
  if (strncmp(path, "notifications.", 14) == 0) path += 14;
  if (strncmp(path, "tanks.", 6) != 0) return 0;
  const char *rest = path + 6;
  uint8_t per = sc == SIZE_WIDE ? ROLE_MAX_INST : PER_SQUARE_PAGE;
  for (uint8_t i = 0; i < b->g[0].inst_count; i++) {
    size_t n = strlen(b->g[0].inst_id[i]);
    if (n && strncmp(rest, b->g[0].inst_id[i], n) == 0 && rest[n] == '.') return (uint8_t)(i / per);
  }
  return 0;
}

static lv_color_t kind_color(tank_kind_t k) {
  switch (k) {
    case TANK_FRESH: return lv_color_hex(0x3B8BEB);
    case TANK_FUEL: return lv_color_hex(0xE0A030);
    case TANK_BLACK: return lv_color_hex(0x7A5230);
    case TANK_GREY: return lv_color_hex(0x9A9A9A);
    default: return ui_theme()->accent;
  }
}

static bool night(void) { return ui_theme() == ui_theme_colors(PALETTE_NIGHT); }

static bool sample(role_binding_t *b, uint8_t i, uint8_t k, bd_sample_t *out) {
  return b->g[0].ids[i][k] != BD_NONE && bd_get(b->g[0].ids[i][k], out);
}

// A slot the server has never sent (and no explicit bind promises) is not on this boat.
static bool seen(role_binding_t *b, uint8_t i, uint8_t k) {
  bd_sample_t s;
  return sample(b, i, k, &s) && (s.valid || s.rx_ms != 0);
}

void tanks_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b) {
  memset(&v, 0, sizeof v);
  lv_obj_update_layout(area);
  uint8_t n = b->g[0].inst_count;
  uint8_t per = sc == SIZE_WIDE ? ROLE_MAX_INST : PER_SQUARE_PAGE;
  v.first = (uint8_t)(page * per);
  v.count = v.first >= n ? 0 : (uint8_t)(n - v.first < per ? n - v.first : per);
  if (!v.count) return;

  int32_t W = lv_obj_get_width(area), H = lv_obj_get_height(area), pad = 8;
  int32_t cw = (W - 2 * pad - (v.count - 1) * pad) / v.count;
  int32_t hdr = 24, volh = 48;
  uint8_t per_kind[5] = {0};
  for (uint8_t i = 0; i < n; i++) per_kind[tank_kind(b->g[0].inst_id[i])]++;

  for (uint8_t j = 0; j < v.count; j++) {
    uint8_t i = (uint8_t)(v.first + j);
    tank_kind_t kind = tank_kind(b->g[0].inst_id[i]);
    int32_t x = pad + j * (cw + pad);

    char name[16];
    tank_label(b->g[0].inst_id[i], per_kind[kind], name, sizeof name);
    lv_obj_t *l = lv_label_create(area);
    lv_label_set_text(l, name);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(l, ui_theme()->accent, 0);
    lv_obj_set_width(l, cw);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, x, 2);

    ui_gauge_spec_t *s = &v.spec[j];
    role_gauge_spec(b, 0, i, S_LEVEL, s, v.bands[j], v.label[j]);
    s->decimals = 0;
    if (!night()) {
      s->has_color = true;
      s->color = kind_color(kind);
    }
    v.strip[j] = ui_strip_create(area, s, true);
    lv_obj_set_pos(v.strip[j], x, hdr);
    lv_obj_set_size(v.strip[j], cw, H - hdr - volh - 4);

    snprintf(v.vol_label[j], sizeof v.vol_label[j], "%s", b->def->groups[0].slots[S_VOLUME].fail_label);
    snprintf(v.vol_unit[j], sizeof v.vol_unit[j], "%s", units_label(Q_VOLUME, b->units));
    v.vol[j] = ui_readout_create(area, v.vol_label[j], v.vol_unit[j], strcmp(v.vol_unit[j], "L") == 0 ? 0 : 1);
    lv_obj_set_pos(v.vol[j], x, H - volh);
    lv_obj_set_size(v.vol[j], cw, volh);
  }
}

void tanks_refresh(role_binding_t *b, int64_t now) {
  (void)now;   // levels have no max age
  for (uint8_t j = 0; j < v.count; j++) {
    uint8_t i = (uint8_t)(v.first + j);
    bd_sample_t lv, vo, ca;
    bool hl = sample(b, i, S_LEVEL, &lv), hv = sample(b, i, S_VOLUME, &vo), hc = sample(b, i, S_CAPACITY, &ca);
    bool ok = hl && lv.kind == BD_NUMBER && lv.valid;
    ui_strip_set(v.strip[j], ok ? units_from_si(Q_RATIO, lv.num, b->units) : 0, ok);
    role_field_alert(v.strip[j], b->g[0].ids[i][S_LEVEL]);

    bool has_vol = seen(b, i, S_VOLUME) || seen(b, i, S_CAPACITY);
    if (!has_vol) {   // no volume on this tank: leave the line empty rather than flag it
      lv_obj_add_flag(v.vol[j], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_remove_flag(v.vol[j], LV_OBJ_FLAG_HIDDEN);
    double m3 = 0;
    bool vok = tank_volume(hl ? &lv : NULL, hv ? &vo : NULL, hc ? &ca : NULL, &m3);
    ui_readout_set(v.vol[j], vok ? units_from_si(Q_VOLUME, m3, b->units) : 0, vok);
  }
}

// The Glance tile: three bars, water and fuel at their lowest tank and waste (black, else grey) at its fullest.
// A tank whose level is missing makes the tile NO DATA and counts as empty on its bar. Water's change over the
// last 6 h comes from the history store when it has the data.
void tanks_tile(const role_binding_t *b, int64_t now_ms, glance_tile_t *out) {
  out->title = "TANKS";
  uint8_t n = b->g[0].inst_count;
  if (n == 0) {
    glance_tile_state(out, GT_NO_DATA);
    return;
  }
  gt_state_t st[ROLE_MAX_INST];
  static const char LABEL[3] = {'W', 'F', 'S'};
  bool present[3] = {false}, valid[3] = {false};
  double level[3] = {0};
  for (uint8_t i = 0; i < n; i++) {
    bd_id_t id = b->g[0].ids[i][S_LEVEL];
    st[i] = glance_path_state(id, 0, now_ms);
    tank_kind_t kind = tank_kind(b->g[0].inst_id[i]);
    int bar = kind == TANK_FRESH ? 0 : kind == TANK_FUEL ? 1 : kind == TANK_BLACK || kind == TANK_GREY ? 2 : -1;
    if (bar < 0) continue;
    present[bar] = true;
    if (bar == 0 && out->trend == BD_NONE) out->trend = id;
    bd_sample_t smp;
    if (id == BD_NONE || !bd_get(id, &smp) || smp.kind != BD_NUMBER || !smp.valid) continue;
    double pct = units_from_si(Q_RATIO, smp.num, b->units);
    if (!valid[bar] || (bar == 2 ? pct > level[bar] : pct < level[bar])) level[bar] = pct;
    valid[bar] = true;
  }
  glance_tile_state(out, glance_worst(st, n));
  for (int bar = 0; bar < 3; bar++) {
    if (!present[bar]) continue;
    double pct = valid[bar] ? level[bar] : 0;
    out->bar_label[out->bar_count] = LABEL[bar];
    out->bar_pct[out->bar_count++] = (uint8_t)(pct < 0 ? 0 : pct > 100 ? 100 : pct + 0.5);
  }
  float d;
  if (out->trend != BD_NONE && hist_change(out->trend, 360, &d))
    snprintf(out->context[0], sizeof out->context[0], "WATER %+.0f%% in 6 h", (double)d * 100.0);
}
