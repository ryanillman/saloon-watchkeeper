#pragma once
#include <stdbool.h>
#include <stdint.h>

#define ALERTS_MAX 16
typedef enum { AL_NORMAL, AL_ALERT, AL_WARN, AL_ALARM, AL_EMERGENCY } alert_state_t;   // same order as SK_STATE_*
typedef enum { AL_REQ_NONE, AL_REQ_ACK, AL_REQ_SILENCE } alert_req_t;
typedef struct {
  char id[64];            // notification path without "notifications.": "propulsion.port.temperature"
  alert_state_t state;
  char message[96];
  bool audible;           // method includes "sound"
  bool acked, silenced;
  char ref[40];           // source's notification id (Signal K UUID), "" if none
  bool send_failed;       // the last acknowledge or silence did not reach the source
  bool local;             // raised on this display, not by the source: survives the source going down, acknowledged locally only
  int64_t since_ms;
} alert_t;
typedef struct { alert_t a[ALERTS_MAX]; uint8_t count; bool source_up; } alerts_snapshot_t;   // sorted: most severe first, then newest

void alerts_init(void);
// Source fills id, state, message, audible, acked, silenced, ref; AL_NORMAL clears.
// Shown acked/silenced = local OR source. An escalation or a re-raise after a clear resets both;
// the same state repeated keeps the local flags. When the list is full the least severe entry
// is dropped for a more severe newcomer; an equal or lesser newcomer is not added.
void alerts_upsert(const alert_t *in, int64_t now_ms);
// A display-raised alert (the source cannot report its own loss). Same rules as alerts_upsert, but it survives
// alerts_source_up(false), has no ref, and an acknowledge or silence stays local: nothing is queued and
// send_failed is never set. Cleared by alerts_clear or an upsert with AL_NORMAL.
void alerts_upsert_local(const alert_t *in, int64_t now_ms);
void alerts_clear(const char *id);
bool alerts_source_is_up(void);                 // the flag alerts_source_up last set
void alerts_source_up(bool up);                 // false clears the list and the request queue
void alerts_snapshot(alerts_snapshot_t *out);
bool alerts_get(const char *id, alert_t *out);
alert_state_t alerts_for_path(const char *path);   // state of the alert whose id equals path; AL_NORMAL if none

// UI side: mark locally and queue for the source. Not queued (send_failed = true) when the
// source is down or the alert has no ref.
void alerts_request(const char *id, alert_req_t req);
// Source side: next queued request, if any. Queue holds 8; oldest dropped and marked send_failed on overflow.
bool alerts_next_request(char id[64], alert_req_t *req);
void alerts_request_result(const char *id, bool ok);

// Buzzer pattern, pure: true while the buzzer should sound at now_ms.
// alarm: 200 on / 800 off; emergency: 150 on / 150 off.
bool alerts_buzzer_on(const alerts_snapshot_t *s, bool enabled, int64_t now_ms);
