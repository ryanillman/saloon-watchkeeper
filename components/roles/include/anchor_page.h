#pragma once
// The Anchor role's page. The behaviour is in anchor_ctl.h; this draws it.
#include "role.h"

void anchor_build_page(lv_obj_t *area, uint8_t page, size_class_t sc, role_binding_t *b);
void anchor_refresh(role_binding_t *b, int64_t now_ms);

// For tests: what the main button reads now ("DROP", "RAISE", a result such as "OFFLINE", "--"), NULL before the page is built.
const char *anchor_page_button_text(void);
lv_obj_t *anchor_page_main_button(void);   // for tests: the DROP / RAISE button, NULL before the page is built
