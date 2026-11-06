#pragma once
// The emergency script (Navigation page 1): the VHF distress call to read aloud, for someone who has never used the radio.
// Two numbered steps (press and hold DISTRESS, then speak), then the call itself one thing to say per line, in the largest
// text that fits: the boat's name three times, the position in spoken words, the nature of the emergency. There are no
// buttons here; an emergency is declared from the quick panel (ui_quick) and this page follows what is declared, whether it
// was declared on this display or raised elsewhere.
#include <stdbool.h>
#include "emergency.h"
#include "role.h"

// The boat's name for the call ("" = unknown, said so in brackets, never guessed). Must outlive the page.
void emergency_page_set_vessel_name(const char *(*fn)(void));

void emergency_page_build(lv_obj_t *area, size_class_t sc, role_binding_t *b);
void emergency_page_refresh(role_binding_t *b, int64_t now_ms);   // inert when the page is not on screen
void emergency_page_reset(void);                                  // another page was built: forget the widgets

// For tests: "send", "steps", "script". NULL when the page is not built or there is no such part.
lv_obj_t *emergency_page_part(const char *name);
