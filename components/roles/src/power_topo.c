#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "power_topo.h"
#include "units.h"

static int kind_of(const char *k) {
  if (!strcmp(k, "house")) return BANK_HOUSE;
  if (!strcmp(k, "start")) return BANK_START;
  if (!strcmp(k, "nav")) return BANK_NAV;
  return -1;
}

static int find_inst(const role_binding_t *b, uint8_t g, const char *id) {
  for (uint8_t i = 0; i < b->g[g].inst_count; i++)
    if (strcmp(b->g[g].inst_id[i], id) == 0) return i;
  return -1;
}

void power_topo_resolve(const role_binding_t *b, power_topo_t *t) {
  static const char *const DEFAULT_NAME[BANK_KINDS] = {"HOUSE", "START", "NAV"};
  static const struct { const char *name; uint8_t g; } GROUPS[] = {
    {"charger", G_CHARGER}, {"solar", G_SOLAR}, {"dcdc", G_DCDC}, {"alternator", G_ALTERNATOR}};
  memset(t, 0, sizeof *t);
  const cfg_role_t *c = b->cfg;
  if (!c || c->bank_count == 0) return;
  for (uint8_t k = 0; k < BANK_KINDS; k++) snprintf(t->bank_name[k], sizeof t->bank_name[k], "%s", DEFAULT_NAME[k]);
  for (uint8_t n = 0; n < c->bank_count; n++) {
    int k = kind_of(c->bank[n].kind);
    if (k < 0) continue;
    t->bank_cfg[k] = true;
    if (c->bank[n].name[0]) snprintf(t->bank_name[k], sizeof t->bank_name[k], "%s", c->bank[n].name);
    int i = find_inst(b, G_BATTERY, c->bank[n].id);
    if (i >= 0 && power_real(b, G_BATTERY, (uint8_t)i)) {
      t->bank_bound[k] = true;
      t->bank_inst[k] = (uint8_t)i;
    }
  }
  t->active = t->bank_cfg[BANK_HOUSE];
  for (uint8_t n = 0; n < c->link_count && t->link_count < TOPO_MAX_LINKS; n++) {
    const cfg_link_t *l = &c->link[n];
    const char *colon = strchr(l->src, ':');
    if (!colon) continue;
    power_link_t *o = &t->link[t->link_count];
    memset(o, 0, sizeof *o);
    bool known = false;
    for (size_t g = 0; g < sizeof GROUPS / sizeof GROUPS[0]; g++)
      if ((size_t)(colon - l->src) == strlen(GROUPS[g].name) && !strncmp(l->src, GROUPS[g].name, strlen(GROUPS[g].name))) {
        o->g = GROUPS[g].g;
        known = true;
      }
    if (!known) continue;
    int i = find_inst(b, o->g, colon + 1);
    o->bound = i >= 0 && power_real(b, o->g, (uint8_t)i);
    o->i = i >= 0 ? (uint8_t)i : 0;
    o->output = l->output;
    o->from = l->from[0] ? (int8_t)kind_of(l->from) : -1;
    o->to = (int8_t)kind_of(l->to);
    t->link_count++;
  }
}

// Sample storage for one compute: house sources, converter outputs and inputs, and two shunts.
// Static (and in PSRAM on the board): samples are too big for a UI task's stack.
#define MAXL TOPO_MAX_LINKS
PLATFORM_EXT_BSS static bd_sample_t st_into[MAXL][3], st_out[MAXL][3], st_in[MAXL][3], st_hb[3], st_nb[3];

static bool soc_of(const role_binding_t *b, const power_topo_t *t, int kind, double *out) {
  if (!t->bank_bound[kind]) return false;
  bd_sample_t s;
  if (!power_sample(b, G_BATTERY, t->bank_inst[kind], B_SOC, &s) || s.kind != BD_NUMBER || !s.valid) return false;
  *out = s.num;
  return true;
}

void power_banks_compute(const role_binding_t *b, const power_topo_t *t, int64_t now_ms, power_banks_view_t *out) {
  memset(out, 0, sizeof *out);
  if (!t->active) return;
  power_src_t into[MAXL], outs[MAXL], ins[MAXL], hb, nb;
  power_conv_t conv[MAXL];
  size_t n_into = 0, n_conv = 0;
  for (uint8_t n = 0; n < t->link_count; n++) {
    const power_link_t *l = &t->link[n];
    if (l->output > 0) continue;
    if (l->to == BANK_HOUSE) {
      into[n_into] = (power_src_t){0};
      if (l->bound) power_src_of(b, l->g, l->i, C_VOLTS, C_AMPS, -1, st_into[n_into], &into[n_into]);
      n_into++;
    } else if (l->g == G_DCDC && l->from == BANK_HOUSE && l->to == BANK_NAV) {
      outs[n_conv] = (power_src_t){0};
      ins[n_conv] = (power_src_t){0};
      if (l->bound) {
        power_src_of(b, G_DCDC, l->i, D_VOLTS, D_AMPS, -1, st_out[n_conv], &outs[n_conv]);
        power_src_of(b, G_DCDC, l->i, D_IN_VOLTS, D_IN_AMPS, -1, st_in[n_conv], &ins[n_conv]);
      }
      conv[n_conv] = (power_conv_t){.out = &outs[n_conv], .in = &ins[n_conv]};
      n_conv++;
    }
  }
  hb = (power_src_t){0};
  nb = (power_src_t){0};
  if (t->bank_bound[BANK_HOUSE]) power_src_of(b, G_BATTERY, t->bank_inst[BANK_HOUSE], B_VOLTS, B_AMPS, B_POWER, st_hb, &hb);
  if (t->bank_bound[BANK_NAV]) power_src_of(b, G_BATTERY, t->bank_inst[BANK_NAV], B_VOLTS, B_AMPS, B_POWER, st_nb, &nb);
  power_banks_in_t in = {.into_house = into, .n_into_house = n_into, .house_batt = &hb, .to_nav = conv,
                         .n_to_nav = n_conv, .nav_batt = &nb, .has_nav = t->bank_cfg[BANK_NAV]};
  out->fig = power_summarize_banks(&in, 0, now_ms);
  out->house_soc_ok = out->fig.house_batt.ok && soc_of(b, t, BANK_HOUSE, &out->house_soc);
  out->start_soc_ok = soc_of(b, t, BANK_START, &out->start_soc);
  out->nav_soc_ok = soc_of(b, t, BANK_NAV, &out->nav_soc);
}

int power_topo_dest(const power_topo_t *t, uint8_t g, uint8_t i, int *from) {
  int pick = -1;
  for (uint8_t n = 0; n < t->link_count; n++) {
    const power_link_t *l = &t->link[n];
    if (!l->bound || l->g != g || l->i != i) continue;
    if (pick < 0 || (l->output == 0 && t->link[pick].output != 0)) pick = n;
  }
  if (from) *from = pick >= 0 ? t->link[pick].from : -1;
  return pick >= 0 ? t->link[pick].to : -1;
}

void power_bank_sources(const role_binding_t *b, const power_topo_t *t, int bank, int64_t now_ms, const char *sep,
                        char *out, size_t n) {
  static const char *const GROUP[] = {[G_SOLAR] = "SOLAR", [G_CHARGER] = "CHARGER", [G_ALTERNATOR] = "ALTERNATOR"};
  size_t len = 0;
  out[0] = 0;
  for (uint8_t k = 0; k < t->link_count; k++) {
    const power_link_t *l = &t->link[k];
    if (l->to != bank || l->g == G_DCDC || l->g >= sizeof GROUP / sizeof GROUP[0] || !GROUP[l->g]) continue;
    char item[48];
    const char *name = l->g == G_CHARGER && l->output > 0 ? "FLOAT CHARGER" : GROUP[l->g];
    if (l->g == G_CHARGER && l->output > 0 && l->bound) {
      snprintf(item, sizeof item, "%s", name);
    } else {
      bd_sample_t st[3];
      power_src_t src;
      double w;
      bool ok = false;
      if (l->bound) {
        power_src_of(b, l->g, l->i, C_VOLTS, C_AMPS, -1, st, &src);
        ok = power_watts(&src, now_ms, &w);
      }
      if (ok) snprintf(item, sizeof item, "%s %.0f %s", name, units_from_si(Q_POWER, w, b->units), units_label(Q_POWER, b->units));
      else snprintf(item, sizeof item, "%s FAIL", name);
    }
    int w = snprintf(out + len, n - len, "%s%s", len ? sep : "", item);
    if (w < 0 || (size_t)w >= n - len) { out[len] = 0; break; }
    len += (size_t)w;
  }
}
