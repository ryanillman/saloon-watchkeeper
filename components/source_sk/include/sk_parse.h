#pragma once
// Signal K JSON → boatdata. Pure: no I/O.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "alerts.h"
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

// Limits sk_parse_full to leaves under these top-level groups (NULL-terminated;
// NULL = everything). A large boat's tree can hold thousands of paths; only the
// ones the configured roles read are kept. The array must outlive its use.
void sk_parse_set_discovery_prefixes(const char *const *prefixes);

// A bound path with its resend hint: 0 = on change ("instant"), else a fixed period in ms.
typedef struct { bd_id_t id; uint32_t resend_ms; } sk_sub_t;

// Values under "notifications." never reach boatdata: an object with a string state
// (other than "normal") goes to alerts_upsert; null, a non-object, or a missing or
// "normal" state goes to alerts_clear.

// {"context":"vessels.self","subscribe":[{"path":...,"policy":"instant"},...]}; caller frees.
// Always adds {"path":"notifications.*","policy":"instant"}; resend_ms > 0 uses "fixed" with "period".
char *sk_build_subscribe(const sk_sub_t *subs, size_t n);

// REST path (/signalk/v2/api/notifications/<ref>/acknowledge or /silence) for a queued request.
// False when the alert has no ref or req is AL_REQ_NONE.
bool sk_build_alert_request(const alert_t *a, alert_req_t req, char *path_out, size_t path_n);
