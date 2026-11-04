#pragma once
// A role knows what data it shows. Binding turns its slot definitions into
// boatdata ids for the instances this boat has.
#include "alerts.h"
#include "boatdata.h"
#include "config.h"
#include "glance.h"
#include "lvgl.h"
#include "ui_gauge.h"
#include "ui_theme.h"
#include "units.h"

#define ROLE_MAX_GROUPS 8   // Power uses 7
#define ROLE_MAX_INST 8
#define ROLE_MAX_SLOTS 8

typedef struct {
  const char *slot;         // "rpm"
  const char *suffix;       // path below the instance: "revolutions"
  const char *fallback;     // tried when suffix is absent, or NULL
  quantity_t q;
  double def_min, def_max;  // SI; equal = no range (a readout)
  int32_t max_age_ms;       // 0 = quiet is fine
  const char *fail_label;   // "RPM" → "RPM FAIL"
  uint32_t resend_ms;       // 0 = on change
} role_slot_t;

struct role_binding;

// A kind of device a role reads. A role with one group uses bare instance ids
// in config; with several, config ids are "<group name>.<id>".
typedef struct {
  const char *name;          // "battery"; prefixes config ids when the role has more than one group
  const char *prefix;        // "propulsion", "tanks", "electrical.solar"
  uint8_t id_depth;          // path segments in an instance id: 1 "port", 2 "freshWater.0"
  uint8_t max_inst;          // <= ROLE_MAX_INST
  const role_slot_t *slots;
  uint8_t slot_count;        // <= ROLE_MAX_SLOTS
} role_group_t;

typedef struct {
  const char *name;              // config name: "engine"
  const char *title;             // strip title: "ENGINE"
  const role_group_t *groups;
  uint8_t group_count;
  uint8_t page_count;
  uint8_t (*pages)(const struct role_binding *b, size_class_t sc);   // NULL = page_count
  // Page that shows this alert path (the data path, without "notifications."). NULL = page 0.
  uint8_t (*page_for)(const struct role_binding *b, size_class_t sc, const char *path);
  void (*build_page)(lv_obj_t *area, uint8_t page, size_class_t sc, struct role_binding *b);
  void (*refresh)(struct role_binding *b, int64_t now_ms);   // updates the widgets build_page made
  // The role's Glance tile, from the data now. NULL = no tile. `out` is zeroed first by the caller; set title and state.
  void (*tile)(const struct role_binding *b, int64_t now_ms, glance_tile_t *out);
} role_def_t;

typedef struct {
  uint8_t inst_count;
  char inst_id[ROLE_MAX_INST][24];       // relative to the group prefix: "freshWater.0"
  char inst_label[ROLE_MAX_INST][12];
  bd_id_t ids[ROLE_MAX_INST][ROLE_MAX_SLOTS];
  double min[ROLE_MAX_INST][ROLE_MAX_SLOTS], max[ROLE_MAX_INST][ROLE_MAX_SLOTS];   // SI
} role_group_binding_t;

typedef struct role_binding {
  const role_def_t *def;
  const cfg_units_t *units;
  const cfg_role_t *cfg;                  // for role options such as "primary"; may be NULL
  role_group_binding_t g[ROLE_MAX_GROUPS];
} role_binding_t;

// Builds the gauge spec for slot k of instance i in group g, in display units, with
// meta.zones as bands. Callers set decimals and sweep. spec, bands and label must outlive the widget.
const ui_gauge_spec_t *role_gauge_spec(role_binding_t *b, uint8_t g, uint8_t i, uint8_t k, ui_gauge_spec_t *spec,
                                       ui_band_t bands[6], char label[32]);

const role_def_t *role_find(const char *name);

// Names of the roles this firmware draws, NULL-terminated (setup's ADD list).
const char *const *role_names(void);

// What discovery finds for this role right now, ignoring pins and excludes: instance ids,
// sorted per group, prefixed with the group name in multi-group roles ("battery.279").
uint8_t role_discover(const role_def_t *d, char ids[][24], uint8_t max);   // NULL if not implemented

// Per group: instances pinned in config, else discovered (sorted), else one placeholder
// labelled with the role title whose slots are all BD_NONE.
void role_bind(const role_def_t *def, const cfg_role_t *cfg, const cfg_units_t *units, role_binding_t *out);

// True if both bindings have the same instances and slot ids in every group.
bool role_binding_same(const role_binding_t *a, const role_binding_t *b);

// Colours a field's value for the Signal K alert on that boatdata path (normal resets it).
void role_field_alert(lv_obj_t *field, bd_id_t id);
