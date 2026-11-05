#pragma once
// Depth and tide, pure so they can be tested without a display. Signal K has three depths and they are not
// interchangeable (below the transducer, the keel or the surface), so the one shown is always named.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "boatdata.h"

#define DEPTH_MAX_AGE_MS 10000          // a sounder sends about once a second: silence is a fault, not a calm sea
#define TIDE_HEIGHT_MAX_AGE_MS (30 * 60 * 1000)   // tide plugins publish about once a minute

typedef enum { DEPTH_BELOW_TRANSDUCER, DEPTH_BELOW_KEEL, DEPTH_BELOW_SURFACE, DEPTH_NONE } depth_ref_t;

// Which of the three readings (index = depth_ref_t, any may be NULL) is shown: the first that is a valid number
// no older than DEPTH_MAX_AGE_MS, in the order transducer, keel, surface. DEPTH_NONE when there is none.
depth_ref_t depth_pick(const bd_sample_t *const s[3], int64_t now_ms);
const char *depth_ref_label(depth_ref_t r);   // "BELOW KEEL"; "" for DEPTH_NONE

// "2026-11-04T14:32:00.000Z" (the form Signal K uses) to epoch seconds. UTC only: a time without the Z is refused.
bool depth_parse_time(const char *iso, int64_t *epoch_s);
// "14:32" in local time (UTC plus the offset, as the clock does). `out` holds at least 6 bytes.
void depth_format_hhmm(int64_t epoch_s, int16_t utc_offset_min, char *out);

typedef enum { TIDE_UNKNOWN, TIDE_RISING, TIDE_FALLING } tide_dir_t;
typedef struct {
  tide_dir_t dir;
  bool has_next;     // a next high or low is known
  bool next_high;
  int64_t next_s;    // epoch seconds of it
} tide_t;

// The next high or low after `now_s` (epoch s, the server's clock; 0 = not known) decides the direction: heading
// for a high is rising. Either sample may be NULL. With no clock, no parseable time or only past times the answer
// is TIDE_UNKNOWN, never a guess.
tide_t tide_assess(const bd_sample_t *time_high, const bd_sample_t *time_low, int64_t now_s);

// The tide's change from its height at `now_s` at each of `n` times, `step_s` apart starting one step after now, as a
// cosine between the next high and next low (so they are half a tide apart, and the shape repeats). Heights are in
// metres; only their difference matters. False when the two events are not both in the future, or are not between
// 30 minutes and 9 hours apart, which no real tide is.
bool tide_deltas(double high_m, int64_t high_s, double low_m, int64_t low_s, int64_t now_s, int32_t step_s, float *out, size_t n);

// Local-time offset the Depth page and tile use for tide times (the app sets it from display.utc_offset_min).
void depth_set_utc_offset(int16_t minutes);
int16_t depth_utc_offset(void);
