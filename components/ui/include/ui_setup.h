#pragma once
// On-device setup: a stack of screens drawn over the page area, below the top strip and the
// alarm banner. Edits a working copy of the config; SAVE hands it to the app, which applies
// it (and saves it when there is an SD card). Presentation only: the app supplies data and
// actions through ui_setup_cb_t.
#include <stdbool.h>
#include <stddef.h>
#include "config.h"
#include "lvgl.h"
#include "ui_theme.h"

typedef enum { SETUP_MENU, SETUP_WIFI, SETUP_SIGNALK, SETUP_SOURCES, SETUP_ROLES, SETUP_DISCOVERY, SETUP_DISPLAY, SETUP_ABOUT } ui_setup_screen_t;

#define SETUP_IDLE_MS 300000   // closes after 5 minutes without a touch; unsaved changes are dropped

// A Wi-Fi network from a scan.
typedef struct { char ssid[33]; int8_t rssi; bool secure; } ui_ap_t;

// What discovery found for one role (ids as written in config: "port", "battery.279").
typedef struct { char role[12]; char ids[16][24]; uint8_t count; } ui_disc_role_t;

typedef struct {
  void (*save)(const cfg_t *edited);   // UI task: apply, and save when there is an SD card
  void (*about)(char *out, size_t n);  // About text, one item per line
  bool has_buzzer, has_sd, dims;       // dims: the backlight has a brightness level, not just on/off
  uint8_t (*discovery)(ui_disc_role_t *out, uint8_t max);   // what is found now, per configured role
  void (*disc_name)(const char *role, const char *id, char *out, size_t n);   // the source's own name for an id, "" if none (NULL: none)
  void (*run_discovery)(void);         // read the Signal K tree again; ui_setup_discovery_changed follows
  const char *const *roles_available;  // roles this firmware draws, NULL-terminated (ADD)
  void (*wifi_scan)(void);             // start a scan off the UI task; ui_setup_wifi_results follows
  void (*wifi_state)(char *line, size_t n);    // "connected Boatnet 192.168.0.40 -61 dBm"
  void (*sk_state)(char *line, size_t n);      // "live 192.168.0.108:3000"
  void (*request_access)(void);
  void (*access_state)(char *line, size_t n);  // "pending (waiting for approval ...)"
} ui_setup_cb_t;

void ui_setup_open(lv_obj_t *screen, const cfg_t *current, size_class_t sc, const ui_setup_cb_t *cb);
void ui_setup_close(void);
bool ui_setup_is_open(void);
void ui_setup_show(ui_setup_screen_t s);
ui_setup_screen_t ui_setup_screen(void);
void ui_setup_tick(void);              // UI task, every tick: inactivity timeout
void ui_setup_discovery_changed(void); // new discovery results: the Discovery screen redraws
void ui_setup_wifi_results(const ui_ap_t *aps, int n);   // UI task: a scan finished

// For tests: the edited copy, whether it differs from the last save, and named parts
// ("root", "back", "save", "close", "discard_yes", "discard_no", "menu_<screen>",
// "unit_<kind>_<value>", "palette_<name>", "brightness", "buzzer", "about_text", "nosd",
// "up_<role>", "down_<role>", "remove_<role>", "add_<role>", "start_<role>", "run",
// "keep_<role>_<id>", "missing_<role>_<id>", "label_<role>_<id>", "confirm_<role>",
// "auto_<role>", "scan", "ap_<ssid>", "ap_other", "wifi_state", "sk_auto", "sk_manual",
// "edit_host", "edit_port", "sk_state", "src_sk", "src_both", "src_bus", "src_note", "request_access", "access_state", "kb", "kb_text").
bool ui_setup_dirty(void);
const cfg_t *ui_setup_working(void);
lv_obj_t *ui_setup_part(const char *name);
