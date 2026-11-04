#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "role.h"

extern const role_def_t role_engine;
extern const role_def_t role_tanks;
extern const role_def_t role_power;
extern const role_def_t role_glance;
static const role_def_t *const ROLES[] = {&role_glance, &role_engine, &role_power, &role_tanks};

const role_def_t *role_find(const char *name) {
  for (size_t i = 0; i < sizeof ROLES / sizeof ROLES[0]; i++)
    if (strcmp(ROLES[i]->name, name) == 0) return ROLES[i];
  return NULL;
}

const char *const *role_names(void) {
  static const char *names[sizeof ROLES / sizeof ROLES[0] + 1];
  for (size_t i = 0; i < sizeof ROLES / sizeof ROLES[0]; i++) names[i] = ROLES[i]->name;
  return names;
}

static void label_for(const char *id, char *out, size_t n) {
  static const struct { const char *id, *label; } known[] = {
    {"port", "PORT"}, {"starboard", "STBD"}, {"center", "CTR"}, {"centre", "CTR"}, {"main", "MAIN"},
  };
  for (size_t i = 0; i < sizeof known / sizeof known[0]; i++)
    if (strcmp(known[i].id, id) == 0) {
      snprintf(out, n, "%s", known[i].label);
      return;
    }
  size_t i = 0;
  for (; id[i] && i + 1 < n; i++) out[i] = (char)toupper((unsigned char)id[i]);
  out[i] = 0;
}

// Config ids are bare in a single-group role, "<group>.<id>" otherwise.
static bool multi(const role_def_t *d) { return d->group_count > 1; }

static void config_key(const role_def_t *d, const role_group_t *g, const char *id, char *out, size_t n) {
  if (multi(d)) snprintf(out, n, "%s.%s", g->name, id);
  else snprintf(out, n, "%s", id);
}

static bool excluded(const role_def_t *d, const role_group_t *g, const cfg_role_t *c, const char *id) {
  char key[48];
  config_key(d, g, id, key, sizeof key);
  for (uint8_t i = 0; c && i < c->exclude_count; i++)
    if (strcmp(c->exclude[i], key) == 0) return true;
  return false;
}


static uint8_t max_inst(const role_group_t *g) { return g->max_inst < ROLE_MAX_INST ? g->max_inst : ROLE_MAX_INST; }

// Every distinct <id> (id_depth segments) in "<prefix>.<id>.<anything>". Keeps
// the first max_inst in sorted order.
static uint8_t discover(const role_def_t *d, const role_group_t *g, const cfg_role_t *c, char ids[][24]) {
  uint8_t n = 0, cap = max_inst(g);
  size_t pl = strlen(g->prefix);
  for (bd_id_t id = 1; id <= bd_count(); id++) {
    const char *p = bd_path(id);
    if (strncmp(p, g->prefix, pl) != 0 || p[pl] != '.') continue;
    const char *seg = p + pl + 1, *end = seg;
    for (uint8_t k = 0; k < g->id_depth && end; k++) end = strchr(k ? end + 1 : end, '.');
    if (!end || end == seg || !end[1] || (size_t)(end - seg) >= 24) continue;
    char inst[24];
    memcpy(inst, seg, (size_t)(end - seg));
    inst[end - seg] = 0;
    if (excluded(d, g, c, inst)) continue;
    bool dup = false;
    uint8_t pos = 0;
    for (; pos < n; pos++) {
      int r = strcmp(ids[pos], inst);
      if (r == 0) dup = true;
      if (r >= 0) break;
    }
    if (dup || pos >= cap) continue;
    if (n == cap) n--;   // full: the largest falls off
    memmove(ids[pos + 1], ids[pos], (size_t)(n - pos) * sizeof ids[0]);
    strcpy(ids[pos], inst);
    n++;
  }
  return n;
}

static const char *explicit_bind(const role_def_t *d, const role_group_t *g, const cfg_role_t *c, const char *inst,
                                 const char *slot) {
  char ik[48], key[80];
  if (inst[0]) {
    config_key(d, g, inst, ik, sizeof ik);
    snprintf(key, sizeof key, "%s.%s", ik, slot);
  } else {   // depth 0: "<group>.<slot>"
    snprintf(key, sizeof key, "%s.%s", g->name, slot);
  }
  for (uint8_t i = 0; c && i < c->bind_count; i++)
    if (strcmp(c->bind[i].slot, key) == 0) return c->bind[i].path;
  return NULL;
}

static bd_id_t bind_slot(const role_def_t *d, const role_group_t *g, const role_slot_t *s, const cfg_role_t *c,
                         const char *inst) {
  const char *exp = explicit_bind(d, g, c, inst, s->slot);
  if (exp) return bd_intern(exp);
  char primary[128], fallback[128];
  const char *dot = inst[0] ? "." : "";   // depth 0: no instance segment
  snprintf(primary, sizeof primary, "%s.%s%s%s", g->prefix, inst, dot, s->suffix);
  if (bd_find(primary) != BD_NONE) return bd_find(primary);
  if (s->fallback) {
    snprintf(fallback, sizeof fallback, "%s.%s%s%s", g->prefix, inst, dot, s->fallback);
    if (bd_find(fallback) != BD_NONE) return bd_find(fallback);
  }
  return bd_intern(primary);   // so a later delta can fill it
}

static void bind_range(const role_slot_t *s, const cfg_role_t *c, const cfg_units_t *u, bd_id_t id,
                       double *lo, double *hi) {
  *lo = s->def_min;
  *hi = s->def_max;
  bd_meta_t m;
  if (id != BD_NONE && bd_get_meta(id, &m) && m.has_scale) {
    *lo = m.scale_lo;
    *hi = m.scale_hi;
  }
  for (uint8_t i = 0; c && i < c->ovr_count; i++) {
    if (strcmp(c->ovr[i].slot, s->slot) != 0) continue;
    if (c->ovr[i].has_min) *lo = units_to_si(s->q, c->ovr[i].min, u);
    if (c->ovr[i].has_max) *hi = units_to_si(s->q, c->ovr[i].max, u);
  }
}

static void bind_group(const role_def_t *d, const role_group_t *g, const cfg_role_t *c, const cfg_units_t *u,
                       role_group_binding_t *b) {
  bool placeholder = false;
  uint8_t cap = max_inst(g);
  size_t nl = multi(d) ? strlen(g->name) : 0;
  if (g->id_depth == 0) {   // one fixed path set, no instance id and no discovery
    b->inst_count = 1;
    snprintf(b->inst_label[0], sizeof b->inst_label[0], "%s", g->name);
    for (uint8_t k = 0; k < g->slot_count && k < ROLE_MAX_SLOTS; k++) {
      b->ids[0][k] = bind_slot(d, g, &g->slots[k], c, "");
      bind_range(&g->slots[k], c, u, b->ids[0][k], &b->min[0][k], &b->max[0][k]);
    }
    return;
  }
  for (uint8_t i = 0; c && i < c->inst_count && b->inst_count < cap; i++) {
    const char *id = c->inst[i].id;
    if (multi(d)) {   // only this group's pinned instances
      if (strncmp(id, g->name, nl) != 0 || id[nl] != '.') continue;
      id += nl + 1;
    }
    snprintf(b->inst_id[b->inst_count], sizeof b->inst_id[0], "%s", id);
    if (c->inst[i].label[0]) snprintf(b->inst_label[b->inst_count], sizeof b->inst_label[0], "%s", c->inst[i].label);
    else label_for(id, b->inst_label[b->inst_count], sizeof b->inst_label[0]);
    b->inst_count++;
  }
  if (b->inst_count == 0) {
    b->inst_count = discover(d, g, c, b->inst_id);
    for (uint8_t i = 0; i < b->inst_count; i++) label_for(b->inst_id[i], b->inst_label[i], sizeof b->inst_label[i]);
  }
  if (b->inst_count == 0) {   // nothing found: one instance whose fields all flag
    b->inst_count = 1;
    snprintf(b->inst_label[0], sizeof b->inst_label[0], "%s", d->title);
    placeholder = true;
  }
  for (uint8_t i = 0; i < b->inst_count; i++)
    for (uint8_t k = 0; k < g->slot_count && k < ROLE_MAX_SLOTS; k++) {
      b->ids[i][k] = placeholder ? BD_NONE : bind_slot(d, g, &g->slots[k], c, b->inst_id[i]);
      bind_range(&g->slots[k], c, u, b->ids[i][k], &b->min[i][k], &b->max[i][k]);
    }
}

void role_bind(const role_def_t *d, const cfg_role_t *c, const cfg_units_t *u, role_binding_t *b) {
  memset(b, 0, sizeof *b);
  b->def = d;
  b->units = u;
  b->cfg = c;
  for (uint8_t g = 0; g < d->group_count && g < ROLE_MAX_GROUPS; g++) bind_group(d, &d->groups[g], c, u, &b->g[g]);
}

bool role_binding_same(const role_binding_t *a, const role_binding_t *b) {
  if (a->def != b->def) return false;
  for (uint8_t g = 0; g < a->def->group_count && g < ROLE_MAX_GROUPS; g++) {
    const role_group_binding_t *x = &a->g[g], *y = &b->g[g];
    if (x->inst_count != y->inst_count) return false;
    for (uint8_t i = 0; i < x->inst_count; i++)
      if (strcmp(x->inst_id[i], y->inst_id[i]) != 0 || memcmp(x->ids[i], y->ids[i], sizeof x->ids[i]) != 0)
        return false;
  }
  return true;
}

uint8_t role_discover(const role_def_t *d, char ids[][24], uint8_t max) {
  uint8_t n = 0;
  char found[ROLE_MAX_INST][24];
  for (uint8_t gi = 0; gi < d->group_count; gi++) {
    const role_group_t *g = &d->groups[gi];
    if (g->id_depth == 0) continue;   // a fixed path (Power's system group), not an instance
    uint8_t k = discover(d, g, NULL, found);
    for (uint8_t i = 0; i < k && n < max; i++) config_key(d, g, found[i], ids[n++], 24);
  }
  return n;
}
