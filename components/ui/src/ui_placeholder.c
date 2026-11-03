#include "ui_placeholder.h"

static void placeholder_page(lv_obj_t *area, uint8_t page, size_class_t sc, void *ctx) {
  const ui_role_view_t *r = ctx;
  lv_obj_t *l = lv_label_create(area);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text_fmt(l, "%s\npage %u of %u\n%s", r->name, page + 1, r->page_count,
                        sc == SIZE_WIDE ? "wide" : "square");
  lv_obj_center(l);
}

static ui_role_view_t roles[] = {
  {"ENGINE", 2, placeholder_page, NULL},
  {"POWER", 2, placeholder_page, NULL},
  {"TANKS", 1, placeholder_page, NULL},
  {"ANCHOR", 1, placeholder_page, NULL},
};

const ui_role_view_t *ui_placeholder_roles(uint8_t *count) {
  *count = sizeof roles / sizeof roles[0];
  for (uint8_t i = 0; i < *count; i++) roles[i].ctx = &roles[i];
  return roles;
}
