#include "role.h"

#include "engine_state.h"

// Default ranges are SI: Hz, K, Pa, V, s, m3/s. A running engine's controller
// transmits continuously, so silence past the max age is a fault (or OFF; see
// engine_state.h). Hours may be sent rarely and never ages out.
#define RPM_AGE ENGINE_RPM_MAX_AGE_MS
#define FIELD_AGE ENGINE_FIELD_MAX_AGE_MS
static const role_slot_t slots[] = {
  {"rpm",     "revolutions",       NULL,                 Q_ROTATION,    0,      4000.0 / 60.0, RPM_AGE,   "RPM", 0},
  {"coolant", "temperature",       "coolantTemperature", Q_TEMPERATURE, 313.15, 393.15,        FIELD_AGE, "COOLANT", 0},
  {"oil",     "oilPressure",       NULL,                 Q_PRESSURE,    0,      600000,        FIELD_AGE, "OIL", 0},
  {"volts",   "alternatorVoltage", NULL,                 Q_VOLTAGE,     10,     16,            FIELD_AGE, "VOLTS", 0},
  {"hours",   "runTime",           NULL,                 Q_DURATION,    0,      0,             0,         "HOURS", 0},
  {"fuel",    "fuel.rate",         NULL,                 Q_FLOW,        0,      40.0 / 1000.0 / 3600.0, FIELD_AGE, "FUEL", 0},
  {"state",   "state",             NULL,                 Q_NONE,        0,      0,             0,         "STATE", 0},
};

static const role_group_t groups[] = {
  {"engine", "propulsion", 1, 2, slots, sizeof slots / sizeof slots[0]},
};

// Pages live in page_engine.c.
void engine_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b);
void engine_refresh(role_binding_t *b, int64_t now_ms);
uint8_t engine_page_for(const role_binding_t *b, size_class_t sc, const char *path);

const role_def_t role_engine = {
  .name = "engine",
  .title = "ENGINE",
  .groups = groups,
  .group_count = 1,
  .page_count = 2,
  .page_for = engine_page_for,
  .build_page = engine_build_page,
  .refresh = engine_refresh,
};
