#include "engine_state.h"
#include <math.h>
#include <string.h>

static bool fresh(const bd_sample_t *s, int64_t max_age, int64_t now) {
  return s && s->kind == BD_NUMBER && isfinite(s->num) && bd_is_fresh(s, max_age, now);
}

engine_status_t engine_assess(const bd_sample_t *rpm, const bd_sample_t *oil, const bd_sample_t *state, int64_t now) {
  engine_status_t r = {ENGINE_NO_DATA, false};
  bool oil_fresh = fresh(oil, ENGINE_FIELD_MAX_AGE_MS, now);
  bool oil_running = oil_fresh && oil->num > ENGINE_OIL_RUNNING_PA;

  if (state && state->valid && state->kind == BD_TEXT) {   // the server says so, when it can
    if (!strcmp(state->text, "unusable")) return r;
    if (!strcmp(state->text, "started")) r.mode = ENGINE_RUNNING;
    if (!strcmp(state->text, "stopped")) r.mode = oil_running ? ENGINE_RUNNING : ENGINE_OFF;
    if (r.mode != ENGINE_NO_DATA) return r;
  }

  if (fresh(rpm, ENGINE_RPM_MAX_AGE_MS, now)) {
    if (rpm->num > ENGINE_RUNNING_HZ) {
      r.mode = ENGINE_RUNNING;
    } else if (oil_running) {   // RPM reads 0 but oil pressure says otherwise: trust the oil
      r.mode = ENGINE_RUNNING;
      r.rpm_suspect = true;
    } else {
      r.mode = ENGINE_OFF;
    }
    return r;
  }

  // RPM silent or failed.
  if (oil_running) {
    r.mode = ENGINE_RUNNING;   // its RPM field flags by age on its own
  } else if (rpm && rpm->valid && rpm->kind == BD_NUMBER && isfinite(rpm->num) && rpm->num <= ENGINE_RUNNING_HZ) {
    r.mode = ENGINE_OFF;       // went quiet at 0 RPM: the key was turned off
  } else if (oil_fresh && !(rpm && rpm->valid)) {
    r.mode = ENGINE_OFF;       // no RPM at all, oil pressure present and low
  }
  return r;
}
