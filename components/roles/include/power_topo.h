#pragma once
// Power topology resolved against the bound instances (spec 2026-11-12-power-configuration-design.md).
// Config (`cfg_role_t.bank`, `.flow`) names batteries and devices by ref (CAN NAME, "<NAME>.<instance>"); this maps
// them to the group and instance indices the pages read, by matching the `ref` leaf the resolver publishes. An
// entry whose device is not seen keeps its place with `bound` false, so the figure that needs it fails visibly
// instead of the entry vanishing.
#include "power_page.h"

enum { BK_HOUSE, BK_START, BK_NAV, BK_WINDLASS, BK_THRUSTER, BK_GENSET, BK_OTHER, BK_KINDS };
enum { FLOW_SOLAR, FLOW_SHORE, FLOW_ENGINE, FLOW_CONVERTER, FLOW_OTHER, FLOW_KINDS };
#define TOPO_MAX_BANKS CFG_MAX_BANKS
#define TOPO_MAX_FLOWS CFG_MAX_FLOWS

typedef struct {
  bool bound;        // the device's output endpoint is seen
  uint8_t g, i;      // group (G_CHARGER, or G_ALTERNATOR for an engine flow) and instance index
  uint8_t output;    // which of the device's outputs, by instance order
  uint8_t kind;      // FLOW_*
  int8_t from, to;   // bank indices; from is -1 unless a converter
  char dev[17];      // the device NAME as configured
} power_flow_t;

typedef struct {
  bool bound;        // the battery endpoint is seen
  uint8_t inst;      // battery instance index, valid when bound
  uint8_t kind;      // BK_*
  char id[12];
  char name[12];     // configured, else the kind in capitals
} power_bank_t;

typedef struct {
  bool active;                             // a house bank is configured
  int8_t house, start, nav;                // bank index of the house bank and the first start and nav banks, else -1
  power_bank_t bank[TOPO_MAX_BANKS];
  uint8_t bank_count;
  power_flow_t flow[TOPO_MAX_FLOWS];
  uint8_t flow_count;
  int8_t batt_bank[ROLE_MAX_INST];         // per battery instance: the bank holding it, else -1 (unassigned)
  int8_t out_flow[2][ROLE_MAX_INST];       // per charger [0] and alternator [1] instance: the flow reading it, else -1
  uint8_t unassigned_batt, unassigned_dev; // seen and placed nowhere (instances, whether or not they have a NAME)
} power_topo_t;

void power_topo_resolve(const role_binding_t *b, power_topo_t *t);

// Bank index by configured id, -1 when none.
int power_topo_bank(const power_topo_t *t, const char *id);

// What each bank shows, computed from live samples: IN, NET, LOAD and the converter feed (power_calc.h), and its state of
// charge. A flow whose device is not seen takes part as an unusable source, so it fails IN rather than vanishing.
typedef struct {
  power_bank_out_t fig;
  bool soc_ok;
  double soc;      // ratio 0..1
} power_bank_view_t;
typedef struct { power_bank_view_t bank[TOPO_MAX_BANKS]; } power_banks_view_t;

void power_banks_compute(const role_binding_t *b, const power_topo_t *t, int64_t now_ms, power_banks_view_t *out);

// Bank a device's endpoint feeds (-1 when no flow names it), by the flow reading that instance; a solar panel input follows
// the flow of its own device. For a converter, *from is the bank it draws from (else -1). `from` may be NULL.
int power_topo_dest(const role_binding_t *b, const power_topo_t *t, uint8_t g, uint8_t i, int *from);

// The flows into `bank` as text for the bank's detail: "ALTERNATOR 40 W  CHARGER 0 W". One entry per non-converter flow,
// in flow order (converters are drawn as arrows). An unseen or unreadable source reads "<NAME> FAIL".
void power_bank_sources(const role_binding_t *b, const power_topo_t *t, int bank, int64_t now_ms, const char *sep,
                        char *out, size_t n);

// Shore power: AC present at the input, not output watts. Over the `shore` flows: a flow's device with a fresh AC-in
// endpoint (a reading no older than SHORE_FRESH_MS; its value does not matter, a charger feeding 0 W still has AC);
// else a bound inverter's AC source; else inferred from the flow's output (above zero = present), which says so.
// *watts (may be NULL) is the sum of the shore flows' usable output. Strongest answer wins when flows disagree.
#define SHORE_FRESH_MS 60000
typedef enum { SHORE_UNKNOWN, SHORE_ABSENT, SHORE_PRESENT, SHORE_INFERRED_ABSENT, SHORE_INFERRED_PRESENT } shore_t;
shore_t power_shore(const role_binding_t *b, const power_topo_t *t, int64_t now_ms, double *watts);

// An inverter's AC source: 1 on shore ("grid", "shore" or acin.connected = 1), 0 reporting something else, -1 none reporting.
int power_inverter_shore(const role_binding_t *b);
