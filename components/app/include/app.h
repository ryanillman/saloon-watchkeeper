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

// UI task, about every 100 ms: rebinds after a discovery, refreshes the page and status.
void app_tick(void);
