#pragma once
// Signal K JSON → boatdata. Pure: no I/O.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "alerts.h"
#include "boatdata.h"

#define SK_SOURCE_ID BD_SRC_SK

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

// The vessel's own name and MMSI from the last full snapshot ("" when unknown). A snapshot without them keeps what
// was learned. Written by the Signal K task, read by the UI.
const char *sk_vessel_name(void);
const char *sk_vessel_mmsi(void);

// Called for every notification on this vessel (after the alerts update). `id` is the path below "notifications." (the
// server appends the notification's UUID to it: "mob.7c0e98fc-..."); `ref` is the value's own id (that UUID), NULL when
// it has none. active: state alarm or emergency. has_pos: a position in value.data.position (the press-time capture a
// display sends), else in value.position (the server's own, when it raised the notification itself), with numeric
// latitude and longitude. A null, a non-object or a state other than alarm/emergency calls it with active false. The
// Signal K task calls it. NULL removes it.
void sk_set_emergency_cb(void (*cb)(const char *id, bool active, bool has_pos, double lat, double lon, const char *ref));

// Sees every numeric or text value of a delta (not notifications, not the REST snapshot) with the update's own
// "$source" ("" when it has none): that is where a value's device is known, and the tree keeps only the latest
// value per path. `num` is NAN for text, `text` NULL for a number. While one is set the stream also subscribes to
// "electrical.*". NULL removes it. The Signal K task calls it.
typedef void (*sk_observe_cb)(const char *path, double num, const char *text, const char *source, int64_t now_ms, void *ctx);
void sk_set_observe_cb(sk_observe_cb cb, void *ctx);

// A bound path with its resend hint: 0 = on change ("instant"), else a fixed period in ms.
typedef struct { bd_id_t id; uint32_t resend_ms; } sk_sub_t;

// Values under "notifications." never reach boatdata: an object with a string state
// (other than "normal") goes to alerts_upsert; null, a non-object, or a missing or
// "normal" state goes to alerts_clear.

// {"context":"vessels.self","subscribe":[{"path":...,"policy":"instant"},...]}; caller frees.
// Always adds {"path":"notifications.*","policy":"instant"}; resend_ms > 0 uses "fixed" with "period".
char *sk_build_subscribe(const sk_sub_t *subs, size_t n);
// Cancels every subscription on the socket; send before resubscribing (the server does not dedupe).
const char *sk_unsubscribe_all(void);

// REST path (/signalk/v2/api/notifications/<ref>/acknowledge or /silence) for a queued request.
// False when the alert has no ref or req is AL_REQ_NONE.
bool sk_build_alert_request(const alert_t *a, alert_req_t req, char *path_out, size_t path_n);
