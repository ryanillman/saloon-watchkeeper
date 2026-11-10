#include "source_can.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "platform.h"

static const char *mapped(const source_can_cfg_t *cfg, const char *key) {
  for (size_t i = 0; i < cfg->nids; i++)
    if (!strcmp(cfg->ids[i].key, key)) return cfg->ids[i].id;
  return NULL;
}

bool source_can_is_instance(const n2k_value_t *v) {
  switch (v->field) {
    case N2K_F_BATT_VOLTAGE: case N2K_F_BATT_CURRENT: case N2K_F_BATT_TEMPERATURE:
    case N2K_F_BATT_SOC: case N2K_F_BATT_TIME_REMAINING:
    case N2K_F_ENGINE_REVOLUTIONS: case N2K_F_ENGINE_BOOST: case N2K_F_ENGINE_OIL_PRESSURE:
    case N2K_F_ENGINE_OIL_TEMPERATURE: case N2K_F_ENGINE_COOLANT_TEMPERATURE:
    case N2K_F_ENGINE_ALTERNATOR_VOLTAGE: case N2K_F_ENGINE_FUEL_RATE: case N2K_F_ENGINE_HOURS:
    case N2K_F_TANK_LEVEL: case N2K_F_TANK_CAPACITY:
      return true;
    default:
      return false;
  }
}

// The config key for an instance value ("engine.0", "battery.1", "tank.fuel.0"); false for non-instance values.
static bool key_for(const n2k_value_t *v, char *key, size_t n) {
  switch (v->field) {
    case N2K_F_BATT_VOLTAGE: case N2K_F_BATT_CURRENT: case N2K_F_BATT_TEMPERATURE:
    case N2K_F_BATT_SOC: case N2K_F_BATT_TIME_REMAINING:
      snprintf(key, n, "battery.%u", v->instance);
      return true;
    case N2K_F_ENGINE_REVOLUTIONS: case N2K_F_ENGINE_BOOST: case N2K_F_ENGINE_OIL_PRESSURE:
    case N2K_F_ENGINE_OIL_TEMPERATURE: case N2K_F_ENGINE_COOLANT_TEMPERATURE:
    case N2K_F_ENGINE_ALTERNATOR_VOLTAGE: case N2K_F_ENGINE_FUEL_RATE: case N2K_F_ENGINE_HOURS:
      snprintf(key, n, "engine.%u", v->instance);
      return true;
    case N2K_F_TANK_LEVEL: case N2K_F_TANK_CAPACITY: {
      const char *fluid = n2k_fluid_name(v->fluid);
      if (!fluid) return false;
      snprintf(key, n, "tank.%s.%u", fluid, v->instance);
      return true;
    }
    default:
      return false;
  }
}

bool source_can_is_mapped(const n2k_value_t *v, const source_can_cfg_t *cfg) {
  char key[48];
  return key_for(v, key, sizeof key) && mapped(cfg, key) != NULL;
}

// The id for an instance value: the mapped id, or the decimal instance.
static void id_for(const source_can_cfg_t *cfg, const n2k_value_t *v, char *out, size_t n) {
  char key[48];
  const char *m = key_for(v, key, sizeof key) ? mapped(cfg, key) : NULL;
  if (m) snprintf(out, n, "%s", m);
  else snprintf(out, n, "%u", v->instance);
}

bool source_can_path(const n2k_value_t *v, const source_can_cfg_t *cfg, char *out, size_t n) {
  char id[32];
  const char *leaf = NULL;
  int w = -1;
  switch (v->field) {
    case N2K_F_DEPTH_BELOW_TRANSDUCER: w = snprintf(out, n, "environment.depth.belowTransducer"); break;
    case N2K_F_DEPTH_SURFACE_TO_TRANSDUCER: w = snprintf(out, n, "environment.depth.surfaceToTransducer"); break;
    case N2K_F_DEPTH_TRANSDUCER_TO_KEEL: w = snprintf(out, n, "environment.depth.transducerToKeel"); break;
    case N2K_F_WIND_SPEED_APPARENT: w = snprintf(out, n, "environment.wind.speedApparent"); break;
    case N2K_F_WIND_ANGLE_APPARENT: w = snprintf(out, n, "environment.wind.angleApparent"); break;
    case N2K_F_HEADING_TRUE: w = snprintf(out, n, "navigation.headingTrue"); break;
    case N2K_F_HEADING_MAGNETIC: w = snprintf(out, n, "navigation.headingMagnetic"); break;
    case N2K_F_MAG_VARIATION: w = snprintf(out, n, "navigation.magneticVariation"); break;
    case N2K_F_COG_TRUE: w = snprintf(out, n, "navigation.courseOverGroundTrue"); break;
    case N2K_F_COG_MAGNETIC: w = snprintf(out, n, "navigation.courseOverGroundMagnetic"); break;
    case N2K_F_SOG: w = snprintf(out, n, "navigation.speedOverGround"); break;
    case N2K_F_STW: w = snprintf(out, n, "navigation.speedThroughWater"); break;
    case N2K_F_POSITION: w = snprintf(out, n, "navigation.position"); break;
    case N2K_F_TIME_EPOCH: w = snprintf(out, n, "navigation.datetime"); break;
    case N2K_F_BATT_VOLTAGE: leaf = "voltage"; break;
    case N2K_F_BATT_CURRENT: leaf = "current"; break;
    case N2K_F_BATT_TEMPERATURE: leaf = "temperature"; break;
    case N2K_F_BATT_SOC: leaf = "capacity.stateOfCharge"; break;
    case N2K_F_BATT_TIME_REMAINING: leaf = "capacity.timeRemaining"; break;
    case N2K_F_ENGINE_REVOLUTIONS: leaf = "revolutions"; break;
    case N2K_F_ENGINE_BOOST: leaf = "boostPressure"; break;
    case N2K_F_ENGINE_OIL_PRESSURE: leaf = "oilPressure"; break;
    case N2K_F_ENGINE_OIL_TEMPERATURE: leaf = "oilTemperature"; break;
    case N2K_F_ENGINE_COOLANT_TEMPERATURE: leaf = "temperature"; break;
    case N2K_F_ENGINE_ALTERNATOR_VOLTAGE: leaf = "alternatorVoltage"; break;
    case N2K_F_ENGINE_FUEL_RATE: leaf = "fuel.rate"; break;
    case N2K_F_ENGINE_HOURS: leaf = "runTime"; break;
    case N2K_F_TANK_LEVEL: leaf = "currentLevel"; break;
    case N2K_F_TANK_CAPACITY: leaf = "capacity"; break;
  }
  if (leaf) {
    id_for(cfg, v, id, sizeof id);
    switch (v->field) {
      case N2K_F_BATT_VOLTAGE: case N2K_F_BATT_CURRENT: case N2K_F_BATT_TEMPERATURE:
      case N2K_F_BATT_SOC: case N2K_F_BATT_TIME_REMAINING:
        w = snprintf(out, n, "electrical.batteries.%s.%s", id, leaf);
        break;
      case N2K_F_TANK_LEVEL: case N2K_F_TANK_CAPACITY: {
        const char *fluid = n2k_fluid_name(v->fluid);
        if (!fluid) return false;
        w = snprintf(out, n, "tanks.%s.%s.%s", fluid, id, leaf);
        break;
      }
      default:
        w = snprintf(out, n, "propulsion.%s.%s", id, leaf);
        break;
    }
  }
  return w > 0 && (size_t)w < n;
}

void source_can_init(source_can_t *s, const source_can_cfg_t *cfg) {
  memset(s, 0, sizeof *s);
  s->cfg = *cfg;
  s->init_ms = platform_millis();
  s->state = BD_SRC_CONNECTING;
  bd_source_register(BD_SRC_N2K, "nmea2000", 1);
  bd_source_set_state(BD_SRC_N2K, BD_SRC_CONNECTING);
}

static void on_value(const n2k_value_t *v, void *ctx) {
  source_can_t *s = ctx;
  char path[96];
  if (!source_can_path(v, &s->cfg, path, sizeof path)) return;
  // Instance values could duplicate an instance Signal K names differently, so in fallback mode only mapped
  // ones may create their path. Everything else has one fixed path and may.
  bool may_create = s->cfg.create_paths || !source_can_is_instance(v) || source_can_is_mapped(v, &s->cfg);
  bd_id_t id = may_create ? bd_intern(path) : bd_find(path);
  if (id == BD_NONE) {
    s->unmapped++;
    return;
  }
  int64_t now = platform_millis();
  if (v->field == N2K_F_POSITION) {
    bd_set_position(id, v->value, v->value2, BD_SRC_N2K, now);
  } else if (v->field == N2K_F_TIME_EPOCH) {
    time_t t = (time_t)v->value;
    struct tm tm;
    char iso[64];   // roomy: the compiler cannot know tm fields are small
    gmtime_r(&t, &tm);
    snprintf(iso, sizeof iso, "%04d-%02d-%02dT%02d:%02d:%02d.000Z", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    bd_set_text(id, iso, BD_SRC_N2K, now);
  } else {
    bd_set_number(id, v->value, BD_SRC_N2K, now);
  }
  s->values++;
}

void source_can_on_msg(const n2k_msg_t *m, void *ctx) {
  source_can_t *s = ctx;
  if (!n2k_decode(m, on_value, s)) return;
  s->last_msg_ms = platform_millis();
  if (s->state != BD_SRC_LIVE) {
    s->state = BD_SRC_LIVE;
    bd_source_set_state(BD_SRC_N2K, BD_SRC_LIVE);
  }
}

void source_can_tick(source_can_t *s) {
  if (s->state == BD_SRC_CONNECTING && platform_millis() - s->init_ms > SOURCE_CAN_SILENCE_MS) {
    s->state = BD_SRC_OFFLINE;
    bd_source_set_state(BD_SRC_N2K, BD_SRC_OFFLINE);
    return;
  }
  if (s->state == BD_SRC_LIVE && platform_millis() - s->last_msg_ms > SOURCE_CAN_SILENCE_MS) {
    s->state = BD_SRC_OFFLINE;
    bd_source_set_state(BD_SRC_N2K, BD_SRC_OFFLINE);
    bd_invalidate_source(BD_SRC_N2K);
  }
}
