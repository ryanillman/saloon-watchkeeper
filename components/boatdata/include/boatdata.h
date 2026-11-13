#pragma once
// The boat's data, keyed by Signal K path (relative to vessels.self), in SI units.
// Thread-safe: every call takes one internal lock.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint16_t bd_id_t;
#define BD_NONE 0
#define BD_MAX_PATHS 512

typedef enum { BD_NUMBER, BD_POSITION, BD_TEXT } bd_kind_t;

// Source ids, as stored in bd_sample_t.source. Ids below BD_MAX_SOURCES; 0 is anonymous (tests).
#define BD_SRC_SK 1
#define BD_SRC_N2K 2
#define BD_SRC_ELECTRICAL 3   // the electrical resolver's classified endpoints (electrical.resolved.*)
#define BD_MAX_SOURCES 8

typedef struct {
  bd_kind_t kind;
  union {
    double num;
    struct { double lat, lon; } pos;
    char text[32];
  };
  int64_t rx_ms;     // monotonic time of the last real update
  bool valid;
  uint8_t source;    // id of the source that wrote it
  bool invalidated;  // made invalid by bd_invalidate_source (the connection went away), not a null the source sent; any bd_set_* clears it
  int64_t src_s;     // the source's own timestamp of the last update, epoch seconds; 0 when the update carried none
} bd_sample_t;

typedef struct { double lower, upper; uint8_t state; } bd_zone_t;   // state: alert_state_t value
typedef struct {
  char units[12];
  char display_name[24];
  bool has_scale;
  double scale_lo, scale_hi;
  bd_zone_t zones[6];
  uint8_t zone_count;
} bd_meta_t;

void bd_init(void);                       // clears everything
bd_id_t bd_intern(const char *path);      // creates if new; BD_NONE when full
bd_id_t bd_find(const char *path);        // BD_NONE if unknown
const char *bd_path(bd_id_t id);

void bd_set_number(bd_id_t id, double v, uint8_t source, int64_t now_ms);   // non-finite → invalid
void bd_set_position(bd_id_t id, double lat, double lon, uint8_t source, int64_t now_ms);
void bd_set_text(bd_id_t id, const char *s, uint8_t source, int64_t now_ms);
void bd_set_source_time(bd_id_t id, int64_t epoch_s);   // after a bd_set_*: the update's own timestamp (every bd_set_* resets it to 0)
int64_t bd_newest_source_time(void);   // the largest source timestamp set so far (epoch s; 0 none): the server's own clock as far as it has spoken
void bd_set_invalid(bd_id_t id);
void bd_invalidate_source(uint8_t source);

// ---- Several sources, one model ----
// A path holds one sample. Sources are ranked; a write from a lower-ranked source is dropped while the path holds a
// valid sample from a higher-ranked one that is younger than the hold window. Sources never registered rank 0 and
// all rank equal, which is last writer wins. Ranks and states are cleared by bd_init.
typedef enum { BD_SRC_NOT_CONFIGURED, BD_SRC_OFFLINE, BD_SRC_CONNECTING, BD_SRC_LIVE } bd_src_state_t;
#define BD_HOLD_MS_DEFAULT 30000   // slow Signal K paths (tank levels, SOC) can be quiet for a while
#define BD_CLOCK_STEP_S 30         // a source timestamp further ahead of the newest (plus elapsed time) waits for a second to agree
#define BD_REPLAY_S 10             // a source timestamp this far behind the newest one is a replay, not a reading

void bd_source_register(uint8_t source, const char *name, uint8_t rank);   // higher rank wins; marks it configured (OFFLINE)
void bd_source_set_state(uint8_t source, bd_src_state_t st);               // what the connection is doing, for roles and the strip
bd_src_state_t bd_source_state(uint8_t source);
const char *bd_source_name(uint8_t source);                                // "" when never registered
void bd_set_hold_ms(int32_t ms);                                           // arbitration hold window; <= 0 restores the default

// Whether a write would be kept. Call before bd_set_* when the source timestamp is known (src_s, epoch s, 0 none): a
// value Signal K replays on reconnect carries its old timestamp and must not displace a fresher value from another
// source. A write the model would drop anyway (lower rank, within the hold window) is refused by bd_set_* too.
bool bd_admit(bd_id_t id, uint8_t source, int64_t src_s, int64_t now_ms);

bool bd_get(bd_id_t id, bd_sample_t *out);   // false if id unknown; out->valid may be false
bool bd_is_fresh(const bd_sample_t *s, int64_t max_age_ms, int64_t now_ms);   // max_age_ms 0 = no limit

void bd_set_meta(bd_id_t id, const bd_meta_t *m);
bool bd_get_meta(bd_id_t id, bd_meta_t *out);   // false if none set

// Ids run 1..bd_count(); for walking every known path.
bd_id_t bd_count(void);

// '*' matches exactly one path segment.
size_t bd_match(const char *pattern, bd_id_t *out, size_t max);
