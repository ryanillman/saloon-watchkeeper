#include <math.h>
#include <string.h>
#include "el_internal.h"

#define PROVISIONAL (1ull << 63)

static uint64_t fnv(uint64_t h, const char *s) {
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 1099511628211ull;
  return h;
}

uint64_t el_dev_key(const el_device_t *d) {
  if (d->id.can_name) return d->id.can_name & ~PROVISIONAL;
  if (d->id.serial[0]) {
    uint64_t h = fnv(14695981039346656037ull, d->id.model_id);
    h = fnv(h * 1099511628211ull, d->id.serial);
    return h & ~PROVISIONAL;
  }
  return PROVISIONAL | (uint64_t)d->feed << 32 | d->ref;
}

static bool has_endpoint(const el_t *e, uint64_t devkey) {
  for (size_t i = 0; i < e->nep; i++)
    if (e->ep[i].devkey == devkey) return true;
  return false;
}

el_device_t *el_dev_find_or_add(el_t *e, uint8_t feed, el_ref_t ref) {
  el_device_t *free_slot = NULL, *idle = NULL;
  for (size_t i = 0; i < EL_MAX_DEVICES; i++) {
    el_device_t *d = &e->dev[i];
    if (d->used && d->feed == feed && d->ref == ref) return d;
    if (!d->used && !free_slot) free_slot = d;
    if (d->used && !idle && !has_endpoint(e, d->key)) idle = d;
  }
  // A server's sources tree and a bus's address claims name every node, electrical or not. A device that has
  // reported nothing electrical gives its slot up to one that does; its identity is sent again when it matters.
  if (!free_slot) free_slot = idle;
  if (!free_slot) {
    e->dropped++;
    return NULL;
  }
  memset(free_slot, 0, sizeof *free_slot);
  free_slot->used = true;
  free_slot->feed = feed;
  free_slot->ref = ref;
  free_slot->key = el_dev_key(free_slot);
  return free_slot;
}

static uint64_t ep_key(uint64_t devkey, uint8_t domain, uint8_t instance) { return (devkey * 31 + instance) * 3 + domain; }

el_endpoint_t *el_ep_find(el_t *e, uint64_t devkey, uint8_t domain, uint8_t instance) {
  uint64_t key = ep_key(devkey, domain, instance);
  for (size_t i = 0; i < e->nep; i++)
    if (e->ep[i].key == key) return &e->ep[i];
  return NULL;
}

el_endpoint_t *el_ep_find_or_add(el_t *e, uint64_t devkey, uint8_t domain, uint8_t instance) {
  el_endpoint_t *found = el_ep_find(e, devkey, domain, instance);
  if (found) return found;
  if (e->nep >= EL_MAX_ENDPOINTS) {
    e->dropped++;
    return NULL;
  }
  el_endpoint_t *p = &e->ep[e->nep++];
  memset(p, 0, sizeof *p);
  p->key = ep_key(devkey, domain, instance);
  p->devkey = devkey;
  p->instance = instance;
  p->domain = domain;
  return p;
}

// Moves every endpoint of device key `from` to `to`; one that lands on an existing endpoint is merged into it.
void el_rekey(el_t *e, uint64_t from, uint64_t to) {
  for (size_t i = e->nep; i-- > 0;) {
    el_endpoint_t *p = &e->ep[i];
    if (p->devkey != from) continue;
    uint64_t nk = ep_key(to, p->domain, p->instance);
    el_endpoint_t *target = NULL;
    for (size_t j = 0; j < e->nep; j++)
      if (j != i && e->ep[j].key == nk) target = &e->ep[j];
    if (!target) {
      p->devkey = to;
      p->key = nk;
      continue;
    }
    for (int f = 0; f < EL_F_COUNT; f++)
      if (p->has[f] && (!target->has[f] || p->t[f] > target->t[f])) {
        target->has[f] = true;
        target->val[f] = p->val[f];
        target->src[f] = p->src[f];
        target->feed[f] = p->feed[f];
        target->dirty |= (uint16_t)(1u << f);
        target->t[f] = p->t[f];
      }
    if (p->pubd && e->ngone < sizeof e->gone / sizeof e->gone[0]) {
      e->gone[e->ngone].key = p->pubkey;
      e->gone[e->ngone++].fn = p->pubfn;
    }
    e->ep[i] = e->ep[--e->nep];
  }
}
