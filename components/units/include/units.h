#pragma once
// Signal K stores SI. These convert to and from what the glass shows.
#include "config.h"

typedef enum {
  Q_TEMPERATURE, Q_PRESSURE, Q_VOLUME, Q_SPEED, Q_DISTANCE, Q_ROTATION,
  Q_VOLTAGE, Q_CURRENT, Q_RATIO, Q_DURATION, Q_FLOW, Q_POWER,
  Q_DEPTH,   // water depth and tide height: feet when the depth unit is ft, otherwise metres
  Q_NONE
} quantity_t;

double units_from_si(quantity_t q, double si, const cfg_units_t *u);
double units_to_si(quantity_t q, double v, const cfg_units_t *u);
const char *units_label(quantity_t q, const cfg_units_t *u);   // "°C", "bar", "RPM", "%", "h", "L/h"
