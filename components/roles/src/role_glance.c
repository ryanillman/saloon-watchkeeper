#include "glance_page.h"

void glance_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b);
void glance_refresh(role_binding_t *b, int64_t now_ms);

const role_def_t role_glance = {
  .name = "glance",
  .title = "GLANCE",
  .groups = NULL,
  .group_count = 0,
  .page_count = 1,
  .build_page = glance_build_page,
  .refresh = glance_refresh,
};
