#pragma once
// The emergency script (Navigation page 1): what to say on the radio (vessel, position, nature) and the buttons that
// declare an emergency. Every button, CANCEL included, sends only after being held for EMERGENCY_HOLD_MS; letting go
// earlier sends nothing. The roles layer cannot call the app, so it acts through the functions the app registers.
#include <stdbool.h>
#include "emergency.h"
#include "role.h"

#define EMERGENCY_HOLD_MS 1000

typedef struct {
  bool (*available)(void);             // Signal K live and the role configured; the buttons hide when false
  bool (*press)(em_nature_t n);
  bool (*cancel)(void);
  const char *(*vessel_name)(void);    // "" when unknown
  const char *(*vessel_mmsi)(void);    // "" when unknown
} emergency_actions_t;

void emergency_page_set_actions(const emergency_actions_t *a);   // must outlive the page
void emergency_page_build(lv_obj_t *area, size_class_t sc, role_binding_t *b);
void emergency_page_refresh(role_binding_t *b, int64_t now_ms);   // inert when the page is not on screen
void emergency_page_reset(void);                                  // another page was built: forget the widgets

// For tests: "nature", "send", "name", "mmsi", "position", "fix", "hint", "btn_mob", "btn_fire", "btn_flood",
// "btn_sinking", "btn_other", "btn_cancel". NULL when the page is not built.
lv_obj_t *emergency_page_part(const char *name);
