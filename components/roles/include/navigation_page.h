#pragma once
// The Navigation role's page: the HSI-style compass, its readouts and fault flags. What it shows is worked out in nav_state.h.
#include "role.h"

void navigation_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b);
void navigation_refresh(role_binding_t *b, int64_t now_ms);
void navigation_tile(const role_binding_t *b, int64_t now_ms, glance_tile_t *out);

#include "ui_rose.h"

// For tests: a part of the page by name ("rose", "heading", "ap_badge", "awa", "aws", "twa", "tws", "sog", "course",
// "xte", "dtw", "ap_hdg", "set_drift"); NULL before the page is built or for an unknown name. The readouts are
// ui_readout widgets (ui_readout_text, ui_field_faulted); set_drift is a label; hidden parts carry LV_OBJ_FLAG_HIDDEN.
lv_obj_t *navigation_page_part(const char *name);
// What the page last gave the rose.
const ui_rose_data_t *navigation_page_rose_data(void);
