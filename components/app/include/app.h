#pragma once
// Startup shared by the simulator and the firmware.
#include "emergency.h"
#include "config.h"
#include "lvgl.h"
#include "platform.h"
#include "sk_parse.h"
#include "ui_theme.h"

// Clears boatdata and loads config from dir (NULL = defaults). Returns the config.
const cfg_t *app_init(const char *config_dir, cfg_report_t *report);

// The paths to subscribe to: every bound role slot, the extras the pages read, and the Anchor role's paths when it is
// configured. Written to out (at most max); returns how many.
size_t app_bound_ids(sk_sub_t *out, size_t max);

// Binds every configured role against what boatdata knows now.
void app_bind(void);

void app_start_ui(lv_display_t *disp, size_class_t sc);
void app_start_network(void);   // Signal K client for every bound path

// The buzzer: fn(on) drives it, enabled_by_board is false when the board has none (or no driver).
// The config's buzzer.enabled also has to be on. Call before app_tick starts.
void app_set_buzzer(void (*fn)(bool on), bool enabled_by_board);

// The backlight: fn(percent) sets it; dims is false when the board only switches it on and off
// (the quick panel then shows ON instead of a slider). Call before app_start_ui, which applies
// the stored brightness. Pass fn NULL to leave the backlight alone (the simulator).
void app_set_backlight(void (*fn)(uint8_t percent), bool dims);

// The board's display reset (restarts the panel scan). With it the quick panel shows RESET SCREEN, and
// display.reset_after_swipe takes effect. Call before app_start_ui. NULL: no button.
void app_set_display_reset(void (*fn)(void));

// ---- Emergency buttons (see docs/specs/2026-11-06-emergency-design.md) ----
// Available when the Navigation role is configured and Signal K is live: the buttons are hidden otherwise.
bool app_emergency_available(void);
// Declares `n`: captures the position and time now, raises the notification (a single attempt, 3 s deadline, no retry)
// and for a MOB with a position the Signal K waypoint. The local state is set whatever the send does; a failed send
// raises the alarm "NOT SENT - TELL HELM". False (nothing changed) when unavailable or a send is still pending.
bool app_emergency_press(em_nature_t n);
// Sends the cancel for the active emergency. False when unavailable or the send was refused; the emergency stays
// active until the server has accepted the cancel.
bool app_emergency_cancel(void);
// The name and MMSI the script page says: vessel.name from the config when set, else what Signal K reports ("" if unknown).
const char *app_vessel_name(void);
const char *app_vessel_mmsi(void);

// UI task, about every 100 ms: rebinds after a discovery, refreshes the page and status.
void app_tick(void);

// The config in use (read it on the UI task) and its directory ("" without an SD card).
const cfg_t *app_config(void);
const char *app_config_dir(void);

// Makes `next` the config in use, doing only what changed. UI task only. Does not save.
typedef enum { APPLY_NONE = 0, APPLY_DISPLAY = 1, APPLY_SIGNALK = 2, APPLY_ROLES = 4, APPLY_WIFI = 8 } app_apply_t;
unsigned app_apply_config(const cfg_t *next);

// The configured role (index) that shows this alert id (a notification path without "notifications."), or -1.
int8_t app_alert_role(const char *id);

// Runs fn(arg) on the UI task at its next app_tick. With wait_ms > 0 the caller blocks until it
// has run (false on timeout); never wait from the UI task itself. Up to 8 may be queued.
typedef void (*app_req_fn)(void *arg);
bool app_post(app_req_fn fn, void *arg, uint32_t wait_ms);

// What `status` and About show. Firmware and board come from app_set_info.
void app_set_info(const char *board, const char *version);
typedef struct {
  char firmware[32], board[16];
  bool wifi; char ip[16]; int8_t rssi;
  char signalk[96];          // "live 192.168.0.108:3000", "connecting auto", "offline 10.0.0.5:3000"
  bool token;
  char access[128];          // access request state and message
  unsigned internal_kb, psram_kb;
  unsigned internal_largest_kb, internal_min_kb;   // largest free block; least free since boot
  platform_stack_t stacks[8]; uint8_t stack_count;
  bool sd;
  char origin[10];           // "main", "backup", "defaults"
  uint8_t alerts; char worst[10];
} app_status_t;
bool app_status(app_status_t *out);   // any task except the UI task (runs there via app_post)

// Console edits, from any task except the UI task. Both refuse without an SD card.
// config set: cfg_set on the config writer task, then apply on the UI task.
bool app_config_set(const char *path, const char *text, cfg_report_t *err, unsigned *applied);
bool app_config_reload(cfg_report_t *rep, unsigned *applied);

// Starts a Signal K access request; false when Signal K is not running. `as` gets the name
// the request shows in the admin UI ("boat-instruments lcd7b 2a:10").
bool app_request_access(char *as, size_t n);
