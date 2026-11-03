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
void bd_set_invalid(bd_id_t id);
void bd_invalidate_source(uint8_t source);

bool bd_get(bd_id_t id, bd_sample_t *out);   // false if id unknown; out->valid may be false
bool bd_is_fresh(const bd_sample_t *s, int64_t max_age_ms, int64_t now_ms);   // max_age_ms 0 = no limit

void bd_set_meta(bd_id_t id, const bd_meta_t *m);
bool bd_get_meta(bd_id_t id, bd_meta_t *out);   // false if none set

// Ids run 1..bd_count(); for walking every known path.
bd_id_t bd_count(void);

// '*' matches exactly one path segment.
size_t bd_match(const char *pattern, bd_id_t *out, size_t max);
