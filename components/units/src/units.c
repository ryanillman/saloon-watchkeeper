#include "units.h"
#include <string.h>

#define IS(a, b) (strcmp((a), (b)) == 0)

// Each display unit is si * scale + offset.
typedef struct { double scale, offset; const char *label; } conv_t;

static const double GAL = 0.003785411784;   // US gallon in m3

static conv_t conv(quantity_t q, const cfg_units_t *u) {
  switch (q) {
    case Q_TEMPERATURE:
      return IS(u->temperature, "F") ? (conv_t){1.8, -459.67, "°F"} : (conv_t){1.0, -273.15, "°C"};
    case Q_PRESSURE:
      if (IS(u->pressure, "psi")) return (conv_t){1.0 / 6894.757293168, 0, "psi"};
      if (IS(u->pressure, "kPa")) return (conv_t){0.001, 0, "kPa"};
      return (conv_t){1e-5, 0, "bar"};
    case Q_VOLUME:
      return IS(u->volume, "gal") ? (conv_t){1.0 / GAL, 0, "gal"} : (conv_t){1000.0, 0, "L"};
    case Q_FLOW:   // m3/s
      return IS(u->volume, "gal") ? (conv_t){3600.0 / GAL, 0, "gal/h"} : (conv_t){3600.0 * 1000.0, 0, "L/h"};
    case Q_SPEED:
      if (IS(u->speed, "m/s")) return (conv_t){1.0, 0, "m/s"};
      if (IS(u->speed, "km/h")) return (conv_t){3.6, 0, "km/h"};
      return (conv_t){3600.0 / 1852.0, 0, "kn"};
    case Q_DISTANCE:
      if (IS(u->distance, "m")) return (conv_t){1.0, 0, "m"};
      if (IS(u->distance, "ft")) return (conv_t){1.0 / 0.3048, 0, "ft"};
      return (conv_t){1.0 / 1852.0, 0, "nm"};
    case Q_ROTATION: return (conv_t){60.0, 0, "RPM"};
    case Q_VOLTAGE: return (conv_t){1.0, 0, "V"};
    case Q_CURRENT: return (conv_t){1.0, 0, "A"};
    case Q_RATIO: return (conv_t){100.0, 0, "%"};
    case Q_DURATION: return (conv_t){1.0 / 3600.0, 0, "h"};
    case Q_POWER: return (conv_t){1.0, 0, "W"};
    default: return (conv_t){1.0, 0, ""};
  }
}

double units_from_si(quantity_t q, double si, const cfg_units_t *u) {
  conv_t c = conv(q, u);
  return si * c.scale + c.offset;
}

double units_to_si(quantity_t q, double v, const cfg_units_t *u) {
  conv_t c = conv(q, u);
  return (v - c.offset) / c.scale;
}

const char *units_label(quantity_t q, const cfg_units_t *u) { return conv(q, u).label; }
