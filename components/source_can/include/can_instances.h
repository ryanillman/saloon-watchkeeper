// The instance cache: what Signal K knew about this boat's engines, batteries and tanks, kept on the SD card so
// that after a boot with Signal K down the bus has paths to fill in and the roles find their instances.
//
// It stores full paths, restricted to the leaves the bus can write (propulsion.<id>.revolutions,
// electrical.batteries.<id>.voltage, tanks.<fluid>.<id>.currentLevel, ...) that Signal K has delivered a valid
// value for. Restoring them as empty placeholders therefore reproduces exactly what Signal K had, no more.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CI_MAX_PATHS 64
#define CI_PATH_LEN 64
#define CI_SCAN_MS 5000     // how often live data is compared with the cache
#define CI_SETTLE_MS 60000  // Signal K must have been live this long before a vanished path is forgotten
#define CI_FORGET_SCANS 6   // ...and the path must then be missing from this many scans in a row (a flapping sender is not forgotten)

typedef struct {
  char path[CI_MAX_PATHS][CI_PATH_LEN];
  uint16_t count;
  uint8_t missed[CI_MAX_PATHS];   // consecutive settled scans each path was absent from (cache only)
} can_instances_t;

// The cache paths Signal K has delivered a valid value for right now. Placeholders and values written by the bus
// do not count.
void can_instances_scan(can_instances_t *out);

// Brings `cache` towards `live`: paths in live are added; when `settled`, paths absent from CI_FORGET_SCANS scans in a
// row are removed. True when the cache changed.
bool can_instances_update(can_instances_t *cache, const can_instances_t *live, bool settled);

// dir/instances.json, else dir/instances.bak.json. False when neither holds a cache.
bool can_instances_load(const char *dir, can_instances_t *out);
// Writes dir/instances.tmp, then keeps the old file as instances.bak.json and moves the new one in (the way cfg_save
// and anchor_record_save do), so a power cut leaves the old cache or the new one.
bool can_instances_save(const char *dir, const can_instances_t *in);

// Creates an empty (invalid) path in boatdata for each entry. Entries that are not a path the bus writes are
// skipped. Returns how many were created or already there.
size_t can_instances_restore(const can_instances_t *in);

typedef struct {
  can_instances_t cache;
  int64_t live_since_ms;  // when Signal K last became live; -1 while it is not
  int64_t last_scan_ms;   // -1 before the first scan
} can_instances_state_t;

// Replaces how can_instances_tick writes the cache (default: can_instances_save on the calling task). The app queues it
// to its writer task so the card is never touched on the UI task. NULL restores the default.
void can_instances_set_saver(bool (*saver)(const char *dir, const can_instances_t *in));

// Loads the cache from dir (NULL: none) and restores it. Returns how many paths were restored.
size_t can_instances_init(can_instances_state_t *s, const char *dir);

// Call from the UI task about every 100 ms. While Signal K is live it rescans every CI_SCAN_MS, updates the cache and
// saves it when it changed. True when it wrote the file.
bool can_instances_tick(can_instances_state_t *s, bool signalk_live, int64_t now_ms, const char *dir);
