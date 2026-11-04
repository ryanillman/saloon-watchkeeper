#pragma once
// Power Summary arithmetic: watts in, battery net, DC load, state of charge.
// A source contributes max(0, w) to IN; negative contributions are already inside
// the battery's net (the monitor sees the inverter's draw), so they are neither
// summed into IN nor added to the load again.
// DC LOAD = dc_system when valid and fresh, else IN - battery net. A usable dc_system stands on
// its own: load_ok is true even if a source or the bank is invalid (in_ok/batt_ok stay false).
// With no sources (n == 0) in_ok is false, so load fails unless dc_system is usable.
// soc_ok also requires batt_ok: the soc belongs to the primary bank.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "boatdata.h"

typedef struct { const bd_sample_t *v, *i, *p; } power_src_t;   // any may be NULL; p is used when valid, else v*i

typedef struct {
  bool in_ok;    double in_w;       // sum of positive DC-side charge power
  bool batt_ok;  double batt_w;     // primary bank, + charging
  bool load_ok;  double load_w;     // DC LOAD
  bool soc_ok;   double soc;        // ratio 0..1
} power_summary_t;

// Where the power goes, for the Summary diagram. The bus (all sources together) feeds the battery
// when it charges and the load; a discharging battery feeds the load too. Watts, never negative.
// to_load is the bus's share of DC LOAD (load minus what the battery supplies). Battery flows need
// batt_ok; a battery of unknown state is treated as supplying nothing.
typedef struct {
  bool to_load_ok;  double to_load;      // bus -> load
  bool batt_ok;     double to_batt;      // bus -> battery
                    double from_batt;    // battery -> load
} power_flows_t;
power_flows_t power_flows(const power_summary_t *s);

// Sources: solar, charger, dcdc, alternator instances, flattened. batt: the primary bank.
// dc_system: the Cerbo's DC system power sample, or NULL. max_age_ms 0 = no limit.
power_summary_t power_summarize(const power_src_t *src, size_t n, const power_src_t *batt,
                                const bd_sample_t *soc, const bd_sample_t *dc_system,
                                int64_t max_age_ms, int64_t now_ms);

// Index of the primary bank: the instance whose id equals `primary` if non-empty and present,
// else the first with a stateOfCharge path bound (soc_bound[i]), else 0.
uint8_t power_primary(const char ids[][24], const bool *soc_bound, uint8_t n, const char *primary);

typedef enum { TTG_VALUE, TTG_CHARGING, TTG_FAIL } ttg_mode_t;
ttg_mode_t power_time_remaining(const bd_sample_t *ttg, const bd_sample_t *current, int64_t max_age_ms, int64_t now_ms);
