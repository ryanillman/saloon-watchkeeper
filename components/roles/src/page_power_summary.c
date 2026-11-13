// Power Summary: a flow diagram. Sources (solar, shore, engine) feed a bus, the bus feeds the
// battery and the load, and a discharging battery feeds the load too. Arrows carry the direction
// and size of each flow and move while power flows. The battery icon fills to the state of charge.
// Wide: sources stacked on the left, load top right, battery bottom centre. Square: sources along
// the top, battery bottom left, load bottom right. Placeholder instances are not sources.
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "power_page.h"
#include "power_topo.h"
#include "ui_flow.h"

#define MAX_SRC 16   // 4 solar + 8 charger + 4 alternator

// Source nodes, from the flows into the house bank. SOLAR is the solar flows. SHORE is the shore flows: it means AC
// present (power_shore), with the flows' watts. ENGINE is the engine flows and the converters drawing from the
// start bank. ANY stands in when no flow reaches the house bank.
enum { N_SOLAR, N_SHORE, N_ENGINE, N_ANY, N_COUNT };

typedef struct { lv_obj_t *icon, *val, *flow; } node_t;

PLATFORM_EXT_BSS static struct {
  node_t src[N_COUNT];
  node_t batt, load;
  node_t nav;                  // multi-bank, wide: the nav bank, fed from the house battery
  lv_obj_t *nav_flow;
  lv_obj_t *nav_strip;         // multi-bank, square: one text line under the diagram
  bool multi;                  // a house bank is configured
  bool idle;                   // not set up: the page only says so
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

static void on_setup(lv_event_t *e) {
  (void)e;
  power_open_setup();
}

void power_summary_build(lv_obj_t *area, size_class_t sc, role_binding_t *b) {
  memset(&v, 0, sizeof v);
  v.W = lv_obj_get_width(area);
  v.H = lv_obj_get_height(area);
  v.sc = sc;
  power_topo_t topo;
  power_topo_resolve(b, &topo);
  v.multi = topo.active;
  if (!topo.active) {   // nothing to draw until a house bank is placed
    v.idle = true;
    const ui_colors_t *col = ui_theme();
    power_text(area, "POWER IS NOT SET UP", &lv_font_montserrat_28, col->fg, 0, v.H / 2 - 70, v.W, LV_TEXT_ALIGN_CENTER);
    power_text(area, "Place the house battery and what charges it", &lv_font_montserrat_20, col->dim, 0, v.H / 2 - 28, v.W,
               LV_TEXT_ALIGN_CENTER);
    lv_obj_t *btn = lv_button_create(area);
    lv_obj_set_size(btn, 240, 56);
    lv_obj_set_pos(btn, v.W / 2 - 120, v.H / 2 + 24);
    lv_obj_set_style_bg_color(btn, col->strip_bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, col->accent, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, on_setup, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(btn);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(l, col->fg, 0);
    ui_label_set(l, "SET UP POWER");
    lv_obj_center(l);
    return;
  }
  uint8_t unassigned = (uint8_t)(topo.unassigned_batt + topo.unassigned_dev);
  if (unassigned) {   // a band along the bottom, tapping it opens Setup > Power
    v.H -= 28;
    char t[24];
    snprintf(t, sizeof t, "%u UNASSIGNED", unassigned);
    lv_obj_t *m = power_text(area, t, &lv_font_montserrat_20, ui_theme()->accent, 0, v.H + 2, v.W, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_height(m, 26);
    lv_obj_add_flag(m, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(m, on_setup, LV_EVENT_CLICKED, NULL);
  }
  bool nav = topo.nav >= 0;
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
    ui_label_set(v.nav_strip, "NAV");
  }
  relayout(1u << N_ANY);
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
  if (v.idle) return;
  PLATFORM_EXT_BSS static bd_sample_t st[MAX_SRC][3];
  power_src_t src[MAX_SRC];
  size_t n = 0, from[N_COUNT] = {0}, to[N_COUNT] = {0};
  uint8_t present = 0;
  power_topo_t topo;
  power_topo_resolve(b, &topo);
  if (!topo.active) return;   // the bindings changed under the page; the shell rebuilds it
  // The house bus is fed by the flows into the house bank, not by every source on the boat.
  for (uint8_t nd = N_SOLAR; nd <= N_ENGINE; nd++) {
    from[nd] = n;
    for (uint8_t k = 0; k < topo.flow_count && n < MAX_SRC; k++) {
      const power_flow_t *l = &topo.flow[k];
      bool match = nd == N_SOLAR ? l->kind == FLOW_SOLAR
                 : nd == N_SHORE ? l->kind == FLOW_SHORE
                 : (l->kind == FLOW_ENGINE || (l->kind == FLOW_CONVERTER && l->from == topo.start && topo.start >= 0));
      if (!match || l->to != topo.house) continue;
      src[n] = (power_src_t){0};   // an unseen source stays in as an unusable reading and fails the node
      if (l->bound) power_src_of(b, l->g, l->i, C_VOLTS, C_AMPS, -1, st[n], &src[n]);
      n++;
      present |= 1u << nd;
    }
    to[nd] = n;
  }
  uint8_t mask = present ? present : (1u << N_ANY);
  if (mask != v.mask || !v.laid_out) relayout(mask);

  power_banks_view_t bv;
  power_banks_compute(b, &topo, now, &bv);
  const power_bank_view_t *hv = &bv.bank[topo.house];   // the Cerbo's dcPower is not used
  power_summary_t r = {.in_ok = hv->fig.in.ok, .in_w = hv->fig.in.w,
                       .batt_ok = hv->fig.net.ok, .batt_w = hv->fig.net.w,
                       .load_ok = hv->fig.load.ok, .load_w = hv->fig.load.w,
                       .soc_ok = hv->fig.net.ok && hv->soc_ok, .soc = hv->soc};
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
  if (topo.active && bv.bank[topo.house].fig.feed.ok && bv.bank[topo.house].fig.feed.w > ref) ref = bv.bank[topo.house].fig.feed.w;

  for (int k = 0; k < N_COUNT; k++) {
    node_t *nd = &v.src[k];
    if (!(mask & (1u << k))) continue;
    if (k == N_SHORE) {   // shore means AC present; the watts are the shore flows' output
      double sw;
      shore_t ss = power_shore(b, &topo, now, &sw);
      bool present = ss == SHORE_PRESENT || ss == SHORE_INFERRED_PRESENT;
      char txt[24];
      if (!present) snprintf(txt, sizeof txt, "OFF");
      else if (ss == SHORE_INFERRED_PRESENT) snprintf(txt, sizeof txt, "%.0f EST", units_from_si(Q_POWER, sw, b->units));   // no AC data: inferred from the output
      else snprintf(txt, sizeof txt, "%.0f", units_from_si(Q_POWER, sw, b->units));
      ui_readout_set_text(nd->val, txt, ss != SHORE_UNKNOWN);
      ui_icon_set_active(nd->icon, present);
      ui_flow_set(nd->flow, present ? ui_flow_level_for(sw, ref) : 0, ss != SHORE_UNKNOWN, NULL);
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
  if (topo.bank[topo.house].bound)
    role_field_alert(v.batt.val, b->g[G_BATTERY].ids[topo.bank[topo.house].inst][B_SOC]);

  set_flow(v.to_load, f.to_load_ok, f.to_load, ref, b, "to load");
  set_flow(v.to_batt, f.batt_ok, f.to_batt, ref, b, "to battery");
  set_flow(v.from_batt, f.batt_ok, f.from_batt, ref, b, "from battery");

  {   // the START link's battery fills to START's charge; NAV shows its load and charge
    bool start_ok = topo.start >= 0 && bv.bank[topo.start].soc_ok;
    double start_soc = start_ok ? bv.bank[topo.start].soc : 0;
    bool nav_soc_ok = topo.nav >= 0 && bv.bank[topo.nav].soc_ok;
    double nav_soc = nav_soc_ok ? bv.bank[topo.nav].soc : 0;
    ui_icon_set_fill(v.src[N_ENGINE].icon, start_soc, start_ok, soc_color(units_from_si(Q_RATIO, start_soc, b->units)));
    static const power_fig_t NO_FIG = {0};
    const power_fig_t *nl = topo.nav >= 0 ? &bv.bank[topo.nav].fig.load : &NO_FIG;
    if (v.nav.icon) {
      ui_readout_set(v.nav.val, units_from_si(Q_POWER, nl->w, b->units), nl->ok);
      ui_icon_set_active(v.nav.icon, nl->ok && nl->w >= 0.5);
      ui_icon_set_fill(v.nav.icon, nav_soc, nav_soc_ok, soc_color(units_from_si(Q_RATIO, nav_soc, b->units)));
      set_flow(v.nav_flow, bv.bank[topo.house].fig.feed.ok, bv.bank[topo.house].fig.feed.w, ref, b, "to nav");
    }
    if (v.nav_strip) {
      char soc_t[16] = "SOC FAIL", load_t[24] = "LOAD FAIL", t[64];
      if (nav_soc_ok) snprintf(soc_t, sizeof soc_t, "%.0f %%", units_from_si(Q_RATIO, nav_soc, b->units));
      if (nl->ok) snprintf(load_t, sizeof load_t, "%.0f %s", units_from_si(Q_POWER, nl->w, b->units), v.unit);
      snprintf(t, sizeof t, "NAV  %s  %s", soc_t, load_t);
      ui_label_set(v.nav_strip, t);
      lv_obj_set_style_text_color(v.nav_strip, nav_soc_ok && nl->ok ? ui_theme()->dim : ui_theme()->fault_label, 0);
    }
  }
}
