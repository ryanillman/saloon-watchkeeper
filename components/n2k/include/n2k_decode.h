// PGN decoders for the values the instruments show. They emit (field, instance, fluid, SI value)
// and know nothing about boatdata or Signal K paths: the display side maps them (source_can).
// Every PGN, offset and scale here is from memory and unverified.  // verify: canboat
#pragma once
#include "n2k.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  N2K_F_DEPTH_BELOW_TRANSDUCER,       // m
  N2K_F_DEPTH_SURFACE_TO_TRANSDUCER,  // m, from a positive offset
  N2K_F_DEPTH_TRANSDUCER_TO_KEEL,     // m, from a negative offset
  N2K_F_WIND_SPEED_APPARENT,          // m/s
  N2K_F_WIND_ANGLE_APPARENT,          // rad, -pi..pi, positive to starboard
  N2K_F_BATT_VOLTAGE,                 // V
  N2K_F_BATT_CURRENT,                 // A
  N2K_F_BATT_TEMPERATURE,             // K
  N2K_F_ENGINE_REVOLUTIONS,           // Hz
  N2K_F_ENGINE_BOOST,                 // Pa
  N2K_F_TANK_LEVEL,                   // ratio 0..1
  N2K_F_TANK_CAPACITY,                // m3
  N2K_F_ENGINE_OIL_PRESSURE,          // Pa
  N2K_F_ENGINE_OIL_TEMPERATURE,       // K
  N2K_F_ENGINE_COOLANT_TEMPERATURE,   // K
  N2K_F_ENGINE_ALTERNATOR_VOLTAGE,    // V
  N2K_F_ENGINE_FUEL_RATE,             // m3/s
  N2K_F_ENGINE_HOURS,                 // s
  N2K_F_BATT_SOC,                     // ratio 0..1
  N2K_F_BATT_TIME_REMAINING,          // s
  N2K_F_HEADING_TRUE,                 // rad
  N2K_F_HEADING_MAGNETIC,             // rad
  N2K_F_MAG_VARIATION,                // rad
  N2K_F_COG_TRUE,                     // rad
  N2K_F_COG_MAGNETIC,                 // rad
  N2K_F_SOG,                          // m/s
  N2K_F_STW,                          // m/s
  N2K_F_POSITION,                     // value = latitude, value2 = longitude (degrees)
  N2K_F_TIME_EPOCH,                   // UTC seconds since 1970
} n2k_field_t;

typedef struct {
  n2k_field_t field;
  uint8_t instance;  // battery, engine and tank fields; 0 otherwise
  uint8_t fluid;     // tank fields: 0 fuel, 1 freshWater, 2 wasteWater, 3 liveWell, 4 oil, 5 blackWater, 6 gasoline; 255 other
  double value;
  double value2;  // longitude for N2K_F_POSITION, 0 otherwise
} n2k_value_t;

typedef void (*n2k_value_cb)(const n2k_value_t *v, void *ctx);

// Calls cb once per available field. Not-available and error values (all-ones patterns) are dropped.
// Returns true when the PGN is one this decodes, even if no field was available; false otherwise.
bool n2k_decode(const n2k_msg_t *m, n2k_value_cb cb, void *ctx);

// The Signal K tank group name for a fluid type, NULL for 255.
const char *n2k_fluid_name(uint8_t fluid);

#ifdef __cplusplus
}
#endif
