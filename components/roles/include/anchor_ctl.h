#pragma once
// The Anchor page's behaviour without any drawing: the staged radius, the hold-to-raise, the commands and what
// became of them. Pure (no LVGL): the page and the Glance tile read anchor_ctl_state(); touches call the functions
// below. All times are the monotonic clock in ms. UI task only.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "anchor_profile.h"
#include "anchor_state.h"
#include "anchor_watch.h"
#include "config.h"

// What happened to a command, as the source_sk command queue reports it (app.c maps its enum onto these).
enum { ANCHOR_RES_PENDING, ANCHOR_RES_OK, ANCHOR_RES_OFFLINE, ANCHOR_RES_NO_ACCESS, ANCHOR_RES_NO_PLUGIN, ANCHOR_RES_REJECTED };

// roles does not depend on source_sk: app.c supplies these from sk_cmd. submit returns a handle, or 0 when refused
// (not connected, queue full): the page shows OFFLINE. replaces_same_path: a radius-style command replaces a pending one.
typedef struct {
  uint16_t (*submit)(const anchor_cmd_t *c, bool replaces_same_path);
  int (*result)(uint16_t handle);   // ANCHOR_RES_*
} anchor_ops_t;

typedef enum { ANCHOR_BTN_DROP, ANCHOR_BTN_RAISE, ANCHOR_BTN_NONE } anchor_btn_t;

typedef struct {
  anchor_view_t view;
  anchor_btn_t button;      // what the main button does now (NONE: no data, nothing to do)
  bool main_enabled;        // false with no data: a command could not be confirmed
  bool radius_enabled;      // false with no data, or for a sector or polygon zone
  double shown_limit_m;     // the staged value, else the server's limit, else the default for a drop
  bool limit_staged;
  const char *message;      // "" or OFFLINE, NO ACCESS, NO PLUGIN, REJECTED, NOT CONFIRMED (5 s)
  int hold_pct;             // raise hold progress, 0..100
  bool no_sd;               // there is no usable card: no boot-restore protection
} anchor_ctl_state_t;

// plugin picks the command routes. watch (may be NULL) gives restoring/no-SD and is cleared on Raise or Drop; dir is
// where its record lives. units gives the display unit for steps and text (feet when depth is "ft", else metres).
void anchor_ctl_setup(const anchor_ops_t *ops, anchor_plugin_t plugin, anchor_watch_t *watch, const char *dir,
                      const cfg_units_t *units);

// Recomputes the view (writing the track into the caller's buffer) and runs the timers: the hold, the staged radius
// (sent 1.5 s after the last tap), results, and NOT CONFIRMED (10 s after the server accepted with no change seen).
void anchor_ctl_update(int64_t now_ms, anchor_pt_t *track, size_t track_cap);
const anchor_ctl_state_t *anchor_ctl_state(void);

// The same view for another reader (the Glance tile), with its own track buffer.
void anchor_build_view(int64_t now_ms, anchor_view_t *v, anchor_pt_t *track, size_t track_cap);

void anchor_ctl_tap_main(int64_t now_ms);     // a tap on DROP sends the drop; on RAISE it does nothing
void anchor_ctl_hold_begin(int64_t now_ms);   // pressing RAISE: sends after 1.5 s of holding
void anchor_ctl_hold_end(int64_t now_ms);     // releasing early cancels
void anchor_ctl_page_left(void);              // the page is being rebuilt or deleted under the user: cancels a hold and an unsent staged radius
void anchor_ctl_step(int dir, int64_t now_ms);   // -1 / +1: stage the radius one step (5 m or 15 ft; 5..300 m)

// Text shared by the page and the Glance tile. A distance in metres as the display shows it: feet when units.depth
// is "ft", else metres, no decimals ("--" for NAN); with_unit appends " m" or " ft". `seconds` as "12 min" or "3h12m".
void anchor_format_distance(char *out, size_t n, double metres, const cfg_units_t *units, bool with_unit);
const char *anchor_distance_unit(const cfg_units_t *units);   // "m" or "ft"
void anchor_format_ago(char *out, size_t n, int64_t seconds);

// Text shared by the page and the Glance tile. A distance in metres as the display shows it: feet when units.distance
// is "ft", else metres, no decimals ("--" for NAN); with_unit appends " m" or " ft". `seconds` as "12 min" or "3h12m".
void anchor_format_distance(char *out, size_t n, double metres, const cfg_units_t *units, bool with_unit);
const char *anchor_distance_unit(const cfg_units_t *units);   // "m" or "ft"
void anchor_format_ago(char *out, size_t n, int64_t seconds);
