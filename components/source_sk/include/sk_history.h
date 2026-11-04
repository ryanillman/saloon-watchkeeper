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

// What one sk_history_backfill call did.
typedef enum { SK_HIST_IDLE, SK_HIST_ASKED, SK_HIST_NO_ANSWER } sk_hist_result_t;

// Asks for 12 hours at one-minute resolution for ONE tracked path that has not been backfilled this
// connection (hist_unfilled), so the caller can keep serving the live stream between requests, and fills it
// in. IDLE: nothing to ask. ASKED: the server answered (whatever the body held; the path is done). NO_ANSWER:
// no response at all; the path is tried once more, so the caller should wait before calling again.
// base_url is "http://host:port".
sk_hist_result_t sk_history_backfill(const char *base_url, const char *token);
