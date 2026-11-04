#pragma once
// Power topology resolved against the bound instances (spec 2026-11-04-multi-bank-power-design.md).
// Config (`cfg_role_t.bank`, `.link`) names banks and sources by id; this maps them to the group and
// instance indices the pages read. An entry whose instance is not bound keeps its place with
// `bound` false, so the figure that needs it fails visibly instead of the entry vanishing.
#include "power_page.h"

enum { BANK_HOUSE, BANK_START, BANK_NAV, BANK_KINDS };
#define TOPO_MAX_LINKS CFG_MAX_LINKS

typedef struct {
  bool bound;        // the source instance is a real bound instance
  uint8_t g, i;      // group (G_CHARGER, G_SOLAR, G_DCDC, G_ALTERNATOR) and instance index
  uint8_t output;    // charger output index, else 0
  int8_t from, to;   // BANK_*; from is -1 unless a DC-DC
} power_link_t;

typedef struct {
  bool active;                           // banks configured with a house bank
  bool bank_cfg[BANK_KINDS];             // the kind is configured
  bool bank_bound[BANK_KINDS];           // ... and its battery instance is bound
  uint8_t bank_inst[BANK_KINDS];         // battery instance index, valid when bank_bound
  char bank_name[BANK_KINDS][12];        // configured name, else HOUSE / START / NAV
  power_link_t link[TOPO_MAX_LINKS];
  uint8_t link_count;
} power_topo_t;

void power_topo_resolve(const role_binding_t *b, power_topo_t *t);

// What the house Summary and the Banks page show, computed from live samples. Every link to the
// house bank takes part, a link whose instance is unbound as an unusable source, so it fails HOUSE IN
// rather than vanishing. A charger counts through its `output 0` link only (its other outputs are
// not in the house arithmetic until a capture shows per-output readings).
typedef struct {
  power_banks_t fig;
  bool house_soc_ok, start_soc_ok, nav_soc_ok;
  double house_soc, start_soc, nav_soc;      // ratio 0..1
} power_banks_view_t;

void power_banks_compute(const role_binding_t *b, const power_topo_t *t, int64_t now_ms, power_banks_view_t *out);

// Bank a source feeds, by its output-0 link (else its first link); -1 when no link names it. For a DC-DC,
// *from is the bank it draws from (else -1). `from` may be NULL.
int power_topo_dest(const power_topo_t *t, uint8_t g, uint8_t i, int *from);

// The sources feeding `bank`, as text for the bank's detail: "ALTERNATOR 40 W  FLOAT CHARGER". One entry per
// non-converter link, in link order (converters are drawn as arrows). A charger's non-main output has no
// reading of its own, so it is named without watts. An unbound or unreadable source reads "<NAME> FAIL".
void power_bank_sources(const role_binding_t *b, const power_topo_t *t, int bank, int64_t now_ms, const char *sep,
                        char *out, size_t n);
