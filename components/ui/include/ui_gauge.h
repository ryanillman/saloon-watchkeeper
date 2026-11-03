#pragma once
// Instrument widgets: hero dial, strip gauge, digital readout. Values are in
// display units. Every field starts failed and shows the fault flag (red cross,
// yellow "<LABEL> FAIL") until a valid value arrives.
#include <stdbool.h>
#include "lvgl.h"

typedef enum { SWEEP_HALF = 180, SWEEP_THREE_QUARTER = 270, SWEEP_FULL = 360 } ui_sweep_t;

typedef struct { double lo, hi; lv_color_t color; } ui_band_t;

typedef struct {
  const char *label;   // "RPM"; the fault flag reads "<label> FAIL"
  const char *unit;    // "RPM", "°C", "bar"
  double lo, hi;
  ui_sweep_t sweep;    // dials only
  const ui_band_t *bands;
  uint8_t band_count;
  uint8_t decimals;
  bool has_color;      // strips: fill colour; otherwise the palette's foreground
  lv_color_t color;
} ui_gauge_spec_t;

// The spec (and its bands) must outlive the widget.
lv_obj_t *ui_dial_create(lv_obj_t *parent, const ui_gauge_spec_t *spec, uint8_t needle_count,
                         const char *const *needle_labels);   // labels may be NULL
void ui_dial_set(lv_obj_t *dial, uint8_t needle, double value, bool valid);
void ui_dial_set_off(lv_obj_t *dial, uint8_t needle);   // engine off: dim "OFF", no needle, no cross

lv_obj_t *ui_strip_create(lv_obj_t *parent, const ui_gauge_spec_t *spec, bool vertical);
void ui_strip_set(lv_obj_t *strip, double value, bool valid);
void ui_strip_set_off(lv_obj_t *strip);                 // dim "OFF", no pointer, no cross

lv_obj_t *ui_readout_create(lv_obj_t *parent, const char *label, const char *unit, uint8_t decimals);
void ui_readout_set(lv_obj_t *readout, double value, bool valid);
// Shows a string instead of a number (a charging mode, "1:30"); empty or !valid flags the field.
void ui_readout_set_text(lv_obj_t *readout, const char *text, bool valid);
// Replaces the default "<LABEL> FAIL" flag text, for a label too long to repeat (DC LOAD shows LOAD FAIL).
void ui_field_set_fault_text(lv_obj_t *field, const char *text);

// Colours the value text for an alert on this field: 0 normal (foreground), 1 alert and 2 warn
// (palette warn), 3 alarm and 4 emergency (palette alarm). Same numbering as alert_state_t.
void ui_field_set_alert(lv_obj_t *field, uint8_t state);
lv_color_t ui_field_value_color(lv_obj_t *field);

// Queries, for pages and tests.
bool ui_field_faulted(lv_obj_t *field);          // the whole field is flagged
const char *ui_field_fault_text(lv_obj_t *field);
bool ui_dial_needle_visible(lv_obj_t *dial, uint8_t needle);
bool ui_dial_needle_faulted(lv_obj_t *dial, uint8_t needle);
double ui_dial_needle_fraction(lv_obj_t *dial, uint8_t needle);
const char *ui_dial_text(lv_obj_t *dial, uint8_t needle);
double ui_strip_fraction(lv_obj_t *strip);
const char *ui_strip_text(lv_obj_t *strip);
const char *ui_readout_text(lv_obj_t *readout);
