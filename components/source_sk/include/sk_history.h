#pragma once
// Past values from the Signal K History API (/signalk/v2/api/history/values), to fill the history
// store after a boot or a reconnect. Best effort: no provider, an empty reply, an error, or a body of
// the wrong shape is not a fault; the history simply keeps what it recorded itself.
#include <stddef.h>

// A reply {"values":[{"path":...},...],"data":[[timestamp, v0, v1, ...], ...]}: the series for the
// path at `path_index`, oldest first, null and non-numbers as NAN. When there are more than `max`
// rows the newest `max` are kept. Returns the row count, 0 for any other shape.
// Row timestamps are not read: the rows are taken as one per minute, the last one being now.
size_t sk_history_parse(const char *json, size_t len, size_t path_index, float *out, size_t max);

// Asks for 12 hours at one-minute resolution, one path at a time, for every path the history
// store tracks, and backfills each. Stops at the first request that gets no response at all.
// base_url is "http://host:port".
void sk_history_backfill(const char *base_url, const char *token);
