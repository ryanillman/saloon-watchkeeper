#pragma once
// Is the engine running, off, or are we blind? Oil pressure is the second witness:
// it collapses within seconds of a stop and is independent of the RPM pickup.
#include <stdbool.h>
#include "boatdata.h"

#define ENGINE_RPM_MAX_AGE_MS 3000
#define ENGINE_FIELD_MAX_AGE_MS 5000
#define ENGINE_RUNNING_HZ 0.5          // 30 RPM
#define ENGINE_OIL_RUNNING_PA 50000.0  // 0.5 bar

typedef enum { ENGINE_NO_DATA, ENGINE_OFF, ENGINE_RUNNING } engine_mode_t;

typedef struct {
  engine_mode_t mode;
  bool rpm_suspect;   // running by oil pressure while RPM reads 0: the RPM sender is wrong
} engine_status_t;

// Any sample may be NULL (path not bound). state is propulsion.<id>.state when a
// server provides it ("stopped" / "started" / "unusable").
engine_status_t engine_assess(const bd_sample_t *rpm, const bd_sample_t *oil, const bd_sample_t *state, int64_t now_ms);
