// Power, Banks page (only with banks configured): the boat's banks in configured order, with an arrow for each
// converter between two of them carrying its output watts. Each bank shows its charge (the
// icon fills to it), SoC, volts, net power, and the sources that feed it. Wide: a column per bank.
// Square: a row per bank, the arrows between the rows.
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "power_page.h"
#include "power_topo.h"
#include "ui_flow.h"

PLATFORM_EXT_BSS static struct {
  power_topo_t topo;
  size_class_t sc;
  uint8_t n;                          // banks drawn
  int kind[TOPO_MAX_BANKS];               // bank index in drawing order
  int pos[TOPO_MAX_BANKS];                // column or row of a bank, -1 = not drawn
  lv_obj_t *icon[TOPO_MAX_BANKS], *soc[TOPO_MAX_BANKS], *volts[TOPO_MAX_BANKS], *watts[TOPO_MAX_BANKS], *src[TOPO_MAX_BANKS];
  ui_gauge_spec_t spec[TOPO_MAX_BANKS];
  ui_band_t bands[TOPO_MAX_BANKS][6];
  char label[TOPO_MAX_BANKS][32];
  uint8_t nconv;
  lv_obj_t *flow[TOPO_MAX_FLOWS];
  uint8_t flow_link[TOPO_MAX_FLOWS];  // index into topo.flow
} bk;

static int32_t clampi(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }

#define caption power_text

static lv_obj_t *field(lv_obj_t *area, const char *name, const char *unit, uint8_t dec, int32_t x, int32_t y,
                       int32_t w, int32_t h, bool centered) {
  lv_obj_t *o = ui_readout_create(area, name, unit, dec);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  ui_readout_set_centered(o, centered);
  return o;
}

uint8_t power_banks_per_page(size_class_t sc) { return sc == SIZE_WIDE ? 4 : 3; }

void power_banks_build(lv_obj_t *area, size_class_t sc, role_binding_t *b, uint8_t page) {
  memset(&bk, 0, sizeof bk);
  power_topo_resolve(b, &bk.topo);
  bk.sc = sc;
  for (int k = 0; k < TOPO_MAX_BANKS; k++) bk.pos[k] = -1;
  uint8_t per = power_banks_per_page(sc), first = (uint8_t)(page * per);   // more banks than fit make more pages
  for (int c = first; c < bk.topo.bank_count && bk.n < per; c++) {
    bk.pos[c] = bk.n;
    bk.kind[bk.n++] = c;
  }
  const ui_colors_t *col = ui_theme();
  int32_t W = lv_obj_get_width(area), H = lv_obj_get_height(area), pad = 8;
  bool wide = sc == SIZE_WIDE;
  const char *pct = units_label(Q_RATIO, b->units), *volt = units_label(Q_VOLTAGE, b->units),
             *pow = units_label(Q_POWER, b->units);
  int32_t gap = 32;   // square: between rows, where the arrow runs
  int32_t cw = (W - 2 * pad) / (bk.n ? bk.n : 1);
  int32_t rowh = (H - 2 * pad - (bk.n - 1) * gap) / (bk.n ? bk.n : 1);
  int32_t isz = wide ? clampi(H * 26 / 100, 48, 110) : clampi(rowh - 40, 48, 84);
  int32_t icx[TOPO_MAX_BANKS], ity[TOPO_MAX_BANKS], ibot[TOPO_MAX_BANKS];   // icon centre x, top y, bottom y, per column or row

  for (uint8_t j = 0; j < bk.n; j++) {
    int k = bk.kind[j];
    if (bk.topo.bank[k].bound)
      role_gauge_spec(b, G_BATTERY, bk.topo.bank[k].inst, B_SOC, &bk.spec[k], bk.bands[k], bk.label[k]);
    if (wide) {
      int32_t x = pad + j * cw, cx = x + cw / 2;
      // SOC on its own line, then volts and watts side by side, with room between the lines so each
      // label reads with the value under it; the sources one per line below.
      int32_t fh = clampi((H - (pad + 30 + isz + 8) - 60 - 2 * 8 - pad) / 2, 44, 72);
      caption(area, bk.topo.bank[k].name, &lv_font_montserrat_20, col->fg, x, pad, cw, LV_TEXT_ALIGN_CENTER);
      bk.icon[k] = ui_icon_create(area, ICON_BATTERY);
      lv_obj_set_pos(bk.icon[k], cx - isz / 2, pad + 30);
      lv_obj_set_size(bk.icon[k], isz, isz);
      icx[j] = cx;
      ity[j] = pad + 30;
      ibot[j] = pad + 30 + isz;
      int32_t y = pad + 30 + isz + 8, fw = cw - 2 * pad, hw = (fw - 6) / 2;
      bk.soc[k] = field(area, "SOC", pct, 0, x + pad, y, fw, fh, true);
      y += fh + 8;
      bk.volts[k] = field(area, "VOLTS", volt, 2, x + pad, y, hw, fh, true);
      bk.watts[k] = field(area, "WATTS", pow, 0, x + pad + hw + 6, y, hw, fh, true);
      bk.src[k] = caption(area, "", &lv_font_montserrat_14, col->dim, x + pad, y + fh + 8, fw, LV_TEXT_ALIGN_CENTER);
    } else {
      int32_t y = pad + j * (rowh + gap), x0 = pad + isz + pad, fw = (W - x0 - pad - 2 * 6) / 3;
      int32_t fh = rowh - 24 - 4;
      bk.icon[k] = ui_icon_create(area, ICON_BATTERY);
      lv_obj_set_pos(bk.icon[k], pad, y + (rowh - isz) / 2 - 8);
      lv_obj_set_size(bk.icon[k], isz, isz);
      icx[j] = pad + isz / 2;
      ity[j] = y + (rowh - isz) / 2 - 8;
      ibot[j] = ity[j] + isz;
      caption(area, bk.topo.bank[k].name, &lv_font_montserrat_20, col->fg, x0, y, W - x0 - pad, LV_TEXT_ALIGN_LEFT);
      bk.soc[k] = field(area, "SOC", pct, 0, x0, y + 24, fw, fh, false);
      bk.volts[k] = field(area, "VOLTS", volt, 2, x0 + fw + 6, y + 24, fw, fh, false);
      bk.watts[k] = field(area, "WATTS", pow, 0, x0 + 2 * (fw + 6), y + 24, fw, fh, false);
      bk.src[k] = caption(area, "", &lv_font_montserrat_14, col->dim, x0, y + 5, W - x0 - pad, LV_TEXT_ALIGN_RIGHT);   // on the name line
    }
  }
  // One arrow per converter between two drawn banks, from the bank it draws on to the bank it feeds.
  for (uint8_t n = 0; n < bk.topo.flow_count && bk.nconv < TOPO_MAX_FLOWS; n++) {
    const power_flow_t *l = &bk.topo.flow[n];
    if (l->kind != FLOW_CONVERTER || l->from < 0 || l->to < 0 || bk.pos[l->from] < 0 || bk.pos[l->to] < 0) continue;
    int a = bk.pos[l->from], z = bk.pos[l->to];
    lv_obj_t *f = ui_flow_create(area);
    if (wide) {
      int32_t dir = z > a ? 1 : -1, y = ity[a] + isz / 2;
      ui_flow_set_route(f, icx[a] + dir * (isz / 2 + 10), y, icx[z] - dir * (isz / 2 + 10), y, AXIS_H, AXIS_H, 1);
    } else {
      int32_t dir = z > a ? 1 : -1;
      int32_t y0 = dir > 0 ? ibot[a] + 2 : ity[a] - 2, y1 = dir > 0 ? ity[z] - 2 : ibot[z] + 2;
      ui_flow_set_route(f, icx[a], y0, icx[z], y1, AXIS_V, AXIS_V, 1);
    }
    bk.flow[bk.nconv] = f;
    bk.flow_link[bk.nconv++] = n;
  }
}

static lv_color_t soc_color(int k, double pct) {
  for (uint8_t i = 0; i < bk.spec[k].band_count; i++)
    if (pct >= bk.spec[k].bands[i].lo && pct <= bk.spec[k].bands[i].hi) return bk.spec[k].bands[i].color;
  return ui_theme()->accent;
}

void power_banks_refresh(role_binding_t *b, int64_t now) {
  if (!bk.n) return;
  char txt[96];
  for (uint8_t j = 0; j < bk.n; j++) {
    int k = bk.kind[j];
    bool bound = bk.topo.bank[k].bound;
    uint8_t i = bk.topo.bank[k].inst;
    bd_sample_t s, st[3];
    power_src_t src;
    double w = 0, volts = 0, soc = 0;
    bool soc_ok = bound && power_sample(b, G_BATTERY, i, B_SOC, &s) && s.kind == BD_NUMBER && s.valid;
    if (soc_ok) soc = s.num;
    bool v_ok = bound && power_sample(b, G_BATTERY, i, B_VOLTS, &s) && s.kind == BD_NUMBER && s.valid;
    if (v_ok) volts = s.num;
    bool w_ok = false;
    if (bound) {
      power_src_of(b, G_BATTERY, i, B_VOLTS, B_AMPS, B_POWER, st, &src);
      w_ok = power_watts(&src, now, &w);
    }
    double pct = units_from_si(Q_RATIO, soc, b->units);
    ui_readout_set(bk.soc[k], pct, soc_ok);
    ui_readout_set(bk.volts[k], units_from_si(Q_VOLTAGE, volts, b->units), v_ok);
    ui_readout_set(bk.watts[k], units_from_si(Q_POWER, w, b->units), w_ok);
    ui_icon_set_fill(bk.icon[k], soc, soc_ok, soc_color(k, pct));
    ui_icon_set_active(bk.icon[k], soc_ok);
    if (bound) {
      role_field_alert(bk.soc[k], b->g[G_BATTERY].ids[i][B_SOC]);
      role_field_alert(bk.volts[k], b->g[G_BATTERY].ids[i][B_VOLTS]);
    }
    power_bank_sources(b, &bk.topo, k, now, bk.sc == SIZE_WIDE ? "\n" : "   ", txt, sizeof txt);
    ui_label_set(bk.src[k], txt);
  }
  double ref = 1, ww[TOPO_MAX_FLOWS];
  bool ok[TOPO_MAX_FLOWS];
  for (uint8_t c = 0; c < bk.nconv; c++) {
    const power_flow_t *l = &bk.topo.flow[bk.flow_link[c]];
    bd_sample_t st[3];
    power_src_t src;
    ok[c] = false;
    ww[c] = 0;
    if (l->bound) {
      power_src_of(b, l->g, l->i, C_VOLTS, C_AMPS, -1, st, &src);
      ok[c] = power_watts(&src, now, &ww[c]);
    }
    if (ok[c] && ww[c] > ref) ref = ww[c];
  }
  for (uint8_t c = 0; c < bk.nconv; c++) {
    char label[24] = "";
    if (ok[c]) snprintf(label, sizeof label, "%.0f %s", units_from_si(Q_POWER, ww[c], b->units), units_label(Q_POWER, b->units));
    ui_flow_set(bk.flow[c], ui_flow_level_for(ww[c], ref), ok[c], label);
  }
}
