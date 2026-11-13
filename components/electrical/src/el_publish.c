#include <stdio.h>
#include <string.h>
#include "boatdata.h"
#include "el_internal.h"

typedef struct {
  el_field_t f;
  const char *leaf;
} leaf_t;

static const leaf_t BATTERY[] = {{EL_F_VOLTAGE, "voltage"}, {EL_F_CURRENT, "current"}, {EL_F_SOC, "capacity.stateOfCharge"},
                                 {EL_F_TIME_REMAINING, "capacity.timeRemaining"}, {EL_F_POWER, "power"}};
static const leaf_t SOLAR[] = {{EL_F_VOLTAGE, "voltage"}, {EL_F_CURRENT, "current"}, {EL_F_POWER, "panelPower"},
                               {EL_F_CONV_STATE, "chargingMode"}};
static const leaf_t CHARGER[] = {{EL_F_VOLTAGE, "voltage"}, {EL_F_CURRENT, "current"}, {EL_F_CONV_STATE, "chargingMode"}};

static const leaf_t ALTERNATOR[] = {{EL_F_VOLTAGE, "voltage"}, {EL_F_CURRENT, "current"}, {EL_F_CONV_STATE, "chargingMode"}};
static const leaf_t ACIN[] = {{EL_F_AC_CURRENT, "current"}};

static const leaf_t REF = {EL_F_VOLTAGE, "ref"};     // like LABEL, not a reading
static const leaf_t LABEL = {EL_F_VOLTAGE, "label"};   // the field is unused: the label is not a reading

typedef struct {
  const char *name;
  const leaf_t *leaves;
  size_t n;
} group_t;

static bool group_of(el_fn_t fn, group_t *g) {
  switch (fn) {
    case EL_FN_BATTERY: *g = (group_t){"batteries", BATTERY, sizeof BATTERY / sizeof BATTERY[0]}; return true;
    case EL_FN_PV: *g = (group_t){"solar", SOLAR, sizeof SOLAR / sizeof SOLAR[0]}; return true;
    case EL_FN_CONVERTER:
    case EL_FN_CHARGER: *g = (group_t){"chargers", CHARGER, sizeof CHARGER / sizeof CHARGER[0]}; return true;
    case EL_FN_ALTERNATOR: *g = (group_t){"alternators", ALTERNATOR, sizeof ALTERNATOR / sizeof ALTERNATOR[0]}; return true;
    case EL_FN_AC_IN: *g = (group_t){"acin", ACIN, sizeof ACIN / sizeof ACIN[0]}; return true;
    default: return false;
  }
}

// The device's CAN NAME as the bus knows it (the key drops the top bit, the NAME does not).
static bool name_of(const el_t *e, const el_endpoint_t *p, uint64_t *name) {
  for (size_t i = 0; i < EL_MAX_DEVICES; i++)
    if (e->dev[i].used && e->dev[i].id.can_name && el_dev_key(&e->dev[i]) == p->devkey) {
      *name = e->dev[i].id.can_name;
      return true;
    }
  return false;
}

static const char *mode_text(int code) {
  static const char *const names[] = {"off", "low power", "fault", "bulk", "absorption", "float", "storage", "equalize",
                                      "passthru", "inverting", "assisting"};
  return code >= 0 && code < (int)(sizeof names / sizeof names[0]) ? names[code] : NULL;
}

static void path_of(const group_t *g, uint64_t key, const leaf_t *l, char *out, size_t n) {
  char id[9];
  el_endpoint_t k = {.key = key};
  el_endpoint_id(&k, id);
  snprintf(out, n, "electrical.resolved.%s.%s.%s", g->name, id, l->leaf);
}

// The endpoint a leaf's value comes from: the endpoint itself, else a mirror merged into it.
static const el_endpoint_t *source_of(const el_t *e, const el_endpoint_t *p, el_field_t f) {
  if (p->has[f]) return p;
  for (size_t i = 0; i < e->nep; i++)
    if (e->ep[i].hidden && e->ep[i].merged_into == p->key && e->ep[i].has[f]) return &e->ep[i];
  return NULL;
}

static void invalidate(const group_t *g, uint64_t key, const leaf_t *l) {
  char path[96];
  path_of(g, key, l, path, sizeof path);
  bd_id_t id = bd_find(path);
  if (id != BD_NONE) bd_set_invalid(id);
}

static void write(const el_t *e, const group_t *g, const el_endpoint_t *p, const leaf_t *l, const el_endpoint_t *s, int64_t now) {
  if (e->feed_down[s->feed[l->f]]) {
    invalidate(g, p->key, l);
    return;
  }
  char path[96];
  path_of(g, p->key, l, path, sizeof path);
  bd_id_t id = bd_intern(path);
  if (id == BD_NONE) return;
  if (l->f == EL_F_CONV_STATE) {
    const char *t = mode_text((int)s->val[l->f]);
    if (t) bd_set_text(id, t, BD_SRC_ELECTRICAL, now);
  } else {
    bd_set_number(id, s->val[l->f], BD_SRC_ELECTRICAL, now);
  }
}

void el_publish(el_t *e, int64_t now) {
  el_lock();
  if (bd_source_state(BD_SRC_ELECTRICAL) == BD_SRC_NOT_CONFIGURED) bd_source_register(BD_SRC_ELECTRICAL, "electrical", 1);
  if (bd_source_state(BD_SRC_ELECTRICAL) != BD_SRC_LIVE) bd_source_set_state(BD_SRC_ELECTRICAL, BD_SRC_LIVE);
  for (size_t i = 0; i < e->ngone; i++) {   // endpoints merged away since the last call
    group_t old;
    if (group_of(e->gone[i].fn, &old)) {
      for (size_t k = 0; k < old.n; k++) invalidate(&old, e->gone[i].key, &old.leaves[k]);
      invalidate(&old, e->gone[i].key, &LABEL);
      invalidate(&old, e->gone[i].key, &REF);
    }
  }
  e->ngone = 0;
  for (size_t i = 0; i < e->nep; i++) {
    el_endpoint_t *p = &e->ep[i];
    group_t g, old;
    bool want = !p->hidden && group_of(p->fn, &g);
    // Hidden, changed function, or re-keyed (its id changed): what it published under the old id goes invalid.
    if (p->pubd && (!want || p->pubfn != p->fn || p->pubkey != p->key) && group_of(p->pubfn, &old)) {
      for (size_t k = 0; k < old.n; k++) invalidate(&old, p->pubkey, &old.leaves[k]);
      invalidate(&old, p->pubkey, &LABEL);
      invalidate(&old, p->pubkey, &REF);
      p->pubd = false;
      p->refd = false;
    }
    if (!want) continue;
    bool full = !p->pubd;
    char label[16];
    el_endpoint_label(e, p, label, sizeof label);
    if (full || strcmp(label, p->publabel)) {   // first, so a role that finds the values finds the name too
      char path[96];
      path_of(&g, p->key, &LABEL, path, sizeof path);
      bd_id_t lid = bd_intern(path);
      if (lid != BD_NONE) bd_set_text(lid, label, BD_SRC_ELECTRICAL, now);
      memcpy(p->publabel, label, sizeof p->publabel);
    }
    uint64_t name;
    if (!p->refd && name_of(e, p, &name)) {
      char path[96], ref[32];
      path_of(&g, p->key, &REF, path, sizeof path);
      snprintf(ref, sizeof ref, "%016llx.%u", (unsigned long long)name, (unsigned)p->instance);
      bd_id_t rid = bd_intern(path);
      if (rid != BD_NONE) bd_set_text(rid, ref, BD_SRC_ELECTRICAL, now);
      p->refd = true;
    }
    for (size_t k = 0; k < g.n; k++) {
      const el_endpoint_t *s = source_of(e, p, g.leaves[k].f);
      if (s && (full || (s->dirty >> g.leaves[k].f & 1))) write(e, &g, p, &g.leaves[k], s, now);
    }
    p->pubd = true;
    p->pubfn = p->fn;
    p->pubkey = p->key;
  }
  for (size_t i = 0; i < e->nep; i++) e->ep[i].dirty = 0;
  el_unlock();
}

void el_feed_down(el_t *e, uint8_t feed) {
  if (feed >= EL_FEEDS) return;
  el_lock();
  e->feed_down[feed] = true;
  for (size_t i = 0; i < e->nep; i++) {
    const el_endpoint_t *p = &e->ep[i];
    group_t g;
    if (!p->pubd || !group_of(p->pubfn, &g)) continue;
    for (size_t k = 0; k < g.n; k++) {
      const el_endpoint_t *s = source_of(e, p, g.leaves[k].f);
      if (s && s->feed[g.leaves[k].f] == feed) invalidate(&g, p->pubkey, &g.leaves[k]);
    }
  }
  el_unlock();
}
