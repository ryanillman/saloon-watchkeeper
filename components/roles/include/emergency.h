#pragma once
// A declared emergency, pure so it can be tested without a display. One state for the whole device: what is
// declared (by a press here or by a Signal K notification), where the boat was when it was pressed, and whether the
// notification reached Signal K. Thread-safe: notifications arrive on the Signal K task, presses and drawing on the
// UI task.
//
// Rules (see docs/specs/2026-11-06-emergency-design.md):
//  - A MOB position is the pressing device's own capture or the notification's payload. Never the receiving device's
//    position at receipt: a late arrow in the wrong place is worse than none.
//  - The echo of our own notification never overwrites the local capture.
//  - A send that did not get through stays NOT SENT; nothing is retried and nothing is cleared silently.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { EM_NONE, EM_MOB, EM_FIRE, EM_FLOOD, EM_SINKING, EM_OTHER } em_nature_t;
typedef enum { EM_SEND_NONE, EM_SENDING, EM_SENT, EM_NOT_SENT } em_send_t;
typedef enum { EM_ORIGIN_NONE, EM_ORIGIN_LOCAL, EM_ORIGIN_NOTIFICATION } em_origin_t;

typedef struct {
  em_nature_t nature;
  em_origin_t origin;
  em_send_t send, waypoint_send;   // the notification, and the Signal K waypoint (MOB only, best effort)
  bool has_pos;
  double lat, lon;                 // degrees; valid when has_pos
  int64_t captured_ms;             // platform_millis() at the press or the receipt
  int64_t captured_epoch_s;        // UTC seconds at the press, 0 when the clock was not known
  char id[40];                     // the server's id (a UUID) for this notification: the key to clear it. "" until known
} em_state_t;

void em_reset(void);
void em_snapshot(em_state_t *out);
bool em_active(void);

// This device's button: captures the position and time, origin LOCAL, send SENDING.
void em_press(em_nature_t n, bool has_pos, double lat, double lon, int64_t now_ms, int64_t epoch_s);
void em_set_send(em_send_t s);
void em_set_waypoint_send(em_send_t s);
// The id the server's reply to the raise carried. Authoritative: a later echo does not replace it.
void em_set_server_id(const char *id);
// A Signal K notification for `n` (active: state alarm or emergency); `id` is its value.id (NULL or "" when it has none).
// Normal clears the same nature, unless it names a different notification than the one we hold. A different nature
// never displaces a MOB.
void em_on_notification(em_nature_t n, bool active, bool has_pos, double lat, double lon, const char *id, int64_t now_ms);
// The cancel reached Signal K: forget the emergency.
void em_cancel_sent(void);

// Notification id (the path after "notifications.") to a nature: "mob", or "mob.<anything>" (the server appends the
// notification's UUID); EM_NONE when it is not one of ours.
em_nature_t em_nature_from_path(const char *id);
const char *em_nature_path(em_nature_t n);    // "mob", "fire", "flooding", "sinking", "emergency" (OTHER: provisional)
const char *em_nature_label(em_nature_t n);   // "MAN OVERBOARD", "FIRE", ...

// The body of POST /signalk/v2/api/notifications (the server's Notifications API): state and message, path with idInPath
// (the notification lands at notifications.<path>.<uuid>, and the reply carries the uuid). A MOB with a position carries
// data.position and data.time. Returns the length, 0 when `n` is too small.
size_t em_raise_body(const em_state_t *s, char *out, size_t n);
// The body of PUT /signalk/v2/api/notifications/<uuid>: {"state":"normal"}.
size_t em_cancel_body(char *out, size_t n);
// The Signal K waypoint (GeoJSON Point, longitude first), named "MOB hh:mm:ss" (UTC). 0 with no position.
size_t em_waypoint_body(const em_state_t *s, char *out, size_t n);

// 41°32.417'N: degrees (2 digits for latitude, 3 for longitude), minutes to the thousandth, rounded so minutes never
// print as 60.000. A value that rounds to zero is N or E.
void em_format_ddm(double deg, bool is_lat, char *out, size_t n);

// Bearing (radians true, 0..2pi) and distance from the boat to the MOB. False unless the state is a MOB with a
// position and the boat's position is finite.
bool em_target(const em_state_t *s, double own_lat, double own_lon, double *bearing_rad_true, double *dist_m);
