#include <math.h>
#include <string.h>
#include "el_internal.h"
#include "platform.h"

static void *mutex;
void el_lock(void) { if (mutex) platform_lock(mutex); }
void el_unlock(void) { if (mutex) platform_unlock(mutex); }

void el_init(el_t *e) {
  if (!mutex) mutex = platform_lock_create();   // created at startup, before a second task exists
  memset(e, 0, sizeof *e);
}

void el_set_identity(el_t *e, uint8_t feed, el_ref_t ref, const el_identity_t *id) {
  if (feed >= EL_FEEDS) return;
  el_lock();
  el_device_t *d = el_dev_find_or_add(e, feed, ref);
  if (!d) {
    el_unlock();
    return;
  }
  // Fields merge: a claim (NAME) and product information (model, serial) arrive separately.
  if (id->can_name) d->id.can_name = id->can_name;
  if (id->model_id[0]) memcpy(d->id.model_id, id->model_id, sizeof d->id.model_id);
  if (id->serial[0]) memcpy(d->id.serial, id->serial, sizeof d->id.serial);
  uint64_t old = d->key, nk = el_dev_key(d);
  d->key = nk;
  if (nk != old) el_rekey(e, old, nk);
  el_classify_all(e);
  el_unlock();
}

// A voltage that stays within EL_STABLE_V restarts its window only when it leaves the band.
static void track_voltage(el_endpoint_t *p, double v, int64_t now_ms) {
  if (p->vseen) {
    double lo = v < p->vlo ? v : p->vlo, hi = v > p->vhi ? v : p->vhi;
    if (hi - lo <= EL_STABLE_V) {
      p->vlo = lo;
      p->vhi = hi;
      p->vlast = now_ms;
      return;
    }
  }
  p->vseen = true;
  p->vlo = p->vhi = v;
  p->vsince = p->vlast = now_ms;
}

void el_observe_locked(el_t *e, uint8_t feed, el_ref_t ref, uint8_t instance, el_field_t f, double value, int64_t now_ms) {
  if (f >= EL_F_COUNT || feed >= EL_FEEDS || !isfinite(value)) return;
  el_device_t *d = el_dev_find_or_add(e, feed, ref);
  if (!d) return;
  el_endpoint_t *p = el_ep_find_or_add(e, d->key, f == EL_F_AC_CURRENT ? EL_DOM_AC : f == EL_F_SWITCH ? EL_DOM_SWITCH : EL_DOM_DC, instance);
  if (!p) return;
  if (p->has[f] && now_ms < p->t[f]) return;
  if (f == EL_F_DC_TYPE && p->has[f] && p->val[f] != value && p->src[f] != ref) p->dc_conflict = true;
  if (f == EL_F_VOLTAGE) track_voltage(p, value, now_ms);
  p->has[f] = true;
  p->val[f] = value;
  p->src[f] = ref;
  p->feed[f] = feed;
  p->t[f] = now_ms;
  p->dirty |= (uint16_t)(1u << f);
  e->feed_down[feed] = false;
  d->emits |= (uint16_t)(1u << f);
  el_classify_all(e);
  el_merge_run(e, now_ms);
}

void el_observe(el_t *e, uint8_t feed, el_ref_t ref, uint8_t instance, el_field_t f, double value, int64_t now_ms) {
  el_lock();
  el_observe_locked(e, feed, ref, instance, f, value, now_ms);
  el_unlock();
}

void el_observe_path(el_t *e, uint8_t feed, el_ref_t ref, uint8_t instance, el_fn_t fn, el_field_t f, double value, int64_t now_ms) {
  el_lock();
  el_device_t *d = el_dev_find_or_add(e, feed, ref);   // counts a drop itself
  if (d) el_observe_locked(e, feed, ref, instance, f, value, now_ms);
  el_endpoint_t *p = d ? el_ep_find(e, d->key, EL_DOM_DC, instance) : NULL;
  if (p) {
    p->fn = fn;
    p->ev = EL_EV_PATH;
  }
  el_unlock();
}

bool el_leaf(const el_t *e, const el_endpoint_t *p, el_field_t f, double *value, el_ref_t *src) {
  const el_endpoint_t *from = p->has[f] ? p : NULL;
  for (size_t i = 0; !from && i < e->nep; i++)
    if (e->ep[i].hidden && e->ep[i].merged_into == p->key && e->ep[i].has[f]) from = &e->ep[i];
  if (!from) return false;
  *value = from->val[f];
  *src = from->src[f];
  return true;   // callers hold the lock or are the only task
}

size_t el_endpoint_count(const el_t *e) { return e->nep; }
const el_endpoint_t *el_endpoint(const el_t *e, size_t i) { return i < e->nep ? &e->ep[i] : NULL; }
uint32_t el_dropped(const el_t *e) { return e->dropped; }

void el_endpoint_id(const el_endpoint_t *p, char out[9]) {
  uint32_t h = (uint32_t)(p->key ^ (p->key >> 32)) * 2654435761u;
  for (int i = 7; i >= 0; i--, h >>= 4) out[i] = "0123456789abcdef"[h & 15];
  out[8] = 0;
}
