#include <math.h>
#include "el_internal.h"
#include "el_product.h"

static bool is_monitor(const el_t *e, uint64_t devkey) {
  el_fn_t fn;
  bool monitor;
  for (size_t i = 0; i < EL_MAX_DEVICES; i++)
    if (e->dev[i].used && e->dev[i].key == devkey && el_product_lookup(e->dev[i].id.model_id, &fn, &monitor)) return monitor;
  return false;
}

// Which of two mirrors to keep: a monitor-class device, then one that measures current and state of charge.
static int authority(const el_t *e, const el_endpoint_t *p) {
  return (is_monitor(e, p->devkey) ? 100 : 0) + (p->has[EL_F_CURRENT] ? 10 : 0) + (p->has[EL_F_SOC] ? 5 : 0) +
         (p->has[EL_F_TIME_REMAINING] ? 2 : 0) + (p->has[EL_F_VOLTAGE] ? 1 : 0);
}

// Two batteries from different devices describe the same bank. Voltage alone is not enough to say so: at least
// one side must measure current, because two independent banks can sit at the same voltage.
static bool agree(const el_endpoint_t *p, const el_endpoint_t *q) {
  if (p->fn != EL_FN_BATTERY || q->fn != EL_FN_BATTERY || p->devkey == q->devkey) return false;
  if (!p->has[EL_F_VOLTAGE] || !q->has[EL_F_VOLTAGE] || fabs(p->val[EL_F_VOLTAGE] - q->val[EL_F_VOLTAGE]) > EL_MERGE_VOLTS) return false;
  if (!p->has[EL_F_CURRENT] && !q->has[EL_F_CURRENT]) return false;
  if (p->has[EL_F_CURRENT] && q->has[EL_F_CURRENT] && fabs(p->val[EL_F_CURRENT] - q->val[EL_F_CURRENT]) > EL_MERGE_AMPS) return false;
  if (p->has[EL_F_SOC] && q->has[EL_F_SOC] && fabs(p->val[EL_F_SOC] - q->val[EL_F_SOC]) > EL_MERGE_SOC) return false;
  return true;
}

static el_endpoint_t *by_key(el_t *e, uint64_t key) {
  for (size_t i = 0; i < e->nep; i++)
    if (e->ep[i].key == key) return &e->ep[i];
  return NULL;
}

static void unhide(el_endpoint_t *h) {
  h->hidden = false;
  h->merged_into = 0;
  h->unmerge_since = h->better_since = 0;
  h->mcand = 0;
}

static void hide(el_endpoint_t *loser, const el_endpoint_t *winner) {
  loser->hidden = true;
  loser->merged_into = winner->key;
  loser->unmerge_since = loser->better_since = 0;
}

void el_merge_run(el_t *e, int64_t now) {
  // Mirrors already merged: stay merged through a short disagreement, and let one that gained authority take over.
  for (size_t i = 0; i < e->nep; i++) {
    el_endpoint_t *h = &e->ep[i];
    if (!h->hidden) continue;
    el_endpoint_t *w = by_key(e, h->merged_into);
    if (!w || w->hidden || w->fn != EL_FN_BATTERY || h->fn != EL_FN_BATTERY) {
      unhide(h);
      continue;
    }
    if (agree(h, w)) h->unmerge_since = 0;
    else if (!h->unmerge_since) h->unmerge_since = now;
    else if (now - h->unmerge_since >= EL_MERGE_WINDOW_MS) {
      unhide(h);
      continue;
    }
    if (authority(e, h) > authority(e, w)) {
      if (!h->better_since) h->better_since = now;
      else if (now - h->better_since >= EL_MERGE_WINDOW_MS) {
        el_endpoint_t keep = *h;
        unhide(h);
        hide(w, &keep);
        for (size_t j = 0; j < e->nep; j++)   // mirrors of the old winner follow the new one
          if (e->ep[j].hidden && e->ep[j].merged_into == w->key && &e->ep[j] != w) e->ep[j].merged_into = h->key;
      }
    } else {
      h->better_since = 0;
    }
  }
  // Visible batteries that have agreed for a full window collapse into the more authoritative one.
  for (size_t i = 0; i < e->nep; i++) {
    el_endpoint_t *p = &e->ep[i];
    if (p->hidden || p->fn != EL_FN_BATTERY) continue;
    el_endpoint_t *q = NULL;
    for (size_t j = 0; j < e->nep && !q; j++)
      if (j != i && !e->ep[j].hidden && agree(p, &e->ep[j])) q = &e->ep[j];
    if (!q) {
      p->mcand = 0;
      continue;
    }
    if (p->mcand != q->key) {
      p->mcand = q->key;
      p->msince = now;
    } else if (now - p->msince >= EL_MERGE_WINDOW_MS) {
      int ap = authority(e, p), aq = authority(e, q);
      bool p_wins = ap != aq ? ap > aq : p->key < q->key;
      if (p_wins) hide(q, p);
      else hide(p, q);
      p->mcand = q->mcand = 0;
    }
  }
}
