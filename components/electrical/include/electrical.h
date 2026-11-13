// The electrical resolver: turns per-device observations (from Signal K or straight from the bus) into
// classified endpoints. Knows nothing about any vendor, address or boat. Portable: libc and boatdata only.
// An endpoint is a (device, instance) pair, never an instance alone: instance numbers are unique per device.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EL_MAX_DEVICES 16
#define EL_MAX_ENDPOINTS 24
#define EL_FEEDS 2               // feed 0 is CAN, 1 is Signal K
#define EL_MERGE_WINDOW_MS 10000 // agreement (or disagreement, or better authority) must last this long to merge, unmerge or take over
#define EL_MERGE_VOLTS 0.05
#define EL_MERGE_AMPS 0.1
#define EL_MERGE_SOC 0.01
#define EL_STABLE_MS 10000       // a voltage-only endpoint must hold within EL_STABLE_V this long to count as a battery
#define EL_STABLE_V 0.5
#define EL_VOLTS_MIN 8.0         // plausible battery range for the voltage-only fallback
#define EL_VOLTS_MAX 60.0

// An adapter's own handle for a device within one feed: a CAN source address, or a hash of a Signal K $source.
typedef uint32_t el_ref_t;

typedef enum {
  EL_F_VOLTAGE, EL_F_CURRENT, EL_F_SOC, EL_F_TIME_REMAINING, EL_F_TEMPERATURE,
  EL_F_DC_TYPE, EL_F_CONV_STATE, EL_F_AC_CURRENT, EL_F_SWITCH, EL_F_POWER,
  EL_F_COUNT
} el_field_t;

typedef enum {
  EL_FN_UNKNOWN, EL_FN_BATTERY, EL_FN_PV, EL_FN_CHARGER, EL_FN_CONVERTER, EL_FN_ALTERNATOR, EL_FN_AC_IN, EL_FN_SWITCH_BANK
} el_fn_t;

// How an endpoint's function was decided; a higher value outranks a lower one.
typedef enum { EL_EV_NONE, EL_EV_VOLTAGE, EL_EV_PRODUCT, EL_EV_PGN, EL_EV_DC_TYPE, EL_EV_PATH } el_evidence_t;

enum { EL_DOM_DC, EL_DOM_AC, EL_DOM_SWITCH };

typedef struct {
  uint64_t can_name;          // 0 = unknown
  char model_id[32], serial[32];
} el_identity_t;

typedef struct {
  uint64_t key;               // stable identity: derived from the device key and the instance
  uint64_t devkey;
  uint8_t instance;
  uint8_t domain;             // EL_DOM_*: AC connections, switch channels and DC instances are separate number spaces
  el_fn_t fn;
  el_evidence_t ev;
  bool disagree;              // lower-ranked evidence pointed at a different function than the one chosen
  bool dc_conflict;           // two suppliers reported different DC types
  bool vseen;
  bool hidden;                // a mirror that lost to merged_into: never published
  uint64_t merged_into;
  uint64_t mcand;             // the endpoint this one has been agreeing with, and since when
  int64_t msince, unmerge_since, better_since;
  double vlo, vhi;            // voltage range since vsince, for the stability test
  int64_t vsince, vlast;
  bool has[EL_F_COUNT];
  double val[EL_F_COUNT];
  el_ref_t src[EL_F_COUNT];   // which device ref supplied each leaf
  uint8_t feed[EL_F_COUNT];   // and through which feed
  uint16_t dirty;             // leaves changed since the last el_publish (bit per el_field_t)
  bool pubd;                  // published to boatdata as pubfn under the id of pubkey
  el_fn_t pubfn;
  uint64_t pubkey;
  char publabel[16];          // the label last written (it changes when a neighbour appears)
  int64_t t[EL_F_COUNT];      // when
} el_endpoint_t;

typedef struct {
  bool used;
  uint8_t feed;
  el_ref_t ref;
  el_identity_t id;
  uint64_t key;               // can_name, else hash of model+serial, else provisional (top bit set)
  uint16_t emits;             // bit per el_field_t this device has ever reported
} el_device_t;

// Defined here only so callers can place it (static, off internal RAM); treat the fields as private.
typedef struct {
  el_device_t dev[EL_MAX_DEVICES];
  el_endpoint_t ep[EL_MAX_ENDPOINTS];
  size_t nep;
  bool feed_down[EL_FEEDS];
  struct { uint64_t key; el_fn_t fn; } gone[8];   // published endpoints merged away: their paths are invalidated by the next el_publish
  size_t ngone;
  uint32_t dropped;           // observations lost because a table was full
} el_t;

void el_init(el_t *e);

// Names a device. Fields merge: only the ones set in `id` replace what is known. Re-keys it and moves its endpoints; if its new key already has endpoints they are merged.
// (feed, ref) identifies the device within an adapter: feed 0 is CAN, 1 is Signal K.
void el_set_identity(el_t *e, uint8_t feed, el_ref_t ref, const el_identity_t *id);

// One reading. Non-finite values and readings older than the one held for that leaf are ignored.
void el_observe(el_t *e, uint8_t feed, el_ref_t ref, uint8_t instance, el_field_t f, double value, int64_t now_ms);

// A reading from a path that already names its function (an adopted signalk-venus style path): sets the function
// with evidence EL_EV_PATH, which nothing weaker overrides.
void el_observe_path(el_t *e, uint8_t feed, el_ref_t ref, uint8_t instance, el_fn_t fn, el_field_t f, double value, int64_t now_ms);

// The value this endpoint publishes for a leaf: its own, else one from a mirror merged into it (the supplier's ref
// says which). False when neither has it. A charger's output current never reaches a battery endpoint: chargers are
// never merged.
bool el_leaf(const el_t *e, const el_endpoint_t *p, el_field_t f, double *value, el_ref_t *src);

// Writes changed leaves of every visible battery, PV and charger endpoint into boatdata (source BD_SRC_ELECTRICAL) as
// electrical.resolved.{batteries,solar,chargers}.<id>.<leaf>, where <id> is el_endpoint_id, plus a text leaf `label`
// (el_endpoint_label) that roles use as the instance's name. Leaves that did not change
// are not rewritten, so a quiet value does not look fresh; an endpoint that became hidden or changed function has its
// old leaves invalidated. Call from the adapter's tick.
void el_publish(el_t *e, int64_t now_ms);

// The feed stopped (CAN offline, Signal K disconnected): its published leaves become invalid. New observations on
// that feed bring them back.
void el_feed_down(el_t *e, uint8_t feed);

size_t el_endpoint_count(const el_t *e);
const el_endpoint_t *el_endpoint(const el_t *e, size_t i);
uint32_t el_dropped(const el_t *e);

// A name a person can read, at most n-1 characters (15 with a 16-byte buffer): the device's short model name (the first
// word of its model id that has no digit, so "SmartShunt IP65 300A" is "SmartShunt"), else the function ("Battery",
// "Solar", "Charger"). Where two shown endpoints of one function would read the same it adds only what tells them
// apart: the tail of the device NAME in hex when the devices differ (the end of Signal K's can0.<canName>), the
// instance when one device has several. Returns the length.
size_t el_endpoint_label(const el_t *e, const el_endpoint_t *p, char *out, size_t n);

// Eight lowercase hex characters derived from the endpoint key: the id used in published paths and config pins.
void el_endpoint_id(const el_endpoint_t *p, char out[9]);

#ifdef __cplusplus
}
#endif
