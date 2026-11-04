#pragma once
// Signal K alerts on the glass: the alarm banner (un-acknowledged alarms only), the alert/warn
// marker, the acknowledged-alarm bell, the list,
// and "SK ALERTS OFFLINE". Everything is driven by ui_alerts_update.
#include <stdbool.h>
#include <stdint.h>
#include "alerts.h"
#include "lvgl.h"
#include "ui_theme.h"

// Role index for GO: maps an alert id to a configured role, or -1.
typedef int8_t (*ui_alert_role_fn)(const char *alert_id);
// Page within that role. NULL means page 0. Called when GO is tapped.
typedef uint8_t (*ui_alert_page_fn)(const char *alert_id);

// Builds the banner, marker, offline text and list on `screen`; the marker and
// offline text go in `strip`, left of its status text. Call after ui_shell_create.
void ui_alerts_attach(lv_obj_t *screen, lv_obj_t *strip, size_class_t sc, ui_alert_role_fn role_of,
                      ui_alert_page_fn page_of);
void ui_alerts_update(const alerts_snapshot_t *s, int64_t now_ms);   // UI task, every tick

// Queries, for tests.
bool ui_alerts_banner_visible(void);
const char *ui_alerts_banner_text(void);
bool ui_alerts_go_visible(void);
const char *ui_alerts_acked_text(void);     // "" when hidden, bell and count of acknowledged alarm/emergency
const char *ui_alerts_marker_text(void);    // "" when hidden, "2" for two alert/warn
const char *ui_alerts_offline_text(void);   // "SK ALERTS OFFLINE" or ""
const char *ui_alerts_corner_text(void);    // banner corner: "+N", "ACK NOT SENT" or ""
bool ui_alerts_list_visible(void);
uint8_t ui_alerts_list_rows(void);

typedef enum { UI_ALERTS_BANNER, UI_ALERTS_ACK, UI_ALERTS_SILENCE, UI_ALERTS_GO, UI_ALERTS_MARKER, UI_ALERTS_CLOSE, UI_ALERTS_CORNER, UI_ALERTS_OFFLINE,
               UI_ALERTS_ROW0_ACK, UI_ALERTS_ACKED_MARKER } ui_alerts_part_t;   // ROW0_ACK: the first list row's ACK
lv_obj_t *ui_alerts_part(ui_alerts_part_t p);
