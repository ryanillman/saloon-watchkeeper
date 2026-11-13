// The NMEA 2000 bus as a boatdata source (rank 1, below Signal K). Decodes messages, names the Signal K
// path for each value (instance map, default ids) and writes it. The display side only: it uses boatdata.
// Default ids and paths follow what the Signal K N2K provider produces and are unverified.  // verify: n2k-signalk
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "boatdata.h"
#include "electrical.h"
#include "n2k_ident.h"
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
  bool create_paths;  // true (CAN only): create every path. false (SK + CAN): create a path only when it has no instance
                      // (depth, wind, navigation) or its instance is mapped in ids; otherwise write only to paths that exist
  const source_can_id_t *ids;
  size_t nids;
  el_t *resolver;     // NULL: battery values go to electrical.batteries.<id>.* as before. Set: battery, DC type, converter and
                      // AC values (and claims and product information) feed the resolver instead, which publishes
                      // electrical.resolved.*; the instance map ids do not apply to batteries then
} source_can_cfg_t;

#define SOURCE_CAN_PEERS 16
#define SOURCE_CAN_ASK_TRIES 5
#define SOURCE_CAN_ASK_BACKOFF_MS 5000   // doubles after each unanswered request
typedef struct {
  bool used, has_product;
  uint8_t addr, tries;
  int64_t next_ms;
} source_can_peer_t;

typedef struct {
  source_can_cfg_t cfg;
  int64_t last_msg_ms;   // last message of a PGN this decodes
  int64_t init_ms;       // when init ran: a bus silent from the start goes OFFLINE this long after
  bd_src_state_t state;
  uint32_t values;       // values written
  uint32_t unmapped;     // values dropped because their path does not exist (create_paths false)
  uint8_t cur_src;       // source address of the message being decoded
  source_can_peer_t peers[SOURCE_CAN_PEERS];
} source_can_t;

#define SOURCE_CAN_SILENCE_MS 5000

// Registers BD_SRC_N2K ("nmea2000", rank 1) and sets it CONNECTING. `cfg->ids` must outlive `s`.
void source_can_init(source_can_t *s, const source_can_cfg_t *cfg);

// An n2k_msg_cb: pass `s` as the context.
void source_can_on_msg(const n2k_msg_t *m, void *ctx);

// LIVE while decoded messages arrive; after SOURCE_CAN_SILENCE_MS of silence OFFLINE, once invalidating
// everything this source wrote. A bus that never delivers a message goes CONNECTING to OFFLINE the same way.
void source_can_tick(source_can_t *s);

// Sends one: a message to a node. False when it cannot go now (queue full, node not ready).
typedef bool (*source_can_send_fn)(uint32_t pgn, uint8_t dst, const uint8_t *data, uint16_t len, void *ctx);
// Asks each source we have heard, and that has not told us its product information, for PGN 126996 (ISO request),
// then again with doubling delays, SOURCE_CAN_ASK_TRIES times in all. A source that claims an address again is asked
// afresh. `send` NULL (the display is listen-only) does nothing. A refused send is retried on the next call.
void source_can_request_identity(source_can_t *s, int64_t now_ms, source_can_send_fn send, void *ctx);

// The Signal K path for a value (false when it has none, e.g. an unknown fluid type).
bool source_can_path(const n2k_value_t *v, const source_can_cfg_t *cfg, char *out, size_t n);

// Engine, battery and tank values belong to an instance whose id may collide with one Signal K names differently.
bool source_can_is_instance(const n2k_value_t *v);
// True when cfg->ids has an entry for the value's instance (so the bus may create its path in fallback mode).
bool source_can_is_mapped(const n2k_value_t *v, const source_can_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
