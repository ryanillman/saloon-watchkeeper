// The NMEA 2000 bus as a boatdata source (rank 1, below Signal K). Decodes messages, names the Signal K
// path for each value (instance map, default ids) and writes it. The display side only: it uses boatdata.
// Default ids and paths follow what the Signal K N2K provider produces and are unverified.  // verify: n2k-signalk
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "boatdata.h"
#include "n2k.h"
#include "n2k_decode.h"

#ifdef __cplusplus
extern "C" {
#endif

// Maps a bus instance to the id the Signal K server uses. Keys: "engine.<instance>", "battery.<instance>",
// "tank.<fluid name>.<instance>" (e.g. "tank.fuel.0"). Unmapped instances use the decimal instance.
typedef struct {
  const char *key;
  const char *id;
} source_can_id_t;

typedef struct {
  bool create_paths;  // true (CAN only): create paths; false (SK + CAN): write only to paths that already exist
  const source_can_id_t *ids;
  size_t nids;
} source_can_cfg_t;

typedef struct {
  source_can_cfg_t cfg;
  int64_t last_msg_ms;   // last message of a PGN this decodes
  bd_src_state_t state;
  uint32_t values;       // values written
  uint32_t unmapped;     // values dropped because their path does not exist (create_paths false)
} source_can_t;

#define SOURCE_CAN_SILENCE_MS 5000

// Registers BD_SRC_N2K ("nmea2000", rank 1) and sets it CONNECTING. `cfg->ids` must outlive `s`.
void source_can_init(source_can_t *s, const source_can_cfg_t *cfg);

// An n2k_msg_cb: pass `s` as the context.
void source_can_on_msg(const n2k_msg_t *m, void *ctx);

// LIVE while decoded messages arrive; after SOURCE_CAN_SILENCE_MS of silence OFFLINE, once invalidating
// everything this source wrote.
void source_can_tick(source_can_t *s);

// The Signal K path for a value (false when it has none, e.g. an unknown fluid type).
bool source_can_path(const n2k_value_t *v, const source_can_cfg_t *cfg, char *out, size_t n);

#ifdef __cplusplus
}
#endif
