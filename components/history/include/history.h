#pragma once
// A minute-by-minute record of chosen paths, for the Glance tiles' trends. One float per minute,
// 12 hours deep per path; a minute with no fresh value is a gap (NAN). Thread-safe.
// Time is the monotonic clock (platform_millis), the same one boatdata stamps samples with.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "boatdata.h"

#define HIST_MINUTES 720
#define HIST_MAX_PATHS 16

void hist_init(void);                          // forgets everything and every tracked path
bool hist_track(bd_id_t id);                   // idempotent; false when HIST_MAX_PATHS are tracked
size_t hist_tracked(bd_id_t *out, size_t max); // the tracked ids, in tracking order

// Call often (the UI tick does). At each new minute stores every tracked path's value, or a gap when
// it is not a valid number received within the last 90 s. Minutes skipped between calls are gaps.
void hist_tick(int64_t now_ms);

// Past values, e.g. from the Signal K History API: vals[n-1] is the current minute, vals oldest
// first, NAN for no data. Only fills minutes that hold no value yet: a live sample always wins.
// Anything older than HIST_MINUTES is dropped.
void hist_backfill(bd_id_t id, const float *vals, size_t n);

// The last `minutes` minutes, oldest first, NAN for gaps. Returns how many were written: fewer than
// asked when less than that has been recorded (after boot), 0 for an untracked path. out has room for `minutes`.
size_t hist_read(bd_id_t id, size_t minutes, float *out);

// Newest valid value minus the oldest valid value in the last `minutes` minutes (that is minutes + 1
// slots: the current minute and `minutes` before it). False with fewer than two valid values.
bool hist_change(bd_id_t id, size_t minutes, float *delta);

// Backfill bookkeeping, so each connection asks the server for every tracked path once, including paths
// tracked after it connected. A new path starts unfilled.
size_t hist_unfilled(bd_id_t *out, size_t max);       // tracked paths still to be backfilled
void hist_note_backfill(bd_id_t id, bool answered);   // answered: done. Unanswered: tried again, and given up on the second try
void hist_forget_backfill(void);                      // a new connection: every path is to be asked again
