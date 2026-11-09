#include "source_can.h"
#include <stdio.h>
#include <string.h>
#include "platform.h"

static const char *mapped(const source_can_cfg_t *cfg, const char *key) {
  for (size_t i = 0; i < cfg->nids; i++)
    if (!strcmp(cfg->ids[i].key, key)) return cfg->ids[i].id;
  return NULL;
}

// The id for a (group, instance): the mapped id, or the decimal instance.
static void id_for(const source_can_cfg_t *cfg, const char *key, uint8_t instance, char *out, size_t n) {
  const char *m = mapped(cfg, key);
  if (m) snprintf(out, n, "%s", m);
  else snprintf(out, n, "%u", instance);
}

bool source_can_path(const n2k_value_t *v, const source_can_cfg_t *cfg, char *out, size_t n) {
  char key[48], id[32];
  int w = -1;
  switch (v->field) {
    case N2K_F_DEPTH_BELOW_TRANSDUCER: w = snprintf(out, n, "environment.depth.belowTransducer"); break;
    case N2K_F_DEPTH_SURFACE_TO_TRANSDUCER: w = snprintf(out, n, "environment.depth.surfaceToTransducer"); break;
    case N2K_F_DEPTH_TRANSDUCER_TO_KEEL: w = snprintf(out, n, "environment.depth.transducerToKeel"); break;
    case N2K_F_WIND_SPEED_APPARENT: w = snprintf(out, n, "environment.wind.speedApparent"); break;
    case N2K_F_WIND_ANGLE_APPARENT: w = snprintf(out, n, "environment.wind.angleApparent"); break;
    case N2K_F_BATT_VOLTAGE:
    case N2K_F_BATT_CURRENT:
    case N2K_F_BATT_TEMPERATURE:
      snprintf(key, sizeof key, "battery.%u", v->instance);
      id_for(cfg, key, v->instance, id, sizeof id);
      w = snprintf(out, n, "electrical.batteries.%s.%s", id,
                   v->field == N2K_F_BATT_VOLTAGE ? "voltage" : v->field == N2K_F_BATT_CURRENT ? "current" : "temperature");
      break;
    case N2K_F_ENGINE_REVOLUTIONS:
    case N2K_F_ENGINE_BOOST:
      snprintf(key, sizeof key, "engine.%u", v->instance);
      id_for(cfg, key, v->instance, id, sizeof id);
      w = snprintf(out, n, "propulsion.%s.%s", id, v->field == N2K_F_ENGINE_REVOLUTIONS ? "revolutions" : "boostPressure");
      break;
    case N2K_F_TANK_LEVEL:
    case N2K_F_TANK_CAPACITY: {
      const char *fluid = n2k_fluid_name(v->fluid);
      if (!fluid) return false;
      snprintf(key, sizeof key, "tank.%s.%u", fluid, v->instance);
      id_for(cfg, key, v->instance, id, sizeof id);
      w = snprintf(out, n, "tanks.%s.%s.%s", fluid, id, v->field == N2K_F_TANK_LEVEL ? "currentLevel" : "capacity");
      break;
    }
  }
  return w > 0 && (size_t)w < n;
}

void source_can_init(source_can_t *s, const source_can_cfg_t *cfg) {
  memset(s, 0, sizeof *s);
  s->cfg = *cfg;
  s->state = BD_SRC_CONNECTING;
  bd_source_register(BD_SRC_N2K, "nmea2000", 1);
  bd_source_set_state(BD_SRC_N2K, BD_SRC_CONNECTING);
}

static void on_value(const n2k_value_t *v, void *ctx) {
  source_can_t *s = ctx;
  char path[96];
  if (!source_can_path(v, &s->cfg, path, sizeof path)) return;
  bd_id_t id = s->cfg.create_paths ? bd_intern(path) : bd_find(path);
  if (id == BD_NONE) {
    s->unmapped++;
    return;
  }
  bd_set_number(id, v->value, BD_SRC_N2K, platform_millis());
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
  if (s->state == BD_SRC_LIVE && platform_millis() - s->last_msg_ms > SOURCE_CAN_SILENCE_MS) {
    s->state = BD_SRC_OFFLINE;
    bd_source_set_state(BD_SRC_N2K, BD_SRC_OFFLINE);
    bd_invalidate_source(BD_SRC_N2K);
  }
}
