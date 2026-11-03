#pragma once
// Signal K alerts on the glass: the alarm banner, the alert/warn marker and list,
// and "SK ALERTS OFFLINE". Everything is driven by ui_alerts_update.
#include <stdbool.h>
#include <stdint.h>
#include "alerts.h"
#include "lvgl.h"
#include "ui_theme.h"

// Role index for GO: maps an alert id to a configured role, or -1.
typedef int8_t (*ui_alert_role_fn)(const char *alert_id);

// Builds the banner, marker, offline text and list on `screen`; the marker and
// offline text go in `strip`, left of its status text. Call after ui_shell_create.
void ui_alerts_attach(lv_obj_t *screen, lv_obj_t *strip, size_class_t sc, ui_alert_role_fn role_of);
void ui_alerts_update(const alerts_snapshot_t *s, int64_t now_ms);   // UI task, every tick

// Queries, for tests.
bool ui_alerts_banner_visible(void);
const char *ui_alerts_banner_text(void);
bool ui_alerts_go_visible(void);
const char *ui_alerts_marker_text(void);    // "" when hidden, "2" for two alert/warn
const char *ui_alerts_offline_text(void);   // "SK ALERTS OFFLINE" or ""
const char *ui_alerts_corner_text(void);    // banner corner: "+N", "ACK NOT SENT" or ""
bool ui_alerts_list_visible(void);
uint8_t ui_alerts_list_rows(void);

typedef enum { UI_ALERTS_BANNER, UI_ALERTS_ACK, UI_ALERTS_SILENCE, UI_ALERTS_GO, UI_ALERTS_MARKER, UI_ALERTS_CLOSE } ui_alerts_part_t;
lv_obj_t *ui_alerts_part(ui_alerts_part_t p);
