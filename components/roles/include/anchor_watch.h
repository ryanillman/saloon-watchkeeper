#pragma once
// The display's own check on the anchor watch, for the failures the server cannot report about itself.
//  - Boot restore: the SD record says an anchor was down. If the server then shows none (or nothing fresh arrives
//    within 120 s of boot), raise ANCHOR WATCH NOT RESTORED. Silenced by ACK; the record is kept until the user
//    raises or drops (anchor_watch_user_cleared).
//  - A live server saying "no anchor" is believed only after ANCHOR_NONE_CONFIRM_MS: a Signal K server shutting down
//    makes its anchor plugin publish nulls just before the connection closes, which must not read as a raise.
//  - Watch lost: an anchor was set and its data has been stale for lost_after_ms: ANCHOR WATCH LOST.
// Both are local alerts (alerts_upsert_local), so they outlive the Signal K connection. The record is written only on
// a change seen on live, fresh data. Pure apart from the SD record file; UI task only.
#include <stdbool.h>
#include <stdint.h>
#include "anchor_record.h"
#include "anchor_state.h"

#define ANCHOR_WATCH_ALERT_ID "navigation.anchor.watch"   // routes to the Anchor role (see app_alert_role)
#define ANCHOR_RESTORE_WAIT_MS 120000
#define ANCHOR_NONE_CONFIRM_MS 15000   // a live server's "no anchor" must last this long before the watch believes the anchor was raised

typedef struct {
  anchor_record_t rec;        // what is on the card (or would be)
  bool have_sd;               // a card to read and write; false after a failed write too (the page shows NO SD)
  bool restoring;             // the record says armed and the server has not shown the anchor back yet
  bool was_set;               // live, fresh data showed an anchor in this session
  int8_t alert;               // 0 none up, 1 WATCH LOST, 2 NOT RESTORED
  int64_t boot_ms, stale_since_ms;   // stale_since_ms -1: data is fresh
  int64_t none_since_ms;             // when the live server began saying there is no anchor; -1 while it does not
  int32_t lost_after_ms;
  uint32_t writes;            // record writes so far
} anchor_watch_t;

// Replaces how the record is written (default: anchor_record_save on the calling task). The app queues it to its
// writer task so the card is never touched on the UI task. NULL restores the default.
void anchor_watch_set_saver(bool (*saver)(const char *dir, const anchor_record_t *r));

// dir: the config directory ("" or NULL: no card, so no record and no boot check). Reads the record.
void anchor_watch_init(anchor_watch_t *w, const char *dir, int64_t boot_ms, int32_t lost_after_ms);

// Whether the boot check is still waiting for the server to show the anchor: pass this to anchor_view_build.
bool anchor_watch_restoring(const anchor_watch_t *w);

// Call on the UI tick with the view just built. `dir` is where the record goes (NULL: no card).
void anchor_watch_tick(anchor_watch_t *w, const anchor_view_t *v, bool source_live, int64_t now_ms, const char *dir);

// The user raised the anchor (hold) or is dropping a new one: clears the record, the alert and the restore wait.
void anchor_watch_user_cleared(anchor_watch_t *w, const char *dir);
