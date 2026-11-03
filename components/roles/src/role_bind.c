#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "role.h"

extern const role_def_t role_engine;
static const role_def_t *const ROLES[] = {&role_engine};

const role_def_t *role_find(const char *name) {
  for (size_t i = 0; i < sizeof ROLES / sizeof ROLES[0]; i++)
    if (strcmp(ROLES[i]->name, name) == 0) return ROLES[i];
  return NULL;
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

static bool excluded(const cfg_role_t *c, const char *id) {
  for (uint8_t i = 0; c && i < c->exclude_count; i++)
    if (strcmp(c->exclude[i], id) == 0) return true;
  return false;
}

static int cmp(const void *a, const void *b) { return strcmp(a, b); }

// Every distinct <id> in "<prefix>.<id>.<anything>", sorted.
static uint8_t discover(const char *prefix, const cfg_role_t *c, char ids[][24]) {
  uint8_t n = 0;
  size_t pl = strlen(prefix);
  for (bd_id_t id = 1; id <= bd_count(); id++) {
    const char *p = bd_path(id);
    if (strncmp(p, prefix, pl) != 0 || p[pl] != '.') continue;
    const char *seg = p + pl + 1, *end = strchr(seg, '.');
    if (!end || (size_t)(end - seg) >= 24) continue;
    char inst[24];
    memcpy(inst, seg, (size_t)(end - seg));
    inst[end - seg] = 0;
    if (excluded(c, inst)) continue;
    bool dup = false;
    for (uint8_t i = 0; i < n && !dup; i++) dup = strcmp(ids[i], inst) == 0;
    if (!dup && n < ROLE_MAX_INST) strcpy(ids[n++], inst);
  }
  qsort(ids, n, sizeof ids[0], cmp);
  return n;
}

static const char *explicit_bind(const cfg_role_t *c, const char *inst, const char *slot) {
  char key[48];
  snprintf(key, sizeof key, "%s.%s", inst, slot);
  for (uint8_t i = 0; c && i < c->bind_count; i++)
    if (strcmp(c->bind[i].slot, key) == 0) return c->bind[i].path;
  return NULL;
}

static bd_id_t bind_slot(const role_def_t *d, const role_slot_t *s, const cfg_role_t *c, const char *inst) {
  const char *exp = explicit_bind(c, inst, s->slot);
  if (exp) return bd_intern(exp);
  char primary[128], fallback[128];
  snprintf(primary, sizeof primary, "%s.%s.%s", d->instance_prefix, inst, s->suffix);
  if (bd_find(primary) != BD_NONE) return bd_find(primary);
  if (s->fallback) {
    snprintf(fallback, sizeof fallback, "%s.%s.%s", d->instance_prefix, inst, s->fallback);
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

void role_bind(const role_def_t *d, const cfg_role_t *c, const cfg_units_t *u, role_binding_t *b) {
  memset(b, 0, sizeof *b);
  b->def = d;
  b->units = u;
  bool placeholder = false;
  if (c && c->inst_count) {
    for (uint8_t i = 0; i < c->inst_count && i < ROLE_MAX_INST; i++) {
      snprintf(b->inst_id[i], sizeof b->inst_id[i], "%s", c->inst[i].id);
      if (c->inst[i].label[0]) snprintf(b->inst_label[i], sizeof b->inst_label[i], "%s", c->inst[i].label);
      else label_for(c->inst[i].id, b->inst_label[i], sizeof b->inst_label[i]);
      b->inst_count++;
    }
  } else {
    b->inst_count = discover(d->instance_prefix, c, b->inst_id);
    for (uint8_t i = 0; i < b->inst_count; i++) label_for(b->inst_id[i], b->inst_label[i], sizeof b->inst_label[i]);
  }
  if (b->inst_count == 0) {   // nothing found: one instance whose fields all flag
    b->inst_count = 1;
    snprintf(b->inst_label[0], sizeof b->inst_label[0], "%s", d->title);
    placeholder = true;
  }
  for (uint8_t i = 0; i < b->inst_count; i++)
    for (uint8_t k = 0; k < d->slot_count && k < ROLE_MAX_SLOTS; k++) {
      b->ids[i][k] = placeholder ? BD_NONE : bind_slot(d, &d->slots[k], c, b->inst_id[i]);
      bind_range(&d->slots[k], c, u, b->ids[i][k], &b->min[i][k], &b->max[i][k]);
    }
}
