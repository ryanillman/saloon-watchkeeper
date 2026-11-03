// Power Summary: IN -> BATTERY -> LOAD as three large readouts, state of charge as a strip.
// Wide: readouts left to right with arrows, strip along the bottom. Square: a 2x2 grid
// (IN, BATTERY / LOAD, vertical SOC strip). Placeholder instances are not sources.
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include "platform.h"
#include "power_page.h"

#define MAX_SRC 12   // 4 solar + 4 charger + 2 dcdc + 2 alternator

static struct {
  lv_obj_t *in, *batt, *load, *soc, *shore;
  ui_gauge_spec_t spec;
  ui_band_t bands[6];
  char label[32];
} v;

void power_summary_build(lv_obj_t *area, size_class_t sc, role_binding_t *b) {
  memset(&v, 0, sizeof v);
  int32_t W = lv_obj_get_width(area), H = lv_obj_get_height(area), pad = 8;
  uint8_t idx[ROLE_MAX_INST], primary;
  uint8_t n = power_banks(b, idx, &primary);
  role_gauge_spec(b, G_BATTERY, n ? idx[primary] : 0, B_SOC, &v.spec, v.bands, v.label);
  v.spec.decimals = 0;
  const char *w = units_label(Q_POWER, b->units);
  v.in = ui_readout_create(area, "IN", w, 0);
  v.batt = ui_readout_create(area, "BATTERY", w, 0);
  v.load = ui_readout_create(area, "DC LOAD", w, 0);   // AC loads fed from shore never reach the DC side
  ui_field_set_fault_text(v.load, "LOAD FAIL");
  v.soc = ui_strip_create(area, &v.spec, sc != SIZE_WIDE);
  if (sc == SIZE_WIDE) {
    int32_t sh = H / 4, arrow = 36, rw = (W - 2 * pad - 2 * arrow) / 3, rh = H - sh - 3 * pad;
    lv_obj_t *r[3] = {v.in, v.batt, v.load};
    for (int k = 0; k < 3; k++) {
      lv_obj_set_pos(r[k], pad + k * (rw + arrow), pad);
      lv_obj_set_size(r[k], rw, rh);
    }
    for (int k = 0; k < 2; k++) {
      lv_obj_t *l = lv_label_create(area);
      lv_label_set_text(l, LV_SYMBOL_RIGHT);
      lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
      lv_obj_set_style_text_color(l, ui_theme()->dim, 0);
      lv_obj_set_pos(l, pad + (k + 1) * rw + k * arrow + arrow / 2 - 8, pad + rh / 2 - 14);
    }
    lv_obj_set_pos(v.soc, pad, H - sh - pad);
    lv_obj_set_size(v.soc, W - 2 * pad, sh);
    v.shore = lv_label_create(area);
    lv_obj_set_pos(v.shore, pad + rw - 90, pad + 4);
  } else {
    int32_t cw = (W - 3 * pad) / 2, ch = (H - 3 * pad) / 2;
    lv_obj_set_pos(v.in, pad, pad);               lv_obj_set_size(v.in, cw, ch);
    lv_obj_set_pos(v.batt, 2 * pad + cw, pad);    lv_obj_set_size(v.batt, cw, ch);
    lv_obj_set_pos(v.load, pad, 2 * pad + ch);    lv_obj_set_size(v.load, cw, ch);
    lv_obj_set_pos(v.soc, 2 * pad + cw, 2 * pad + ch);
    lv_obj_set_size(v.soc, cw, ch);
    v.shore = lv_label_create(area);
    lv_obj_set_pos(v.shore, pad + cw - 90, pad + 4);
  }
  lv_label_set_text(v.shore, "SHORE");
  lv_obj_set_style_text_font(v.shore, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(v.shore, ui_theme()->accent, 0);
  lv_obj_add_flag(v.shore, LV_OBJ_FLAG_HIDDEN);
}

// The inverter says it is on shore power: AC source "grid" or "shore", or acin.connected = 1.
static bool shore(const role_binding_t *b) {
  for (uint8_t i = 0; i < b->g[G_INVERTER].inst_count; i++) {
    bd_sample_t s;
    if (!power_real(b, G_INVERTER, i) || !power_sample(b, G_INVERTER, i, I_SHORE, &s) || !s.valid) continue;
    if (s.kind == BD_TEXT && (!strcasecmp(s.text, "grid") || !strcasecmp(s.text, "shore"))) return true;
    if (s.kind == BD_NUMBER && s.num == 1) return true;
  }
  return false;
}

void power_summary_refresh(role_binding_t *b, int64_t now) {
  static const uint8_t SRC_G[] = {G_SOLAR, G_CHARGER, G_DCDC, G_ALTERNATOR};
  PLATFORM_EXT_BSS static bd_sample_t st[MAX_SRC][3];
  power_src_t src[MAX_SRC];
  size_t n = 0;
  for (size_t c = 0; c < sizeof SRC_G; c++)
    for (uint8_t i = 0; i < b->g[SRC_G[c]].inst_count && n < MAX_SRC; i++)
      if (power_real(b, SRC_G[c], i)) {
        power_src_of(b, SRC_G[c], i, C_VOLTS, C_AMPS, -1, st[n], &src[n]);   // V x I: watts into the battery
        n++;
      }

  uint8_t idx[ROLE_MAX_INST], primary;
  uint8_t banks = power_banks(b, idx, &primary);
  bd_sample_t bst[3], soc, dc;
  power_src_t batt;
  bool have_soc = false;
  if (banks) {
    power_src_of(b, G_BATTERY, idx[primary], B_VOLTS, B_AMPS, B_POWER, bst, &batt);
    have_soc = power_sample(b, G_BATTERY, idx[primary], B_SOC, &soc);
  }
  bool have_dc = power_sample(b, G_SYSTEM, 0, Y_DCPOWER, &dc);
  power_summary_t r = power_summarize(src, n, banks ? &batt : NULL, have_soc ? &soc : NULL, have_dc ? &dc : NULL, 0, now);

  ui_readout_set(v.in, units_from_si(Q_POWER, r.in_w, b->units), r.in_ok);
  ui_readout_set(v.batt, units_from_si(Q_POWER, r.batt_w, b->units), r.batt_ok);
  ui_readout_set(v.load, units_from_si(Q_POWER, r.load_w, b->units), r.load_ok);
  ui_strip_set(v.soc, units_from_si(Q_RATIO, r.soc, b->units), r.soc_ok);
  if (banks) role_field_alert(v.soc, b->g[G_BATTERY].ids[idx[primary]][B_SOC]);
  if (shore(b)) lv_obj_remove_flag(v.shore, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(v.shore, LV_OBJ_FLAG_HIDDEN);
}
