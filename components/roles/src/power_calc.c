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

power_banks_t power_summarize_banks(const power_banks_in_t *in, int64_t max_age_ms, int64_t now_ms) {
  power_banks_t r = {0};
  r.house_in.ok = in->n_into_house > 0;
  for (size_t k = 0; r.house_in.ok && k < in->n_into_house; k++) {
    double w;
    if (!watts(&in->into_house[k], &w, max_age_ms, now_ms)) r.house_in.ok = false;
    else if (w > 0) r.house_in.w += w;
  }
  if (!r.house_in.ok) r.house_in.w = 0;
  r.house_batt.ok = watts(in->house_batt, &r.house_batt.w, max_age_ms, now_ms);
  if (!r.house_batt.ok) r.house_batt.w = 0;

  double draw = 0, out_sum = 0;
  bool draw_ok = true, out_ok = in->has_nav && in->n_to_nav > 0, est = false;
  for (size_t k = 0; k < in->n_to_nav; k++) {
    const power_conv_t *c = &in->to_nav[k];
    double o = 0, i = 0;
    bool have_out = watts(c->out, &o, max_age_ms, now_ms);
    if (have_out) out_sum += o; else out_ok = false;
    if (watts(c->in, &i, max_age_ms, now_ms)) draw += i;
    else if (have_out) { draw += o / POWER_CONV_EFF; est = true; }
    else draw_ok = false;
  }
  if (in->n_to_nav > 0 && draw_ok) {
    r.nav_feed.ok = true;
    r.nav_feed.w = draw;
    r.nav_feed.est = est;
  }
  if (r.house_in.ok && r.house_batt.ok && draw_ok) {
    r.house_load.ok = true;
    r.house_load.w = r.house_in.w - r.house_batt.w - draw;
    r.house_load.est = est;
  }
  double nb;
  if (out_ok && watts(in->nav_batt, &nb, max_age_ms, now_ms)) {
    r.nav_load.ok = true;
    r.nav_load.w = out_sum - nb;
  }
  return r;
}
