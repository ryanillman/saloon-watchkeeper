#pragma once
// Tanks page helpers, pure so they can be tested without a display.
#include <stddef.h>
#include <stdint.h>
#include "boatdata.h"
#include "ui_theme.h"

typedef enum { TANK_FRESH, TANK_FUEL, TANK_BLACK, TANK_GREY, TANK_OTHER } tank_kind_t;

tank_kind_t tank_kind(const char *inst_id);   // "freshWater.0" -> TANK_FRESH
// of_type: tanks of this kind on the boat. ("freshWater.0", 2) -> "FRESH 1"; ("fuel.0", 1) -> "FUEL"
void tank_label(const char *inst_id, uint8_t of_type, char *out, size_t n);
// Litres-as-m3 for the tank: reported volume, else level x capacity. false = flag.
bool tank_volume(const bd_sample_t *level, const bd_sample_t *volume, const bd_sample_t *capacity, double *m3);
uint8_t tank_pages(uint8_t tanks, size_class_t sc);   // wide: 1; square: ceil(tanks / 4), at least 1
