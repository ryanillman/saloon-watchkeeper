// Power Summary: a flow diagram. Sources (solar, shore, engine) feed a bus, the bus feeds the
// battery and the load, and a discharging battery feeds the load too. Arrows carry the direction
// and size of each flow and move while power flows. The battery icon fills to the state of charge.
// Wide: sources stacked on the left, load top right, battery bottom centre. Square: sources along
// the top, battery bottom left, load bottom right. Placeholder instances are not sources.
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "platform.h"
#include "power_page.h"
#include "power_topo.h"
#include "ui_flow.h"

#define MAX_SRC 12   // 4 solar + 4 charger + 2 dcdc + 2 alternator

// Source nodes. SHORE is the charger group (a shore charger), shown too when the inverter reports
// shore power. ENGINE is the alternators and DC-DC converters. ANY stands in when nothing is bound.
enum { N_SOLAR, N_SHORE, N_ENGINE, N_ANY, N_COUNT };

typedef struct { lv_obj_t *icon, *val, *flow; } node_t;

static struct {
  node_t src[N_COUNT];
  node_t batt, load;
  node_t nav;                  // multi-bank, wide: the nav bank, fed from the house battery
  lv_obj_t *nav_flow;
  lv_obj_t *nav_strip;         // multi-bank, square: one text line under the diagram
  bool multi;                  // banks configured
  lv_obj_t *to_load, *to_batt, *from_batt, *bus;
  int32_t W, H;
  size_class_t sc;
  uint8_t mask;        // sources currently shown, bit per N_*
  bool laid_out;
  const char *unit;
  ui_gauge_spec_t spec;
  ui_band_t bands[6];
  char label[32];
} v;

static int32_t clampi(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }
static int32_t mini(int32_t a, int32_t b) { return a < b ? a : b; }

static void put(lv_obj_t *o, int32_t x, int32_t y, int32_t w, int32_t h) {
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
}

// Positions everything for the sources in `mask` (bit per node).
static void relayout(uint8_t mask) {
  const int32_t pad = 8, gap = 4, W = v.W, H = v.H;
  bool wide = v.sc == SIZE_WIDE;
  int32_t isz = clampi(wide ? H / 5 : H / 6, 40, 120), rh = clampi(wide ? H / 7 : H / 8, 44, 72);
  int32_t rw = clampi(W / 6, 100, 170);
  uint8_t shown[N_COUNT], m = 0;
  for (uint8_t k = 0; k < N_COUNT; k++) {
    bool on = mask & (1u << k);
    if (on) shown[m++] = k;
    if (on) {
      lv_obj_remove_flag(v.src[k].icon, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(v.src[k].val, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(v.src[k].flow, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(v.src[k].icon, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(v.src[k].val, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(v.src[k].flow, LV_OBJ_FLAG_HIDDEN);
    }
  }
  int32_t jx, jy;                       // the bus
  int32_t bx, by_top, bcx;              // battery icon: arrow target above it, centre x
  int32_t lx, ly_icon, ly_val;          // load: arrow targets on its left edge
  int32_t b_out_x, b_out_y;             // where the battery's own arrow leaves
  int32_t lcx;

  if (v.sc == SIZE_WIDE) {
    int32_t cell = (H - 2 * pad) / (m ? m : 1);
    int32_t si = mini(isz, cell - 8), sh = mini(rh, cell - 8);
    jx = W * 50 / 100;
    jy = H * 30 / 100;
    for (uint8_t k = 0; k < m; k++) {
      node_t *n = &v.src[shown[k]];
      int32_t cy = pad + k * cell + cell / 2;
      put(n->icon, pad, cy - si / 2, si, si);
      put(n->val, pad + si + gap, cy - sh / 2, rw, sh);
      ui_flow_set_route(n->flow, pad + si + gap + rw + 6, cy, jx, jy, AXIS_H, AXIS_H, 0);
    }
    lcx = W - pad - rw / 2;
    put(v.load.icon, lcx - isz / 2, pad, isz, isz);
    put(v.load.val, lcx - rw / 2, pad + isz + gap, rw, rh);
    lx = lcx - isz / 2 - 8;
    ly_icon = pad + isz / 2;
    ly_val = pad + isz + gap + rh / 2;
    bcx = W * 66 / 100;
    int32_t bi_y = H - pad - rh - gap - isz;
    put(v.batt.icon, bcx - isz / 2, bi_y, isz, isz);
    put(v.batt.val, bcx - rw / 2, H - pad - rh, rw, rh);
    by_top = bi_y - 6;
    bx = bcx;
    b_out_x = bcx + isz / 2 + 8;
    b_out_y = bi_y + isz / 2;
    ui_flow_set_route(v.to_load, jx, jy, lx, ly_icon, AXIS_H, AXIS_H, 1);
    ui_flow_set_route(v.from_batt, b_out_x, b_out_y, lcx - rw / 2 - 8, ly_val, AXIS_H, AXIS_H, -1);
    ui_flow_set_route(v.to_batt, jx, jy, bx, by_top, AXIS_V, AXIS_V, 1);
    if (v.nav.icon) {   // under the load, level with the battery; the arrow leaves the house battery
      put(v.nav.icon, lcx - isz / 2, bi_y, isz, isz);
      put(v.nav.val, lcx - rw / 2, H - pad - rh, rw, rh);
      int32_t ay = bi_y + isz * 3 / 4;
      ui_flow_set_route(v.nav_flow, b_out_x, ay, lcx - isz / 2 - 8, ay, AXIS_H, AXIS_H, 1);
    }
  } else {
    int32_t cell = (W - 2 * pad) / (m ? m : 1);
    int32_t si = mini(isz, cell * 6 / 10);
    int32_t row_top = H - pad - rh - gap - isz, src_bottom = pad + si + gap + rh;
    jx = W / 2;
    jy = (src_bottom + row_top) / 2;
    for (uint8_t k = 0; k < m; k++) {
      node_t *n = &v.src[shown[k]];
      int32_t cx = pad + k * cell + cell / 2, vw = mini(cell - 8, rw + 20);
      put(n->icon, cx - si / 2, pad, si, si);
      put(n->val, cx - vw / 2, pad + si + gap, vw, rh);
      ui_flow_set_route(n->flow, cx, pad + si + gap + rh + 6, jx, jy, AXIS_V, AXIS_V, 0);
    }
    int32_t row_y = row_top;
    bcx = W / 4;
    lcx = W * 3 / 4;
    int32_t vw = mini(W / 2 - 2 * pad, rw + 20);
    put(v.batt.icon, bcx - isz / 2, row_y, isz, isz);
    put(v.batt.val, bcx - vw / 2, H - pad - rh, vw, rh);
    put(v.load.icon, lcx - isz / 2, row_y, isz, isz);
    put(v.load.val, lcx - vw / 2, H - pad - rh, vw, rh);
    by_top = row_y - 6;
    bx = bcx;
    lx = lcx;
    ly_icon = row_y - 6;
    b_out_x = bcx + isz / 2 + 8;
    b_out_y = row_y + isz / 2;
    ui_flow_set_route(v.to_load, jx, jy, lx, ly_icon, AXIS_V, AXIS_V, 1);
    ui_flow_set_route(v.from_batt, b_out_x, b_out_y, lcx - isz / 2 - 8, row_y + isz / 2, AXIS_H, AXIS_H, 0);
    ui_flow_set_route(v.to_batt, jx, jy, bx, by_top, AXIS_V, AXIS_V, -1);
  }
  lv_obj_set_pos(v.bus, jx - 6, jy - 6);
  v.mask = mask;
  v.laid_out = true;
}

void power_summary_build(lv_obj_t *area, size_class_t sc, role_binding_t *b) {
  memset(&v, 0, sizeof v);
  v.W = lv_obj_get_width(area);
  v.H = lv_obj_get_height(area);
  v.sc = sc;
  power_topo_t topo;
  power_topo_resolve(b, &topo);
  v.multi = topo.active;
  bool nav = topo.active && topo.bank_cfg[BANK_NAV];
  int32_t strip_h = 0;
  if (nav && sc != SIZE_WIDE) {   // no room for a fourth node: one text line under the diagram
    strip_h = 28;
    v.H -= strip_h;
  }
  uint8_t idx[ROLE_MAX_INST], primary;
  uint8_t n = power_banks(b, idx, &primary);
  role_gauge_spec(b, G_BATTERY, n ? idx[primary] : 0, B_SOC, &v.spec, v.bands, v.label);
  v.spec.decimals = 0;
  v.unit = units_label(Q_POWER, b->units);

  // Arrows first, so the nodes draw over their ends.
  for (int k = 0; k < N_COUNT; k++) v.src[k].flow = ui_flow_create(area);
  v.to_load = ui_flow_create(area);
  v.to_batt = ui_flow_create(area);
  v.from_batt = ui_flow_create(area);
  if (nav && sc == SIZE_WIDE) v.nav_flow = ui_flow_create(area);
  v.bus = lv_obj_create(area);
  lv_obj_remove_style_all(v.bus);
  lv_obj_remove_flag(v.bus, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(v.bus, 12, 12);
  lv_obj_set_style_radius(v.bus, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(v.bus, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(v.bus, ui_theme()->accent, 0);

  // With banks, the engine node is the converter that carries charge from the start bank to the house.
  static const ui_icon_t ICONS[N_COUNT] = {ICON_SOLAR, ICON_SHORE, ICON_ENGINE, ICON_ENGINE};
  static const char *const NAMES[N_COUNT] = {"SOLAR", "SHORE", "ENGINE", "IN"};
  static const char START_LINK[] = "START" LV_SYMBOL_RIGHT "HOUSE";
  for (int k = 0; k < N_COUNT; k++) {
    bool link = v.multi && k == N_ENGINE;
    v.src[k].icon = ui_icon_create(area, link ? ICON_CONVERTER : ICONS[k]);
    v.src[k].val = ui_readout_create(area, link ? START_LINK : NAMES[k], v.unit, 0);
  }
  if (nav && sc == SIZE_WIDE) {
    v.nav.icon = ui_icon_create(area, ICON_BATTERY);
    v.nav.val = ui_readout_create(area, "NAV LOAD", v.unit, 0);
    ui_field_set_fault_text(v.nav.val, "NAV FAIL");
    ui_readout_set_centered(v.nav.val, true);
  }
  v.batt.icon = ui_icon_create(area, ICON_BATTERY);
  v.batt.val = ui_readout_create(area, "BATTERY", units_label(Q_RATIO, b->units), 0);
  ui_field_set_fault_text(v.batt.val, "SOC FAIL");
  v.load.icon = ui_icon_create(area, ICON_LOAD);
  v.load.val = ui_readout_create(area, "DC LOAD", v.unit, 0);   // AC loads fed from shore never reach the DC side
  ui_field_set_fault_text(v.load.val, "LOAD FAIL");
  ui_readout_set_centered(v.batt.val, true);
  ui_readout_set_centered(v.load.val, true);
  if (sc != SIZE_WIDE)
    for (int k = 0; k < N_COUNT; k++) ui_readout_set_centered(v.src[k].val, true);   // under their icons
  if (strip_h) {
    v.nav_strip = lv_label_create(area);
    lv_obj_set_style_text_font(v.nav_strip, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(v.nav_strip, ui_theme()->dim, 0);
    lv_obj_set_style_text_align(v.nav_strip, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(v.nav_strip, v.W);
    lv_obj_set_pos(v.nav_strip, 0, v.H + 2);
    lv_label_set_text(v.nav_strip, "NAV");
  }
  relayout(1u << N_ANY);
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

static void flow_label(char *out, size_t n, double watts, const role_binding_t *b, const char *what) {
  double w = units_from_si(Q_POWER, watts, b->units);
  if (v.sc == SIZE_WIDE) snprintf(out, n, "%.0f %s %s", w, v.unit, what);
  else snprintf(out, n, "%.0f %s", w, v.unit);   // narrow: the arrow says where it goes
}

static void set_flow(lv_obj_t *f, bool ok, double watts, double ref, const role_binding_t *b, const char *what) {
  char text[32] = "";
  if (ok) flow_label(text, sizeof text, watts, b, what);
  ui_flow_set(f, ui_flow_level_for(watts, ref), ok, text);
}

static lv_color_t soc_color(double pct) {
  for (uint8_t i = 0; i < v.spec.band_count; i++)
    if (pct >= v.spec.bands[i].lo && pct <= v.spec.bands[i].hi) return v.spec.bands[i].color;
  return ui_theme()->accent;
}

void power_summary_refresh(role_binding_t *b, int64_t now) {
  // Sources in node order, so each node's instances are a contiguous range of src[].
  static const struct { uint8_t node, g; } SRC[] = {
      {N_SOLAR, G_SOLAR}, {N_SHORE, G_CHARGER}, {N_ENGINE, G_DCDC}, {N_ENGINE, G_ALTERNATOR}};
  PLATFORM_EXT_BSS static bd_sample_t st[MAX_SRC][3];
  power_src_t src[MAX_SRC];
  size_t n = 0, from[N_COUNT] = {0}, to[N_COUNT] = {0};
  uint8_t present = 0;
  power_topo_t topo;
  power_topo_resolve(b, &topo);
  if (topo.active) {
    // Multi-bank: the house bus is fed by the links to the house bank, not by every source on the boat.
    // SOLAR: solar links. SHORE: charger links (its main output). ENGINE: converters drawing from START.
    for (uint8_t nd = N_SOLAR; nd <= N_ENGINE; nd++) {
      from[nd] = n;
      for (uint8_t k = 0; k < topo.link_count && n < MAX_SRC; k++) {
        const power_link_t *l = &topo.link[k];
        bool match = nd == N_SOLAR ? l->g == G_SOLAR : nd == N_SHORE ? l->g == G_CHARGER : (l->g == G_DCDC && l->from == BANK_START);
        if (!match || l->to != BANK_HOUSE || l->output > 0) continue;
        src[n] = (power_src_t){0};   // an unbound source stays in as an unusable reading and fails the node
        if (l->bound) power_src_of(b, l->g, l->i, C_VOLTS, C_AMPS, -1, st[n], &src[n]);
        n++;
        present |= 1u << nd;
      }
      to[nd] = n;
    }
  } else {
    for (size_t c = 0; c < sizeof SRC / sizeof SRC[0]; c++) {
      uint8_t g = SRC[c].g, nd = SRC[c].node;
      if (!(present & (1u << nd))) from[nd] = n;
      for (uint8_t i = 0; i < b->g[g].inst_count && n < MAX_SRC; i++)
        if (power_real(b, g, i)) {
          power_src_of(b, g, i, C_VOLTS, C_AMPS, -1, st[n], &src[n]);   // V x I: watts into the battery
          n++;
          present |= 1u << nd;
        }
      to[nd] = n;
    }
  }
  bool on_shore = shore(b);
  if (on_shore) present |= 1u << N_SHORE;
  uint8_t mask = present ? present : (1u << N_ANY);
  if (mask != v.mask || !v.laid_out) relayout(mask);

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
  power_banks_view_t bv = {0};
  if (topo.active) {   // bank figures replace the single-bank ones; the Cerbo's dcPower is not used
    power_banks_compute(b, &topo, now, &bv);
    r = (power_summary_t){.in_ok = bv.fig.house_in.ok, .in_w = bv.fig.house_in.w,
                          .batt_ok = bv.fig.house_batt.ok, .batt_w = bv.fig.house_batt.w,
                          .load_ok = bv.fig.house_load.ok, .load_w = bv.fig.house_load.w,
                          .soc_ok = bv.house_soc_ok, .soc = bv.house_soc};
  }
  power_flows_t f = power_flows(&r);

  // Each source node, from its own instances. Watts are the positive charge power, as in IN.
  double cat_w[N_COUNT] = {0}, ref = 0;
  bool cat_ok[N_COUNT] = {0};
  for (int k = 0; k < N_COUNT; k++) {
    bool shown = mask & (1u << k);
    if (!shown) continue;
    if (k == N_ANY) {   // nothing bound: the whole IN fails
      cat_ok[k] = false;
    } else if (to[k] > from[k]) {
      power_summary_t s = power_summarize(src + from[k], to[k] - from[k], NULL, NULL, NULL, 0, now);
      cat_ok[k] = s.in_ok;
      cat_w[k] = s.in_w;
    }
    if (cat_ok[k] && cat_w[k] > ref) ref = cat_w[k];
  }
  if (f.to_load_ok && f.to_load > ref) ref = f.to_load;
  if (f.batt_ok && f.to_batt > ref) ref = f.to_batt;
  if (f.batt_ok && f.from_batt > ref) ref = f.from_batt;
  if (topo.active && bv.fig.nav_feed.ok && bv.fig.nav_feed.w > ref) ref = bv.fig.nav_feed.w;

  for (int k = 0; k < N_COUNT; k++) {
    node_t *nd = &v.src[k];
    if (!(mask & (1u << k))) continue;
    if (k == N_SHORE && to[k] == from[k]) {   // shore reported by the inverter only: no watts to show
      ui_readout_set_text(nd->val, "ON", true);
      ui_icon_set_active(nd->icon, true);
      ui_flow_set(nd->flow, 0, true, NULL);
      continue;
    }
    ui_readout_set(nd->val, units_from_si(Q_POWER, cat_w[k], b->units), cat_ok[k]);
    ui_icon_set_active(nd->icon, cat_ok[k] && cat_w[k] >= 0.5);
    ui_flow_set(nd->flow, ui_flow_level_for(cat_w[k], ref), cat_ok[k], NULL);
  }

  ui_readout_set(v.load.val, units_from_si(Q_POWER, r.load_w, b->units), r.load_ok);
  ui_icon_set_active(v.load.icon, r.load_ok && r.load_w >= 0.5);
  double pct = units_from_si(Q_RATIO, r.soc, b->units);
  ui_readout_set(v.batt.val, pct, r.soc_ok);
  ui_icon_set_fill(v.batt.icon, r.soc, r.soc_ok, soc_color(pct));
  if (banks && !(topo.active && !topo.bank_bound[BANK_HOUSE]))
    role_field_alert(v.batt.val, b->g[G_BATTERY].ids[idx[primary]][B_SOC]);

  set_flow(v.to_load, f.to_load_ok, f.to_load, ref, b, "to load");
  set_flow(v.to_batt, f.batt_ok, f.to_batt, ref, b, "to battery");
  set_flow(v.from_batt, f.batt_ok, f.from_batt, ref, b, "from battery");

  if (topo.active) {   // the START link's battery fills to START's charge; NAV shows its load and charge
    ui_icon_set_fill(v.src[N_ENGINE].icon, bv.start_soc, bv.start_soc_ok, soc_color(units_from_si(Q_RATIO, bv.start_soc, b->units)));
    const power_fig_t *nl = &bv.fig.nav_load;
    if (v.nav.icon) {
      ui_readout_set(v.nav.val, units_from_si(Q_POWER, nl->w, b->units), nl->ok);
      ui_icon_set_active(v.nav.icon, nl->ok && nl->w >= 0.5);
      ui_icon_set_fill(v.nav.icon, bv.nav_soc, bv.nav_soc_ok, soc_color(units_from_si(Q_RATIO, bv.nav_soc, b->units)));
      set_flow(v.nav_flow, bv.fig.nav_feed.ok, bv.fig.nav_feed.w, ref, b, "to nav");
    }
    if (v.nav_strip) {
      char soc_t[16] = "SOC FAIL", load_t[24] = "LOAD FAIL", t[64];
      if (bv.nav_soc_ok) snprintf(soc_t, sizeof soc_t, "%.0f %%", units_from_si(Q_RATIO, bv.nav_soc, b->units));
      if (nl->ok) snprintf(load_t, sizeof load_t, "%.0f %s", units_from_si(Q_POWER, nl->w, b->units), v.unit);
      snprintf(t, sizeof t, "NAV  %s  %s", soc_t, load_t);
      lv_label_set_text(v.nav_strip, t);
      lv_obj_set_style_text_color(v.nav_strip, bv.nav_soc_ok && nl->ok ? ui_theme()->dim : ui_theme()->fault_label, 0);
    }
  }
}
