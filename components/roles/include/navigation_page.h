#pragma once
// The Navigation role's page: the HSI-style compass, its readouts and fault flags. What it shows is worked out in nav_state.h.
#include "role.h"

void navigation_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b);
void navigation_refresh(role_binding_t *b, int64_t now_ms);
void navigation_tile(const role_binding_t *b, int64_t now_ms, glance_tile_t *out);
