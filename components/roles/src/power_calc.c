#include "power_calc.h"
#include <string.h>

static bool usable(const bd_sample_t *s, int64_t max_age_ms, int64_t now_ms) {
  return s && s->kind == BD_NUMBER && s->valid && bd_is_fresh(s, max_age_ms, now_ms);
}

static bool watts(const power_src_t *s, double *w, int64_t max_age_ms, int64_t now_ms) {
  if (!s) return false;
  if (usable(s->p, max_age_ms, now_ms)) { *w = s->p->num; return true; }
  if (usable(s->v, max_age_ms, now_ms) && usable(s->i, max_age_ms, now_ms)) { *w = s->v->num * s->i->num; return true; }
  return false;
}

power_summary_t power_summarize(const power_src_t *src, size_t n, const power_src_t *batt,
                                const bd_sample_t *soc, const bd_sample_t *dc_system,
                                int64_t max_age_ms, int64_t now_ms) {
  power_summary_t r = {0};
  r.in_ok = n > 0;
  for (size_t k = 0; k < n; k++) {
    double w;
    if (!watts(&src[k], &w, max_age_ms, now_ms)) { r.in_ok = false; break; }
    if (w > 0) r.in_w += w;
  }
  if (!r.in_ok) r.in_w = 0;
  r.batt_ok = watts(batt, &r.batt_w, max_age_ms, now_ms);
  if (!r.batt_ok) r.batt_w = 0;
  if (usable(dc_system, max_age_ms, now_ms)) {
    r.load_ok = true;
    r.load_w = dc_system->num;
  } else if (r.in_ok && r.batt_ok) {
    r.load_ok = true;
    r.load_w = r.in_w - r.batt_w;
  }
  if (r.batt_ok && usable(soc, max_age_ms, now_ms)) { r.soc_ok = true; r.soc = soc->num; }
  return r;
}

uint8_t power_primary(const char ids[][24], const bool *soc_bound, uint8_t n, const char *primary) {
  if (primary && primary[0])
    for (uint8_t i = 0; i < n; i++)
      if (strcmp(ids[i], primary) == 0) return i;
  for (uint8_t i = 0; i < n; i++)
    if (soc_bound[i]) return i;
  return 0;
}

ttg_mode_t power_time_remaining(const bd_sample_t *ttg, const bd_sample_t *current, int64_t max_age_ms, int64_t now_ms) {
  if (usable(ttg, max_age_ms, now_ms)) return TTG_VALUE;
  if (usable(current, max_age_ms, now_ms) && current->num >= 0) return TTG_CHARGING;
  return TTG_FAIL;
}

power_flows_t power_flows(const power_summary_t *s) {
  power_flows_t f = {0};
  f.batt_ok = s->batt_ok;
  if (s->batt_ok) {
    if (s->batt_w > 0) f.to_batt = s->batt_w;
    else f.from_batt = -s->batt_w;
  }
  if (s->load_ok) {
    f.to_load_ok = true;
    f.to_load = s->load_w > f.from_batt ? s->load_w - f.from_batt : 0;
  }
  return f;
}

power_bank_out_t power_summarize_bank(const power_bank_in_t *in, int64_t max_age_ms, int64_t now_ms) {
  power_bank_out_t r = {0};
  r.in.ok = in->n_into > 0;
  for (size_t k = 0; r.in.ok && k < in->n_into; k++) {
    double w;
    if (!watts(&in->into[k], &w, max_age_ms, now_ms)) r.in.ok = false;
    else if (w > 0) r.in.w += w;
  }
  if (!r.in.ok) r.in.w = 0;
  r.net.ok = watts(in->batt, &r.net.w, max_age_ms, now_ms);
  if (!r.net.ok) r.net.w = 0;

  r.feed.ok = true;
  for (size_t k = 0; k < in->n_drawn; k++) {
    const power_conv_t *c = &in->drawn[k];
    double o = 0, i = 0;
    if (watts(c->in, &i, max_age_ms, now_ms)) r.feed.w += i;
    else if (watts(c->out, &o, max_age_ms, now_ms)) { r.feed.w += o / POWER_CONV_EFF; r.feed.est = true; }
    else r.feed.ok = false;
  }
  if (!r.feed.ok) { r.feed.w = 0; r.feed.est = false; }
  if (r.in.ok && r.net.ok && r.feed.ok) {
    r.load.ok = true;
    r.load.w = r.in.w - r.net.w - r.feed.w;
    r.load.est = r.feed.est;
  }
  return r;
}
