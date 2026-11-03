#pragma once
// Signal K JSON → boatdata. Pure: no I/O.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "boatdata.h"

#define SK_SOURCE_ID 1

// Zone and notification states, as stored in bd_zone_t.state.
enum { SK_STATE_NORMAL, SK_STATE_ALERT, SK_STATE_WARN, SK_STATE_ALARM, SK_STATE_EMERGENCY };

void sk_parse_reset(void);   // forgets the self context

// A stream message: the server hello (learns our vessel's context) or a delta.
// Deltas for other vessels are ignored. null, objects other than a position,
// and booleans mark the path invalid; they are never stored as 0.
void sk_parse_message(const char *json, size_t len, int64_t now_ms);

// The REST body of /signalk/v1/api/vessels/self: interns every leaf path and
// stores its meta. Values are left to the stream.
bool sk_parse_full(const char *json, size_t len);   // false if not a JSON object

// {"context":"vessels.self","subscribe":[{"path":...,"policy":"instant"},...]}; caller frees.
char *sk_build_subscribe(const bd_id_t *ids, size_t n);
