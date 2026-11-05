#pragma once
// The Depth role's page and Glance tile. Slot order is the order in role_depth.c.
#include "role.h"

enum { G_DEPTH, G_TIDE };   // group indices
enum { D_TRANSDUCER, D_KEEL, D_SURFACE };   // depth group slots, in depth_ref_t order
enum { T_HEIGHT, T_HIGH, T_LOW, T_TIME_HIGH, T_TIME_LOW };   // tide group slots

void depth_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b);
void depth_refresh(role_binding_t *b, int64_t now_ms);
void depth_tile(const role_binding_t *b, int64_t now_ms, glance_tile_t *out);
