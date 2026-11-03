#pragma once
// A role knows what data it shows. Binding turns its slot definitions into
// boatdata ids for the instances this boat has.
#include "boatdata.h"
#include "config.h"
#include "lvgl.h"
#include "ui_theme.h"
#include "units.h"

#define ROLE_MAX_INST 4
#define ROLE_MAX_SLOTS 12

typedef struct {
  const char *slot;         // "rpm"
  const char *suffix;       // path below the instance: "revolutions"
  const char *fallback;     // tried when suffix is absent, or NULL
  quantity_t q;
  double def_min, def_max;  // SI; equal = no range (a readout)
  int32_t max_age_ms;       // 0 = quiet is fine
  const char *fail_label;   // "RPM" → "RPM FAIL"
} role_slot_t;

struct role_binding;

typedef struct {
  const char *name;              // config name: "engine"
  const char *title;             // strip title: "ENGINE"
  const char *instance_prefix;   // "propulsion": instances are propulsion.<id>.*
  const role_slot_t *slots;
  uint8_t slot_count;
  uint8_t page_count;
  void (*build_page)(lv_obj_t *area, uint8_t page, size_class_t sc, struct role_binding *b);
} role_def_t;

typedef struct role_binding {
  const role_def_t *def;
  const cfg_units_t *units;
  uint8_t inst_count;
  char inst_id[ROLE_MAX_INST][24];
  char inst_label[ROLE_MAX_INST][12];
  bd_id_t ids[ROLE_MAX_INST][ROLE_MAX_SLOTS];
  double min[ROLE_MAX_INST][ROLE_MAX_SLOTS], max[ROLE_MAX_INST][ROLE_MAX_SLOTS];   // SI
} role_binding_t;

const role_def_t *role_find(const char *name);   // NULL if not implemented

// Instances: pinned in config, else discovered (sorted), else one placeholder
// labelled with the role title whose slots are all BD_NONE.
void role_bind(const role_def_t *def, const cfg_role_t *cfg, const cfg_units_t *units, role_binding_t *out);
