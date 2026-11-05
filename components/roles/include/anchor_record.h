#pragma once
// The anchor watch record on the SD card: what the display last saw of the anchor, so after a power cut it can tell
// that an anchor was down. Written only when the display sees a change on live data (see anchor_watch.h).
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  bool armed;            // an anchor was set
  double limit_m;        // the alarm radius when last seen; 0 unknown
  int64_t set_epoch_s;   // when it was set, if known; 0 unknown (there is no wall clock at boot)
} anchor_record_t;

// Reads dir/anchor.json, else dir/anchor.bak.json. False when neither exists or holds a record.
bool anchor_record_load(const char *dir, anchor_record_t *out);

// Writes dir/anchor.tmp, then moves anchor.json to anchor.bak.json and anchor.tmp to anchor.json (the way cfg_save
// does it), so a power cut leaves the old record or the new one. False when it could not be written.
bool anchor_record_save(const char *dir, const anchor_record_t *r);
