// Power pages. Summary lives in page_power_summary.c. Batteries: one row per bank
// (volts, amps, watts, state-of-charge strip, time remaining). Charging: one row per
// solar, charger, DC-DC and alternator instance. Placeholder instances (a group that
// found nothing) are skipped, except a missing battery, which shows as a failed row.
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "power_page.h"
#include "power_topo.h"

#define CHARGE_GROUPS 4
static const uint8_t CHARGE[CHARGE_GROUPS] = {G_SOLAR, G_CHARGER, G_DCDC, G_ALTERNATOR};
static const char *const GROUP_LABEL[] = {"BATTERY", "SOLAR", "CHARGER", "INVERTER", "DCDC", "ALTERNATOR"};

static int built_page = -1;
#define RENT 12   // charging rows that can exist: 4 + 4 + 2 + 2

PLATFORM_EXT_BSS static struct {
  uint8_t n;
  uint8_t idx[ROLE_MAX_INST];
  lv_obj_t *v[ROLE_MAX_INST], *a[ROLE_MAX_INST], *w[ROLE_MAX_INST], *soc[ROLE_MAX_INST], *ttg[ROLE_MAX_INST];
  ui_gauge_spec_t spec[ROLE_MAX_INST];
  ui_band_t bands[ROLE_MAX_INST][6];
  char gl[ROLE_MAX_INST][32];       // gauge label storage
  char slabel[ROLE_MAX_INST][32];   // row title
  lv_obj_t *src[ROLE_MAX_INST];     // multi-bank: the sources feeding this bank
  int8_t kind[ROLE_MAX_INST];       // multi-bank: BANK_*, -1 = a battery no bank claims
  power_topo_t topo;
  bool wide;
} bat;

static struct {
  uint8_t n, g[RENT], i[RENT];
  lv_obj_t *w[RENT], *a[RENT], *panel[RENT], *mode[RENT];
  char slabel[RENT][32];
  char dest[RENT][40];              // multi-bank: where the row's power goes
} chg;

// ---- shared helpers ----

bool power_real(const role_binding_t *b, uint8_t g, uint8_t i) { return b->g[g].ids[i][0] != BD_NONE; }

bool power_sample(const role_binding_t *b, uint8_t g, uint8_t i, uint8_t k, bd_sample_t *out) {
  return b->g[g].ids[i][k] != BD_NONE && bd_get(b->g[g].ids[i][k], out);
}

bool power_seen(const bd_sample_t *s) { return s->valid || s->rx_ms != 0; }

void power_src_of(const role_binding_t *b, uint8_t g, uint8_t i, int kv, int ki, int kp, bd_sample_t store[3],
                  power_src_t *out) {
  const int k[3] = {kv, ki, kp};
  const bd_sample_t *p[3] = {NULL, NULL, NULL};
  for (int x = 0; x < 3; x++)
    if (k[x] >= 0 && power_sample(b, g, i, (uint8_t)k[x], &store[x])) p[x] = &store[x];
  *out = (power_src_t){p[0], p[1], p[2]};
}

bool power_watts(const power_src_t *s, int64_t now_ms, double *w) {
  power_summary_t r = power_summarize(NULL, 0, s, NULL, NULL, 0, now_ms);   // the "battery" path keeps the sign
  *w = r.batt_w;
  return r.batt_ok;
}

void power_label(const role_binding_t *b, uint8_t g, uint8_t i, char *out, size_t n) {
  char dflt[24];
  size_t k = 0;
  for (; b->g[g].inst_id[i][k] && k + 1 < sizeof dflt; k++) dflt[k] = (char)toupper((unsigned char)b->g[g].inst_id[i][k]);
  dflt[k] = 0;
  if (strcmp(dflt, b->g[g].inst_label[i]) == 0) snprintf(out, n, "%s %s", GROUP_LABEL[g], dflt);
  else snprintf(out, n, "%s", b->g[g].inst_label[i]);
}

uint8_t power_banks(const role_binding_t *b, uint8_t idx[ROLE_MAX_INST], uint8_t *primary) {
  const role_group_binding_t *gb = &b->g[G_BATTERY];
  uint8_t n = 0;
  char ids[ROLE_MAX_INST][24];
  bool soc_bound[ROLE_MAX_INST];
  for (uint8_t i = 0; i < gb->inst_count; i++) {
    if (!power_real(b, G_BATTERY, i)) continue;
    bd_sample_t s;
    idx[n] = i;
    snprintf(ids[n], sizeof ids[n], "%s", gb->inst_id[i]);
    soc_bound[n] = power_sample(b, G_BATTERY, i, B_SOC, &s) && power_seen(&s);
    n++;
  }
  const char *want = b->cfg ? b->cfg->primary : NULL;
  if (b->cfg)   // configured banks: the house bank is the primary, and `primary` is ignored
    for (uint8_t k = 0; k < b->cfg->bank_count; k++)
      if (!strcmp(b->cfg->bank[k].kind, "house")) want = b->cfg->bank[k].id;
  *primary = n ? power_primary(ids, soc_bound, n, want) : 0;
  return n;
}

static bool num(const role_binding_t *b, uint8_t g, uint8_t i, uint8_t k, double *out) {
  bd_sample_t s;
  if (!power_sample(b, g, i, k, &s) || s.kind != BD_NUMBER || !s.valid) return false;
  *out = units_from_si(b->def->groups[g].slots[k].q, s.num, b->units);
  return true;
}

static void upper(char *dst, size_t n, const char *src) {
  size_t i = 0;
  for (; src[i] && i + 1 < n; i++) dst[i] = (char)toupper((unsigned char)src[i]);
  dst[i] = 0;
}

static bool text(const role_binding_t *b, uint8_t g, uint8_t i, uint8_t k, char *out, size_t n) {
  bd_sample_t s;
  if (!power_sample(b, g, i, k, &s) || s.kind != BD_TEXT || !s.valid) return false;
  upper(out, n, s.text);
  return true;
}

static lv_obj_t *readout(lv_obj_t *area, const role_binding_t *b, uint8_t g, uint8_t k, uint8_t dec, int32_t x,
                         int32_t y, int32_t w, int32_t h) {
  const role_slot_t *sl = &b->def->groups[g].slots[k];
  lv_obj_t *o = ui_readout_create(area, sl->fail_label, units_label(sl->q, b->units), dec);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  return o;
}

lv_obj_t *power_text(lv_obj_t *area, const char *txt, const lv_font_t *f, lv_color_t col, int32_t x, int32_t y,
                     int32_t w, lv_text_align_t al) {
  lv_obj_t *l = lv_label_create(area);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, col, 0);
  lv_obj_set_style_text_align(l, al, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, w);
  lv_obj_set_pos(l, x, y);
  return l;
}

static void caption(lv_obj_t *area, const char *txt, int32_t x, int32_t y, int32_t w, const lv_font_t *f) {
  lv_obj_t *l = lv_label_create(area);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, ui_theme()->accent, 0);
  lv_obj_set_width(l, w);
  lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
  lv_obj_set_pos(l, x, y);
}

// ---- page counts ----

#define PER_PAGE(sc) ((sc) == SIZE_WIDE ? 6 : 4)

static uint8_t charge_rows(const role_binding_t *b, uint8_t g[RENT], uint8_t i[RENT]) {
  uint8_t n = 0;
  for (uint8_t c = 0; c < CHARGE_GROUPS; c++)
    for (uint8_t k = 0; k < b->g[CHARGE[c]].inst_count && n < RENT; k++)
      if (power_real(b, CHARGE[c], k)) {
        if (g) g[n] = CHARGE[c];
        if (i) i[n] = k;
        n++;
      }
  if (power_multi(b) && g && i) {   // grouped by the bank they feed: house, start, nav, then sources no link names
    power_topo_t t;
    power_topo_resolve(b, &t);
    int key[RENT];
    for (uint8_t k = 0; k < n; k++) { int d = power_topo_dest(&t, g[k], i[k], NULL); key[k] = d < 0 ? 9 : d; }
    for (uint8_t k = 1; k < n; k++)   // insertion sort, stable
      for (uint8_t m = k; m > 0 && key[m] < key[m - 1]; m--) {
        int tk = key[m]; key[m] = key[m - 1]; key[m - 1] = tk;
        uint8_t tg = g[m]; g[m] = g[m - 1]; g[m - 1] = tg;
        uint8_t ti = i[m]; i[m] = i[m - 1]; i[m - 1] = ti;
      }
  }
  return n;
}

uint8_t power_pages(const role_binding_t *b, size_class_t sc) {
  uint8_t rows = charge_rows(b, NULL, NULL), per = PER_PAGE(sc);
  uint8_t pages = (uint8_t)((rows + per - 1) / per);
  return (uint8_t)(2 + (power_multi(b) ? 1 : 0) + (pages ? pages : 1));
}

// ---- Batteries ----

static void batteries_build(lv_obj_t *area, size_class_t sc, role_binding_t *b) {
  int32_t W = lv_obj_get_width(area), H = lv_obj_get_height(area), pad = 8;
  bool wide = sc == SIZE_WIDE;
  uint8_t primary;
  bat.n = power_banks(b, bat.idx, &primary);
  memset(bat.kind, -1, sizeof bat.kind);
  bat.wide = wide;
  power_topo_resolve(b, &bat.topo);
  if (bat.topo.active) {   // the configured banks in config order, then any battery no bank claims
    uint8_t order[ROLE_MAX_INST], m = 0, real[ROLE_MAX_INST];
    int8_t kinds[ROLE_MAX_INST];
    memcpy(real, bat.idx, sizeof real);
    uint8_t nreal = bat.n;
    bool taken[ROLE_MAX_INST] = {0};
    for (uint8_t k = 0; k < b->cfg->bank_count; k++) {
      int kind = !strcmp(b->cfg->bank[k].kind, "house") ? BANK_HOUSE : !strcmp(b->cfg->bank[k].kind, "start") ? BANK_START : BANK_NAV;
      if (!bat.topo.bank_bound[kind]) continue;
      order[m] = bat.topo.bank_inst[kind];
      kinds[m++] = (int8_t)kind;
      for (uint8_t r = 0; r < nreal; r++) if (real[r] == bat.topo.bank_inst[kind]) taken[r] = true;
    }
    for (uint8_t r = 0; r < nreal && m < ROLE_MAX_INST; r++)
      if (!taken[r]) { order[m] = real[r]; kinds[m++] = -1; }
    memcpy(bat.idx, order, m);
    memcpy(bat.kind, kinds, m);
    bat.n = m;
  }
  if (!bat.n) {   // no bank found: keep the placeholder row so its fields flag
    bat.n = 1;
    bat.idx[0] = 0;
  }
  int32_t bh = (H - pad) / bat.n - pad;
  if (bh > 110) bh = 110;
  int32_t title_w = wide ? 175 : 0, title_h = wide ? 0 : 24;
  for (uint8_t j = 0; j < bat.n; j++) {
    uint8_t i = bat.idx[j];
    int32_t y = pad + j * (bh + pad), x0 = pad + title_w, inner = W - x0 - pad;
    if (bat.topo.active && bat.kind[j] >= 0) snprintf(bat.slabel[j], sizeof bat.slabel[j], "%s", bat.topo.bank_name[bat.kind[j]]);
    else if (bat.topo.active) {
      power_label(b, G_BATTERY, i, bat.slabel[j], sizeof bat.slabel[j]);
      size_t len = strlen(bat.slabel[j]);
      snprintf(bat.slabel[j] + len, sizeof bat.slabel[j] - len, " (UNASSIGNED)");
    } else power_label(b, G_BATTERY, i, bat.slabel[j], sizeof bat.slabel[j]);
    bool src_line = bat.topo.active && bat.kind[j] >= 0;   // the bank's own sources, under the name (wide) or beside it
    caption(area, bat.slabel[j], pad, wide ? y + bh / 2 - (src_line ? 26 : 12) : y, wide ? title_w : W - 2 * pad, &lv_font_montserrat_20);
    if (src_line)
      bat.src[j] = wide ? power_text(area, "", &lv_font_montserrat_14, ui_theme()->dim, pad, y + bh / 2 + 2, title_w, LV_TEXT_ALIGN_LEFT)
                        : power_text(area, "", &lv_font_montserrat_14, ui_theme()->dim, W / 3, y + 4, W * 2 / 3 - pad, LV_TEXT_ALIGN_RIGHT);
    role_gauge_spec(b, G_BATTERY, i, B_SOC, &bat.spec[j], bat.bands[j], bat.gl[j]);
    bat.spec[j].decimals = 0;
    if (wide) {   // volts, amps, watts, strip, time on one line
      int32_t u = (inner - 4 * pad) * 10 / 62;   // three readouts of 1, the strip 2, time 1.1
      int32_t x = x0;
      bat.v[j] = readout(area, b, G_BATTERY, B_VOLTS, 2, x, y, u, bh);
      x += u + pad;
      bat.a[j] = readout(area, b, G_BATTERY, B_AMPS, 1, x, y, u, bh);
      x += u + pad;
      bat.w[j] = readout(area, b, G_BATTERY, B_POWER, 0, x, y, u, bh);
      x += u + pad;
      bat.soc[j] = ui_strip_create(area, &bat.spec[j], false);
      lv_obj_set_pos(bat.soc[j], x, y);
      lv_obj_set_size(bat.soc[j], 2 * u, bh);
      x += 2 * u + pad;
      bat.ttg[j] = readout(area, b, G_BATTERY, B_TIME, 0, x, y, inner - (x - x0), bh);
    } else {      // title, then readouts, then strip and time
      int32_t rh = (bh - title_h) / 2, u = (W - 4 * pad) / 3, y1 = y + title_h, y2 = y1 + rh;
      bat.v[j] = readout(area, b, G_BATTERY, B_VOLTS, 2, pad, y1, u, rh);
      bat.a[j] = readout(area, b, G_BATTERY, B_AMPS, 1, 2 * pad + u, y1, u, rh);
      bat.w[j] = readout(area, b, G_BATTERY, B_POWER, 0, 3 * pad + 2 * u, y1, u, rh);
      int32_t sw = (W - 3 * pad) * 6 / 10;
      bat.soc[j] = ui_strip_create(area, &bat.spec[j], false);
      lv_obj_set_pos(bat.soc[j], pad, y2);
      lv_obj_set_size(bat.soc[j], sw, rh);
      bat.ttg[j] = readout(area, b, G_BATTERY, B_TIME, 0, 2 * pad + sw, y2, W - 3 * pad - sw, rh);
    }
  }
}

static void time_text(double seconds, char *out, size_t n) {
  int32_t m = (int32_t)(seconds / 60.0);
  if (m < 0) m = 0;
  if (m > 99 * 60 + 59) m = 99 * 60 + 59;
  snprintf(out, n, "%d:%02d", (int)(m / 60), (int)(m % 60));
}

static void batteries_refresh(role_binding_t *b, int64_t now) {
  for (uint8_t j = 0; j < bat.n; j++) {
    uint8_t i = bat.idx[j];
    double x;
    bool ok = num(b, G_BATTERY, i, B_VOLTS, &x);
    ui_readout_set(bat.v[j], x, ok);
    role_field_alert(bat.v[j], b->g[G_BATTERY].ids[i][B_VOLTS]);
    ok = num(b, G_BATTERY, i, B_AMPS, &x);
    ui_readout_set(bat.a[j], x, ok);
    role_field_alert(bat.a[j], b->g[G_BATTERY].ids[i][B_AMPS]);
    bd_sample_t st[3];
    power_src_t src;
    power_src_of(b, G_BATTERY, i, B_VOLTS, B_AMPS, B_POWER, st, &src);
    ok = power_watts(&src, now, &x);
    ui_readout_set(bat.w[j], x, ok);
    ok = num(b, G_BATTERY, i, B_SOC, &x);
    ui_strip_set(bat.soc[j], x, ok);
    role_field_alert(bat.soc[j], b->g[G_BATTERY].ids[i][B_SOC]);

    bd_sample_t ttg, cur;
    bool ht = power_sample(b, G_BATTERY, i, B_TIME, &ttg), hc = power_sample(b, G_BATTERY, i, B_AMPS, &cur);
    char t[16] = "";
    switch (power_time_remaining(ht ? &ttg : NULL, hc ? &cur : NULL, 0, now)) {
      case TTG_VALUE: time_text(ttg.num, t, sizeof t); break;
      case TTG_CHARGING: snprintf(t, sizeof t, "CHARGING"); break;   // not a fault
      case TTG_FAIL: break;
    }
    ui_readout_set_text(bat.ttg[j], t, t[0] != 0);
    if (bat.src[j]) {
      char txt[96];
      power_bank_sources(b, &bat.topo, bat.kind[j], now, bat.wide ? "\n" : "   ", txt, sizeof txt);
      lv_label_set_text(bat.src[j], txt);
    }
  }
}

// ---- Charging ----

// Summary, [Banks], Batteries, then the Charging pages.
static uint8_t batteries_page(const role_binding_t *b) { return power_multi(b) ? 2 : 1; }
static uint8_t first_charging_page(const role_binding_t *b) { return (uint8_t)(batteries_page(b) + 1); }

static void charging_build(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b) {
  int32_t W = lv_obj_get_width(area), H = lv_obj_get_height(area), pad = 8;
  bool wide = sc == SIZE_WIDE;
  uint8_t g[RENT], i[RENT], total = charge_rows(b, g, i), per = PER_PAGE(sc);
  uint8_t first = (uint8_t)((page - first_charging_page(b)) * per);
  chg.n = first >= total ? 0 : (uint8_t)(total - first < per ? total - first : per);
  if (!total) {
    lv_obj_t *l = lv_label_create(area);
    lv_label_set_text(l, "NO CHARGING SOURCES");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(l, ui_theme()->dim, 0);
    lv_obj_center(l);
    return;
  }
  int32_t rh = (H - pad) / per - pad;
  int32_t title_w = wide ? 175 : 0, title_h = wide ? 0 : 24;
  int32_t x0 = pad + title_w, inner = W - x0 - pad;
  int32_t u = (inner - 3 * pad) * 10 / 43;   // watts, amps, panel 1 each; mode 1.3
  power_topo_t topo;
  power_topo_resolve(b, &topo);
  for (uint8_t j = 0; j < chg.n; j++) {
    chg.g[j] = g[first + j];
    chg.i[j] = i[first + j];
    uint8_t gg = chg.g[j], ii = chg.i[j];
    bool solar = gg == G_SOLAR;
    int32_t y = pad + j * (rh + pad), y1 = y + title_h, h = rh - title_h;
    power_label(b, gg, ii, chg.slabel[j], sizeof chg.slabel[j]);
    caption(area, chg.slabel[j], pad, wide ? y + rh / 2 - (topo.active ? 26 : 12) : y, wide ? title_w : W - 2 * pad, &lv_font_montserrat_20);
    if (topo.active) {   // where this source's power goes: "TO HOUSE", "START > HOUSE" for a converter, "UNLINKED"
      int from, to = power_topo_dest(&topo, gg, ii, &from);
      if (to < 0) snprintf(chg.dest[j], sizeof chg.dest[j], "UNLINKED");
      else if (from >= 0) snprintf(chg.dest[j], sizeof chg.dest[j], "%s " LV_SYMBOL_RIGHT " %s", topo.bank_name[from], topo.bank_name[to]);
      else snprintf(chg.dest[j], sizeof chg.dest[j], "TO %s", topo.bank_name[to]);
      bool total_only = false;   // a charger's readings are for the whole unit, attributed to its main output
      for (uint8_t n = 0; n < topo.link_count; n++)
        if (gg == G_CHARGER && topo.link[n].g == gg && topo.link[n].i == ii && topo.link[n].output > 0) total_only = true;
      if (total_only) {
        size_t len = strlen(chg.dest[j]);
        snprintf(chg.dest[j] + len, sizeof chg.dest[j] - len, "  EST");
      }
      if (wide) power_text(area, chg.dest[j], &lv_font_montserrat_14, ui_theme()->dim, pad, y + rh / 2 + 2, title_w, LV_TEXT_ALIGN_LEFT);
      else power_text(area, chg.dest[j], &lv_font_montserrat_14, ui_theme()->dim, W / 2, y + 4, W / 2 - pad, LV_TEXT_ALIGN_RIGHT);
    }
    const role_group_t *def = &b->def->groups[gg];
    uint8_t ka = solar ? S_AMPS : C_AMPS, km = solar ? S_MODE : C_MODE;
    lv_obj_t *w = ui_readout_create(area, "WATTS", units_label(Q_POWER, b->units), 0);
    chg.w[j] = w;
    chg.a[j] = ui_readout_create(area, def->slots[ka].fail_label, units_label(Q_CURRENT, b->units), 1);
    chg.panel[j] = solar ? readout(area, b, G_SOLAR, S_PANEL, 0, 0, 0, 10, 10) : NULL;
    chg.mode[j] = ui_readout_create(area, def->slots[km].fail_label, "", 0);
    int32_t x = x0, mw = inner - 3 * (u + pad);
    lv_obj_set_pos(w, x, y1); lv_obj_set_size(w, u, h);
    x += u + pad;
    lv_obj_set_pos(chg.a[j], x, y1); lv_obj_set_size(chg.a[j], u, h);
    x += u + pad;
    if (chg.panel[j]) { lv_obj_set_pos(chg.panel[j], x, y1); lv_obj_set_size(chg.panel[j], u, h); }
    x += u + pad;
    lv_obj_set_pos(chg.mode[j], x, y1); lv_obj_set_size(chg.mode[j], mw, h);
  }
}

static void charging_refresh(role_binding_t *b, int64_t now) {
  for (uint8_t j = 0; j < chg.n; j++) {
    uint8_t g = chg.g[j], i = chg.i[j];
    bool solar = g == G_SOLAR;
    uint8_t kv = solar ? S_VOLTS : C_VOLTS, ka = solar ? S_AMPS : C_AMPS, km = solar ? S_MODE : C_MODE;
    double x;
    bd_sample_t st[3];
    power_src_t src;
    power_src_of(b, g, i, kv, ka, -1, st, &src);
    bool ok = power_watts(&src, now, &x);
    ui_readout_set(chg.w[j], x, ok);
    ok = num(b, g, i, ka, &x);
    ui_readout_set(chg.a[j], x, ok);
    role_field_alert(chg.a[j], b->g[g].ids[i][ka]);
    if (chg.panel[j]) {
      ok = num(b, g, i, S_PANEL, &x);
      ui_readout_set(chg.panel[j], x, ok);
    }
    char t[24] = "";
    ok = text(b, g, i, km, t, sizeof t);
    ui_readout_set_text(chg.mode[j], t, ok);
  }
}

// ---- dispatch ----

void power_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b) {
  memset(&bat, 0, sizeof bat);
  memset(&chg, 0, sizeof chg);
  lv_obj_update_layout(area);
  built_page = page;
  if (page == 0) power_summary_build(area, sc, b);
  else if (power_multi(b) && page == 1) power_banks_build(area, sc, b);
  else if (page == batteries_page(b)) batteries_build(area, sc, b);
  else charging_build(area, page, sc, b);
}

void power_refresh(role_binding_t *b, int64_t now) {
  if (built_page == 0) power_summary_refresh(b, now);
  else if (power_multi(b) && built_page == 1) power_banks_refresh(b, now);
  else if (built_page == batteries_page(b)) batteries_refresh(b, now);
  else if (built_page > batteries_page(b)) charging_refresh(b, now);
}

// The Glance tile: the house (primary) bank's state of charge, with the worst state over every bank's
// charge and voltage. The house charge missing is NO DATA, whatever the other banks say.
void power_tile(const role_binding_t *b, int64_t now_ms, glance_tile_t *out) {
  (void)now_ms;
  out->title = "POWER";
  uint8_t idx[ROLE_MAX_INST], primary;
  uint8_t n = power_banks(b, idx, &primary);
  if (n == 0) {
    glance_tile_state(out, GT_NO_DATA);
    return;
  }
  uint8_t house = idx[primary];
  gt_state_t st[2 * ROLE_MAX_INST + 1];
  size_t ns = 0;
  for (uint8_t j = 0; j < n; j++) {
    static const uint8_t watched[] = {B_SOC, B_VOLTS};
    for (size_t w = 0; w < sizeof watched; w++) {
      bd_id_t id = b->g[G_BATTERY].ids[idx[j]][watched[w]];
      if (id != BD_NONE) st[ns++] = glance_path_state(id, 0, now_ms);   // the slots have no max age (role_power.c)
    }
  }
  double soc, x;
  bool have_soc = num(b, G_BATTERY, house, B_SOC, &soc);
  if (!have_soc) st[ns++] = GT_NO_DATA;
  glance_tile_state(out, glance_worst(st, ns));
  out->trend = b->g[G_BATTERY].ids[house][B_SOC];
  if (have_soc) {
    snprintf(out->headline, sizeof out->headline, "%.0f", soc);
    snprintf(out->unit, sizeof out->unit, "%s", units_label(Q_RATIO, b->units));
  }
  double volts, amps;
  bool hv = num(b, G_BATTERY, house, B_VOLTS, &volts), ha = num(b, G_BATTERY, house, B_AMPS, &amps);
  if (hv && ha) snprintf(out->context[0], sizeof out->context[0], "%.1f V  %.1f A", volts, amps);
  else if (hv) snprintf(out->context[0], sizeof out->context[0], "%.1f V", volts);
  if (num(b, G_BATTERY, house, B_TIME, &x) && x > 0) snprintf(out->context[1], sizeof out->context[1], "%.0f h left", x);
}
