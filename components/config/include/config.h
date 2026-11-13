#pragma once
// /config.json on the SD card: the device's source of truth.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CFG_VERSION 1
#define CFG_MAX_ROLES 8

typedef struct { char ssid[33]; char password[65]; } cfg_wifi_t;
typedef struct {
  char host[64]; uint16_t port; char token[256];
  char request[128];   // href of a pending access request, resumed after a restart; "" = none
} cfg_sk_t;   // host "auto" = mDNS
// distance: nm mi km (course, cross-track); depth: ft m (water depth, tide, anchor radius)
typedef struct { char temperature[2], pressure[4], volume[4], speed[5], distance[3], depth[3]; } cfg_units_t;
typedef struct { char id[24]; char label[12]; } cfg_instance_t;
typedef struct { char slot[48]; char path[96]; } cfg_bind_t;          // slot "<instance>.<slot>"
typedef struct { char slot[24]; bool has_min, has_max; double min, max; } cfg_override_t;
// Power configuration (spec 2026-11-12-power-configuration-design.md). A bank is a named battery endpoint with a kind;
// a flow is a device that moves energy into (or, for a converter, between) banks. Refs are CAN NAMEs in lower-case
// hex: a device is "<NAME>", an endpoint "<NAME>.<instance>".
typedef struct { char id[12]; char name[12]; char kind[9]; char battery[24]; } cfg_bank_t;   // kind: house start nav windlass thruster genset other
typedef struct { char device[17]; char kind[10]; char to[12]; char from[12]; uint8_t output; } cfg_flow_t;   // kind: solar shore engine converter other; to/from are bank ids
#define CFG_MAX_BANKS 8
#define CFG_MAX_FLOWS 16

typedef struct {
  char role[12];
  uint8_t src_entry;  // 1-based entry in the file's roles array this came from; 0 = new
  char primary[24];   // power: instance id of the primary battery bank; empty = automatic
  cfg_bank_t bank[CFG_MAX_BANKS]; uint8_t bank_count;   // power: 0 = not set up
  cfg_flow_t flow[CFG_MAX_FLOWS]; uint8_t flow_count;
  bool power_rejected;   // power: the file's banks/flows were invalid (or old-format) and are kept as written, not loaded
  bool power_old_keys;   // power: ... because they were in the old format (links, or banks named by instance id)
  cfg_instance_t inst[8]; uint8_t inst_count;
  cfg_bind_t bind[8]; uint8_t bind_count;
  cfg_override_t ovr[8]; uint8_t ovr_count;
  char exclude[8][48]; uint8_t exclude_count;
} cfg_role_t;
// sources.nmea2000: the NMEA 2000 bus as a data source. ids maps a bus instance to the Signal K id the
// server uses ("engine.0" -> "port", "battery.1" -> "279", "tank.fuel.0" -> "port").
typedef struct { char key[24]; char id[24]; } cfg_n2k_id_t;
#define CFG_MAX_N2K_IDS 16
typedef struct {
  bool enabled;
  bool only;   // mode "only": no Signal K, the bus creates every path. "fallback" (default): Signal K first
  cfg_n2k_id_t ids[CFG_MAX_N2K_IDS];
  uint8_t id_count;
} cfg_n2k_t;
typedef struct {
  cfg_wifi_t wifi;
  cfg_sk_t sk;
  cfg_n2k_t n2k;
  cfg_units_t units;
  char palette[6];          // "auto" | "day" | "dusk" | "night"
  uint8_t brightness;       // 0..100
  int16_t utc_offset_min;   // display.utc_offset_min: local time minus UTC, -720..840
  char start_role[12];
  bool buzzer;
  bool reset_after_swipe;   // display.reset_after_swipe: restart the display scan 400 ms after the last page change
  char vessel_name[32];     // vessel.name: shown on the emergency script; empty = use the name Signal K reports
  char anchor_plugin[24];   // anchor.plugin: "hoekens-anchor-alarm" (default) | "anchoralarm"; which Signal K plugin the Anchor page commands
  cfg_role_t roles[CFG_MAX_ROLES];
  uint8_t role_count;
} cfg_t;

typedef enum { CFG_FROM_MAIN, CFG_FROM_BACKUP, CFG_FROM_DEFAULTS } cfg_origin_t;
typedef struct { char key_path[64]; int line; char message[96]; } cfg_issue_t;   // line 0 = n/a
typedef struct { cfg_issue_t issues[8]; uint8_t count; } cfg_report_t;

// Loads dir/config.json, else dir/config.bak.json, else defaults. A file with no
// valid roles gets the default roles, so the display is never blank.
cfg_origin_t cfg_load(const char *dir, cfg_t *out, cfg_report_t *report);

// Writes config.tmp, fsyncs, renames config.json to config.bak.json, then
// config.tmp to config.json. Keys the firmware does not know are kept.
bool cfg_save(const char *dir, const cfg_t *cfg);

void cfg_defaults(cfg_t *out);

// Checks a power role's banks and flows. True when there are no banks and no flows, or the configuration is valid:
// known kinds; unique slug ids; exactly one "house"; every battery a valid, unique endpoint ref; "other" banks named;
// every flow a known kind on a device NAME, `to` (and a converter's `from`) naming a bank id, `from` only on a
// converter and never equal to `to`. On false, `why` says what is wrong.
bool cfg_power_check(const cfg_role_t *r, char *why, size_t why_n);

// True when the file's power role used the old format (links, or banks named by instance id): not read.
bool cfg_power_has_old_keys(const cfg_role_t *r);

// Edits dir/config.json at a key path ("sources.signalk.token"). An existing key keeps its
// type (string; a number must parse; a bool takes true/false); a missing key is created as a
// string. The result is checked by loading it into *out: on any issue for that path, returns
// false with *err filled and leaves the file and the cached tree untouched. On success saves
// (atomic) and returns true with the new config in *out. Runs on the config writer task.
bool cfg_set(const char *dir, const char *path, const char *text, cfg_t *out, cfg_report_t *err);

// The config last loaded or saved, as pretty JSON with the Wi-Fi password and Signal K token
// shown as "set (N chars)". Caller frees. NULL if nothing is loaded.
char *cfg_show(const char *dir);
