#pragma once
// /config.json on the SD card: the device's source of truth.
#include <stdbool.h>
#include <stdint.h>

#define CFG_VERSION 1
#define CFG_MAX_ROLES 8

typedef struct { char ssid[33]; char password[65]; } cfg_wifi_t;
typedef struct {
  char host[64]; uint16_t port; char token[256];
  char request[128];   // href of a pending access request, resumed after a restart; "" = none
} cfg_sk_t;   // host "auto" = mDNS
typedef struct { char temperature[2], pressure[4], volume[4], speed[5], distance[3]; } cfg_units_t;
typedef struct { char id[24]; char label[12]; } cfg_instance_t;
typedef struct { char slot[48]; char path[96]; } cfg_bind_t;          // slot "<instance>.<slot>"
typedef struct { char slot[24]; bool has_min, has_max; double min, max; } cfg_override_t;
typedef struct {
  char role[12];
  uint8_t src_entry;  // 1-based entry in the file's roles array this came from; 0 = new
  char primary[24];   // power: instance id of the primary battery bank; empty = automatic
  cfg_instance_t inst[8]; uint8_t inst_count;
  cfg_bind_t bind[8]; uint8_t bind_count;
  cfg_override_t ovr[8]; uint8_t ovr_count;
  char exclude[8][48]; uint8_t exclude_count;
} cfg_role_t;
typedef struct {
  cfg_wifi_t wifi;
  cfg_sk_t sk;
  cfg_units_t units;
  char palette[6];          // "auto" | "day" | "dusk" | "night"
  uint8_t brightness;       // 0..100
  char start_role[12];
  bool buzzer;
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

// Edits dir/config.json at a key path ("sources.signalk.token"). An existing key keeps its
// type (string; a number must parse; a bool takes true/false); a missing key is created as a
// string. The result is checked by loading it into *out: on any issue for that path, returns
// false with *err filled and leaves the file and the cached tree untouched. On success saves
// (atomic) and returns true with the new config in *out. Runs on the config writer task.
bool cfg_set(const char *dir, const char *path, const char *text, cfg_t *out, cfg_report_t *err);

// The config last loaded or saved, as pretty JSON with the Wi-Fi password and Signal K token
// shown as "set (N chars)". Caller frees. NULL if nothing is loaded.
char *cfg_show(const char *dir);
