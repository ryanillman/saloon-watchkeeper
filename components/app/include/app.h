#pragma once
// Startup shared by the simulator and the firmware.
#include "config.h"
#include "lvgl.h"
#include "ui_theme.h"

// Clears boatdata and loads config from dir (NULL = defaults). Returns the config.
const cfg_t *app_init(const char *config_dir, cfg_report_t *report);

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

// UI task, about every 100 ms: rebinds after a discovery, refreshes the page and status.
void app_tick(void);

// The config in use (read it on the UI task) and its directory ("" without an SD card).
const cfg_t *app_config(void);
const char *app_config_dir(void);

// Makes `next` the config in use, doing only what changed. UI task only. Does not save.
typedef enum { APPLY_NONE = 0, APPLY_DISPLAY = 1, APPLY_SIGNALK = 2, APPLY_ROLES = 4, APPLY_WIFI = 8 } app_apply_t;
unsigned app_apply_config(const cfg_t *next);

// Runs fn(arg) on the UI task at its next app_tick. With wait_ms > 0 the caller blocks until it
// has run (false on timeout); never wait from the UI task itself. Up to 8 may be queued.
typedef void (*app_req_fn)(void *arg);
bool app_post(app_req_fn fn, void *arg, uint32_t wait_ms);
