#include "role.h"

// Default ranges are SI: Hz, K, Pa, V, s, m3/s.
static const role_slot_t slots[] = {
  {"rpm",     "revolutions",       NULL,                 Q_ROTATION,    0,      4000.0 / 60.0, 0, "RPM"},
  {"coolant", "temperature",       "coolantTemperature", Q_TEMPERATURE, 313.15, 393.15,        0, "COOLANT"},
  {"oil",     "oilPressure",       NULL,                 Q_PRESSURE,    0,      600000,        0, "OIL"},
  {"volts",   "alternatorVoltage", NULL,                 Q_VOLTAGE,     10,     16,            0, "VOLTS"},
  {"hours",   "runTime",           NULL,                 Q_DURATION,    0,      0,             0, "HOURS"},
  {"fuel",    "fuel.rate",         NULL,                 Q_FLOW,        0,      40.0 / 1000.0 / 3600.0, 0, "FUEL"},
};

// Pages live in page_engine.c.
void engine_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b);
void engine_refresh(role_binding_t *b, int64_t now_ms);

const role_def_t role_engine = {
  .name = "engine",
  .title = "ENGINE",
  .instance_prefix = "propulsion",
  .slots = slots,
  .slot_count = sizeof slots / sizeof slots[0],
  .page_count = 2,
  .build_page = engine_build_page,
  .refresh = engine_refresh,
};
