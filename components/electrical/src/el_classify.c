#include "el_internal.h"
#include "el_product.h"

typedef struct {
  bool present;
  el_fn_t fn;
} cand_t;

bool el_device_emits(const el_t *e, uint64_t devkey, el_field_t f) {
  for (size_t i = 0; i < EL_MAX_DEVICES; i++)
    if (e->dev[i].used && e->dev[i].key == devkey && (e->dev[i].emits >> f & 1)) return true;
  return false;
}

// A device that reports converter status is converter-class: its DC endpoints are the converter's sides.
static bool converter_class(const el_t *e, const el_endpoint_t *p) { return el_device_emits(e, p->devkey, EL_F_CONV_STATE); }

static uint8_t lowest_dc_instance(const el_t *e, const el_endpoint_t *p) {
  uint8_t low = 255;
  for (size_t i = 0; i < e->nep; i++) {
    const el_endpoint_t *q = &e->ep[i];
    if (q->devkey == p->devkey && q->domain == EL_DOM_DC && (q->has[EL_F_VOLTAGE] || q->has[EL_F_CURRENT]) && q->instance < low)
      low = q->instance;
  }
  return low;
}

// Rank EL_EV_DC_TYPE: the bus said what kind of DC source this is.
static cand_t by_dc_type(const el_t *e, const el_endpoint_t *p) {
  cand_t c = {false, EL_FN_UNKNOWN};
  if (!p->has[EL_F_DC_TYPE]) return c;
  c.present = true;
  if (p->dc_conflict) return c;
  switch ((int)p->val[EL_F_DC_TYPE]) {
    case 0: c.fn = converter_class(e, p) ? EL_FN_CHARGER : EL_FN_BATTERY; break;   // "battery" on a converter is its output side
    case 1: c.fn = EL_FN_ALTERNATOR; break;
    case 2: c.fn = EL_FN_CONVERTER; break;
    case 3: c.fn = EL_FN_PV; break;
    default: break;
  }
  return c;
}

// Rank EL_EV_PGN: which messages the device sends.
static cand_t by_pgn(const el_t *e, const el_endpoint_t *p) {
  cand_t c = {false, EL_FN_UNKNOWN};
  if (p->domain == EL_DOM_SWITCH) return (cand_t){true, EL_FN_SWITCH_BANK};
  if (p->domain == EL_DOM_AC) return (cand_t){true, p->has[EL_F_AC_CURRENT] ? EL_FN_AC_IN : EL_FN_UNKNOWN};
  if (!converter_class(e, p) || !(p->has[EL_F_VOLTAGE] || p->has[EL_F_CURRENT])) return c;
  c.present = true;
  // Without a DC type only the lowest instance is taken as the output; the rest stay unknown.
  if (p->has[EL_F_CURRENT] && !p->has[EL_F_SOC] && p->instance == lowest_dc_instance(e, p)) c.fn = EL_FN_CHARGER;
  return c;
}

// Rank EL_EV_PRODUCT: what the device's model says its endpoints are.
static cand_t by_product(const el_t *e, const el_endpoint_t *p) {
  cand_t c = {false, EL_FN_UNKNOWN};
  if (p->domain != EL_DOM_DC) return c;
  bool monitor;
  for (size_t i = 0; i < EL_MAX_DEVICES; i++)
    if (e->dev[i].used && e->dev[i].key == p->devkey && el_product_lookup(e->dev[i].id.model_id, &c.fn, &monitor)) {
      c.present = true;
      return c;
    }
  return c;
}

// Rank EL_EV_VOLTAGE: a stable, plausible battery voltage and nothing saying it is something else.
static cand_t by_voltage(const el_endpoint_t *p) {
  cand_t c = {false, EL_FN_UNKNOWN};
  if (p->domain != EL_DOM_DC || !p->has[EL_F_VOLTAGE] || !p->vseen) return c;
  c.present = true;
  // Stability is needed to start believing it, not to keep believing it: a load transient swings a battery's voltage.
  if (p->ev == EL_EV_VOLTAGE && p->fn == EL_FN_BATTERY && p->val[EL_F_VOLTAGE] >= EL_VOLTS_MIN && p->val[EL_F_VOLTAGE] <= EL_VOLTS_MAX) {
    c.fn = EL_FN_BATTERY;
    return c;
  }
  if (p->vlast - p->vsince >= EL_STABLE_MS && p->vlo >= EL_VOLTS_MIN && p->vhi <= EL_VOLTS_MAX) c.fn = EL_FN_BATTERY;
  return c;
}

static void classify(const el_t *e, el_endpoint_t *p) {
  if (p->ev == EL_EV_PATH) return;   // adopted from a path that already names its function
  cand_t r1 = by_dc_type(e, p), r2 = by_pgn(e, p), r3 = by_product(e, p), r4 = by_voltage(p);
  cand_t chosen = {false, EL_FN_UNKNOWN};
  el_evidence_t ev = EL_EV_NONE;
  if (r1.present) { chosen = r1; ev = EL_EV_DC_TYPE; }
  else if (r2.present && r2.fn != EL_FN_UNKNOWN) { chosen = r2; ev = EL_EV_PGN; }
  else if (r3.present) { chosen = r3; ev = EL_EV_PRODUCT; }
  else if (r2.present) { chosen = r2; ev = EL_EV_PGN; }   // PGNs ruled it out and nothing else names it: stay unknown
  else if (r4.present) { chosen = r4; ev = EL_EV_VOLTAGE; }
  p->fn = chosen.fn;
  p->ev = ev;
  p->disagree = ev != EL_EV_NONE && ((r1.present && r1.fn != chosen.fn) || (r2.present && r2.fn != chosen.fn) ||
                                     (r3.present && r3.fn != chosen.fn) ||
                                     (r4.present && r4.fn != chosen.fn));
}

void el_classify_all(el_t *e) {
  for (size_t i = 0; i < e->nep; i++) classify(e, &e->ep[i]);
}
