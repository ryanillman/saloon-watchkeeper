#include "power_page.h"

// Paths are the signalk-venus defaults and may change after a capture from the real Cerbo.
// A Cerbo only publishes on change by default and fixed-period subscriptions do not resend on
// Signal K 2.35.1 (milestone 4 verification), so no Power slot has a max age or a resend period.
static const role_slot_t battery[] = {
  {"voltage",   "voltage",        NULL, Q_VOLTAGE,  10,  16,  0, "VOLTS", 0},
  {"current",   "current",        NULL, Q_CURRENT,  -200, 200, 0, "AMPS", 0},
  {"power",     "power",          NULL, Q_POWER,    0,   0,   0, "WATTS", 0},
  // The Signal K schema and signalk-venus put these under capacity.; the bare names are a fallback.
  {"soc",       "capacity.stateOfCharge", "stateOfCharge", Q_RATIO,    0, 1, 0, "SOC", 0},
  {"remaining", "capacity.timeRemaining", "timeRemaining", Q_DURATION, 0, 0, 0, "TIME", 0},
};

static const role_slot_t solar[] = {
  {"voltage",   "voltage",        NULL, Q_VOLTAGE,  10,  16,  0, "VOLTS", 0},
  {"current",   "current",        NULL, Q_CURRENT,  0,   60,  0, "AMPS", 0},
  {"panel",     "panelPower",     NULL, Q_POWER,    0,   1000, 0, "PANEL", 0},
  {"mode",      "chargingMode",   NULL, Q_NONE,     0,   0,   0, "MODE", 0},
};

static const role_slot_t charger[] = {
  {"voltage",   "voltage",        NULL, Q_VOLTAGE,  10,  16,  0, "VOLTS", 0},
  {"current",   "current",        NULL, Q_CURRENT,  -100, 100, 0, "AMPS", 0},
  {"mode",      "chargingMode",   NULL, Q_NONE,     0,   0,   0, "MODE", 0},
};

static const role_slot_t inverter[] = {
  {"shore",     "acin.acState.acSource", "acin.connected", Q_NONE, 0, 0, 0, "SHORE", 0},
  {"mode",      "inverterMode",   NULL, Q_NONE,     0,   0,   0, "INVERTER", 0},
};

static const role_slot_t dcdc[] = {
  {"voltage",   "voltage",        NULL, Q_VOLTAGE,  10,  16,  0, "VOLTS", 0},
  {"current",   "current",        NULL, Q_CURRENT,  0,   60,  0, "AMPS", 0},
  {"mode",      "chargingMode",   NULL, Q_NONE,     0,   0,   0, "MODE", 0},
  // Input side, read only by multi-bank boats (power_topo). These suffixes are a guess: an Orion XS is
  // expected to report its input, but the signalk-venus paths have not been seen. A converter whose
  // input does not bind has its input power estimated from its output.
  {"inVolts",   "input.voltage",  NULL, Q_VOLTAGE,  0,   0,   0, "IN VOLTS", 0},
  {"inAmps",    "input.current",  NULL, Q_CURRENT,  0,   0,   0, "IN AMPS", 0},
};

static const role_slot_t alternator[] = {
  {"voltage",   "voltage",        NULL, Q_VOLTAGE,  10,  16,  0, "VOLTS", 0},
  {"current",   "current",        NULL, Q_CURRENT,  0,   150, 0, "AMPS", 0},
  {"mode",      "chargingMode",   NULL, Q_NONE,     0,   0,   0, "MODE", 0},
};

// The Cerbo's DC system power: read by the Summary only, never drawn as a page of its own.
static const role_slot_t system_slots[] = {
  {"dcPower",   "dcPower",        NULL, Q_POWER,    0,   0,   0, "LOAD", 0},
};

#define N(a) ((uint8_t)(sizeof a / sizeof a[0]))
static const role_group_t groups[] = {
  {"battery",    "electrical.batteries",  1, 4, battery,       N(battery)},
  {"solar",      "electrical.solar",      1, 4, solar,         N(solar)},
  {"charger",    "electrical.chargers",   1, 4, charger,       N(charger)},
  {"inverter",   "electrical.inverters",  1, 2, inverter,      N(inverter)},
  {"dcdc",       "electrical.dcdc",       1, 2, dcdc,          N(dcdc)},
  {"alternator", "electrical.alternators", 1, 2, alternator,   N(alternator)},
  {"system",     "electrical.venus",      0, 1, system_slots,  N(system_slots)},
};

// Pages live in page_power.c.
void power_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b);
void power_refresh(role_binding_t *b, int64_t now_ms);
uint8_t power_pages(const role_binding_t *b, size_class_t sc);
void power_tile(const role_binding_t *b, int64_t now_ms, glance_tile_t *out);

const role_def_t role_power = {
  .name = "power",
  .title = "POWER",
  .groups = groups,
  .group_count = N(groups),
  .page_count = 3,
  .pages = power_pages,
  .build_page = power_build_page,
  .refresh = power_refresh,
  .tile = power_tile,
};
