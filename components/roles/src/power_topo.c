#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "platform.h"
#include "power_topo.h"
#include "units.h"

static int bank_kind_of(const char *k) {
  static const char *const K[BK_KINDS] = {"house", "start", "nav", "windlass", "thruster", "genset", "other"};
  for (int i = 0; i < BK_KINDS; i++)
    if (!strcmp(k, K[i])) return i;
  return -1;
}

static int flow_kind_of(const char *k) {
  static const char *const K[FLOW_KINDS] = {"solar", "shore", "engine", "converter", "other"};
  for (int i = 0; i < FLOW_KINDS; i++)
    if (!strcmp(k, K[i])) return i;
  return -1;
}

bool power_ref_of(const role_binding_t *b, uint8_t g, uint8_t i, char out[32]) {
  static const int8_t SLOT[] = {[G_BATTERY] = B_REF, [G_SOLAR] = S_REF, [G_CHARGER] = C_REF, [G_ACIN] = A_REF,
                                [G_ALTERNATOR] = C_REF, [G_INVERTER] = -1, [G_SYSTEM] = -1};
  if (g >= sizeof SLOT / sizeof SLOT[0] || SLOT[g] < 0 || i >= b->g[g].inst_count) return false;
  bd_sample_t s;
  if (!power_sample(b, g, i, (uint8_t)SLOT[g], &s) || s.kind != BD_TEXT || !s.valid) return false;
  snprintf(out, 32, "%s", s.text);
  return true;
}

static bool ref_device_is(const char *ref, const char *device) {
  return !strncmp(ref, device, 16) && ref[16] == '.';
}

// The n-th (by endpoint instance) real instance of group g published by `device`; -1 when there are fewer.
static int nth_output(const role_binding_t *b, uint8_t g, const char *device, uint8_t n) {
  int last = -1, pick = -1;
  for (uint8_t k = 0; k <= n; k++) {
    int best = -1, best_inst = 1 << 20;
    for (uint8_t i = 0; i < b->g[g].inst_count; i++) {
      char ref[32];
      if (!power_real(b, g, i) || !power_ref_of(b, g, i, ref) || !ref_device_is(ref, device)) continue;
      int inst = atoi(ref + 17);
      if (inst > last && inst < best_inst) { best_inst = inst; best = i; }
    }
    if (best < 0) return -1;
    last = best_inst;
    pick = best;
  }
  return pick;
}

int power_topo_bank(const power_topo_t *t, const char *id) {
  for (uint8_t i = 0; i < t->bank_count; i++)
    if (!strcmp(t->bank[i].id, id)) return i;
  return -1;
}

void power_topo_resolve(const role_binding_t *b, power_topo_t *t) {
  memset(t, 0, sizeof *t);
  t->house = t->start = t->nav = -1;
  memset(t->batt_bank, -1, sizeof t->batt_bank);
  memset(t->out_flow, -1, sizeof t->out_flow);
  const cfg_role_t *c = b->cfg;
  if (c) {
    for (uint8_t n = 0; n < c->bank_count && t->bank_count < TOPO_MAX_BANKS; n++) {
      int kind = bank_kind_of(c->bank[n].kind);
      if (kind < 0) continue;
      power_bank_t *k = &t->bank[t->bank_count];
      k->kind = (uint8_t)kind;
      snprintf(k->id, sizeof k->id, "%s", c->bank[n].id);
      if (c->bank[n].name[0]) snprintf(k->name, sizeof k->name, "%s", c->bank[n].name);
      else for (size_t j = 0; j < sizeof k->name - 1 && c->bank[n].kind[j]; j++) k->name[j] = (char)toupper((unsigned char)c->bank[n].kind[j]);
      for (uint8_t i = 0; i < b->g[G_BATTERY].inst_count; i++) {
        char ref[32];
        if (power_real(b, G_BATTERY, i) && power_ref_of(b, G_BATTERY, i, ref) && !strcmp(ref, c->bank[n].battery)) {
          k->bound = true;
          k->inst = i;
          t->batt_bank[i] = (int8_t)t->bank_count;
          break;
        }
      }
      if (kind == BK_HOUSE && t->house < 0) t->house = (int8_t)t->bank_count;
      if (kind == BK_START && t->start < 0) t->start = (int8_t)t->bank_count;
      if (kind == BK_NAV && t->nav < 0) t->nav = (int8_t)t->bank_count;
      t->bank_count++;
    }
    t->active = t->house >= 0;
    for (uint8_t n = 0; n < c->flow_count && t->flow_count < TOPO_MAX_FLOWS; n++) {
      const cfg_flow_t *f = &c->flow[n];
      int kind = flow_kind_of(f->kind);
      if (kind < 0) continue;
      power_flow_t *o = &t->flow[t->flow_count];
      o->kind = (uint8_t)kind;
      o->output = f->output;
      o->to = (int8_t)power_topo_bank(t, f->to);
      o->from = f->from[0] ? (int8_t)power_topo_bank(t, f->from) : -1;
      snprintf(o->dev, sizeof o->dev, "%s", f->device);
      o->g = kind == FLOW_ENGINE ? G_ALTERNATOR : G_CHARGER;
      int i = nth_output(b, o->g, f->device, f->output);
      if (i < 0) {   // the other group: an engine flow on a device the resolver calls a charger, or any flow on an alternator
        o->g = o->g == G_ALTERNATOR ? G_CHARGER : G_ALTERNATOR;
        i = nth_output(b, o->g, f->device, f->output);
        if (i < 0) o->g = kind == FLOW_ENGINE ? G_ALTERNATOR : G_CHARGER;
      }
      if (i >= 0) {
        o->bound = true;
        o->i = (uint8_t)i;
        t->out_flow[o->g == G_CHARGER ? 0 : 1][i] = (int8_t)t->flow_count;
      }
      t->flow_count++;
    }
  }
  for (uint8_t i = 0; i < b->g[G_BATTERY].inst_count; i++)
    if (power_real(b, G_BATTERY, i) && t->batt_bank[i] < 0) t->unassigned_batt++;
  for (uint8_t g = 0; g < 2; g++) {
    uint8_t grp = g == 0 ? G_CHARGER : G_ALTERNATOR;
    for (uint8_t i = 0; i < b->g[grp].inst_count; i++)
      if (power_real(b, grp, i) && t->out_flow[g][i] < 0) t->unassigned_dev++;
  }
}

// Sample storage for one compute. Static (and in PSRAM on the board): samples are too big for a UI task's stack.
PLATFORM_EXT_BSS static bd_sample_t st_flow[TOPO_MAX_FLOWS][3], st_batt[3];
PLATFORM_EXT_BSS static power_src_t flow_src[TOPO_MAX_FLOWS], into_buf[TOPO_MAX_FLOWS], batt_src;
PLATFORM_EXT_BSS static power_conv_t drawn_buf[TOPO_MAX_FLOWS];

static bool soc_of(const role_binding_t *b, const power_bank_t *k, double *out) {
  if (!k->bound) return false;
  bd_sample_t s;
  if (!power_sample(b, G_BATTERY, k->inst, B_SOC, &s) || s.kind != BD_NUMBER || !s.valid) return false;
  *out = s.num;
  return true;
}

void power_banks_compute(const role_binding_t *b, const power_topo_t *t, int64_t now_ms, power_banks_view_t *out) {
  memset(out, 0, sizeof *out);
  if (!t->active) return;
  for (uint8_t n = 0; n < t->flow_count; n++) {
    const power_flow_t *l = &t->flow[n];
    flow_src[n] = (power_src_t){0};   // an unseen device stays in as an unusable reading and fails IN
    if (l->bound) power_src_of(b, l->g, l->i, C_VOLTS, C_AMPS, -1, st_flow[n], &flow_src[n]);
  }
  for (uint8_t k = 0; k < t->bank_count; k++) {
    size_t n_into = 0, n_drawn = 0;
    for (uint8_t n = 0; n < t->flow_count; n++) {
      const power_flow_t *l = &t->flow[n];
      if (l->to == k) into_buf[n_into++] = flow_src[n];
      if (l->kind == FLOW_CONVERTER && l->from == k) drawn_buf[n_drawn++] = (power_conv_t){.out = &flow_src[n], .in = NULL};
    }
    batt_src = (power_src_t){0};
    if (t->bank[k].bound) power_src_of(b, G_BATTERY, t->bank[k].inst, B_VOLTS, B_AMPS, B_POWER, st_batt, &batt_src);
    power_bank_in_t in = {.batt = &batt_src, .into = into_buf, .n_into = n_into, .drawn = drawn_buf, .n_drawn = n_drawn};
    out->bank[k].fig = power_summarize_bank(&in, 0, now_ms);
    out->bank[k].soc_ok = soc_of(b, &t->bank[k], &out->bank[k].soc);
  }
}

int power_topo_dest(const role_binding_t *b, const power_topo_t *t, uint8_t g, uint8_t i, int *from) {
  int pick = -1;
  if ((g == G_CHARGER || g == G_ALTERNATOR) && i < ROLE_MAX_INST) pick = t->out_flow[g == G_CHARGER ? 0 : 1][i];
  else if (g == G_SOLAR) {
    char ref[32];
    if (power_ref_of(b, g, i, ref))
      for (uint8_t n = 0; n < t->flow_count && pick < 0; n++)
        if (ref_device_is(ref, t->flow[n].dev)) pick = n;
  }
  if (pick >= 0 && t->flow[pick].to < 0) pick = -1;
  if (from) *from = pick >= 0 ? t->flow[pick].from : -1;
  return pick >= 0 ? t->flow[pick].to : -1;
}

void power_bank_sources(const role_binding_t *b, const power_topo_t *t, int bank, int64_t now_ms, const char *sep,
                        char *out, size_t n) {
  size_t len = 0;
  out[0] = 0;
  for (uint8_t k = 0; k < t->flow_count; k++) {
    const power_flow_t *l = &t->flow[k];
    if (l->to != bank || l->kind == FLOW_CONVERTER) continue;
    char name[24], item[48];
    switch (l->kind) {
      case FLOW_SOLAR: snprintf(name, sizeof name, "SOLAR"); break;
      case FLOW_ENGINE: snprintf(name, sizeof name, "ALTERNATOR"); break;
      case FLOW_SHORE: if (l->output) snprintf(name, sizeof name, "CHARGER %u", l->output + 1); else snprintf(name, sizeof name, "CHARGER"); break;
      default: snprintf(name, sizeof name, "OTHER"); break;
    }
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
    int wr = snprintf(out + len, n - len, "%s%s", len ? sep : "", item);
    if (wr < 0 || (size_t)wr >= n - len) { out[len] = 0; break; }
    len += (size_t)wr;
  }
}

int power_inverter_shore(const role_binding_t *b) {
  int state = -1;
  for (uint8_t i = 0; i < b->g[G_INVERTER].inst_count; i++) {
    bd_sample_t s;
    if (!power_real(b, G_INVERTER, i) || !power_sample(b, G_INVERTER, i, I_SHORE, &s) || !s.valid) continue;
    if ((s.kind == BD_TEXT && (!strcasecmp(s.text, "grid") || !strcasecmp(s.text, "shore"))) || (s.kind == BD_NUMBER && s.num == 1)) return 1;
    state = 0;
  }
  return state;
}

shore_t power_shore(const role_binding_t *b, const power_topo_t *t, int64_t now_ms, double *watts) {
  shore_t best = SHORE_UNKNOWN;
  static const uint8_t RANK[] = {[SHORE_UNKNOWN] = 0, [SHORE_INFERRED_ABSENT] = 1, [SHORE_ABSENT] = 2,
                                 [SHORE_INFERRED_PRESENT] = 3, [SHORE_PRESENT] = 4};
  int inv = power_inverter_shore(b);
  if (watts) *watts = 0;
  for (uint8_t n = 0; n < t->flow_count; n++) {
    const power_flow_t *l = &t->flow[n];
    if (l->kind != FLOW_SHORE) continue;
    double w = 0;
    bool w_ok = false;
    if (l->bound) {
      bd_sample_t st[3];
      power_src_t src;
      power_src_of(b, l->g, l->i, C_VOLTS, C_AMPS, -1, st, &src);
      w_ok = power_watts(&src, now_ms, &w);
      if (w_ok && watts && w > 0) *watts += w;
    }
    shore_t s = SHORE_UNKNOWN;
    bool has_ac = false;
    for (uint8_t i = 0; i < b->g[G_ACIN].inst_count; i++) {
      char ref[32];
      if (!power_real(b, G_ACIN, i) || !power_ref_of(b, G_ACIN, i, ref) || !ref_device_is(ref, l->dev)) continue;
      has_ac = true;
      bd_sample_t a;
      if (power_sample(b, G_ACIN, i, A_CURRENT, &a) && a.valid && now_ms - a.rx_ms <= SHORE_FRESH_MS) s = SHORE_PRESENT;
    }
    if (has_ac && s != SHORE_PRESENT) s = SHORE_ABSENT;
    if (!has_ac) {
      if (inv >= 0) s = inv == 1 ? SHORE_PRESENT : SHORE_ABSENT;
      else if (w_ok) s = w > 0 ? SHORE_INFERRED_PRESENT : SHORE_INFERRED_ABSENT;
    }
    if (RANK[s] > RANK[best]) best = s;
  }
  return best;
}
