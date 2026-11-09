// An NMEA 2000 bus node: address claim, ISO Request replies, product information
// and a send queue. Pure C, libc and n2k only; the bus is reached through
// n2k_node_ops_t, so the whole state machine runs on the host.
// Byte layouts and timings are from memory.  // verify: ISO 11783-5 / canboat
#pragma once
#include "n2k.h"

#ifdef __cplusplus
extern "C" {
#endif

#define N2K_NODE_CLAIM_WAIT_MS 250
#define N2K_NODE_ADDR_NONE 254

typedef struct {
  uint32_t unique_number;  // 21 bits
  uint16_t manufacturer;   // 11 bits
  uint8_t device_instance, function, device_class, system_instance, industry_group;
} n2k_name_fields_t;

// ISO NAME: unique 0-20, manufacturer 21-31, device instance 32-39, function 40-47,
// reserved 48, class 49-55, system instance 56-59, industry group 60-62,
// arbitrary-address-capable 63 (always set).
uint64_t n2k_name_pack(const n2k_name_fields_t *f);

typedef struct {
  uint16_t n2k_db_version, product_code;
  char model_id[32], sw_version[32], model_version[32], serial[32];
  uint8_t certification, load_equivalency;
} n2k_product_t;

typedef enum { N2K_NODE_IDLE, N2K_NODE_CLAIMING, N2K_NODE_READY, N2K_NODE_CANNOT_CLAIM } n2k_node_state_t;

typedef struct {
  bool (*tx_frame)(const n2k_frame_t *f, void *ctx);  // false when the bus cannot take it now
  uint32_t (*now_ms)(void *ctx);
  void (*lock)(void *ctx);    // optional: guards the send queue against other tasks
  void (*unlock)(void *ctx);  // optional
  void *ctx;
} n2k_node_ops_t;

typedef struct {
  n2k_name_fields_t name;
  uint8_t preferred_address;
  n2k_product_t product;
} n2k_node_config_t;

// One queued outgoing message. The caller supplies the array.
typedef struct {
  uint32_t pgn;
  uint8_t dst, prio;
  uint16_t len;
  uint8_t data[N2K_FP_MAX];
} n2k_qent_t;

// Defined here only so callers can place it; treat the fields as private.
// Threading: n2k_node_send may be called from any task (it takes the lock). Everything
// else (on_msg, tick, start, bus_reset) belongs to the single bus task, except that
// bus_reset also locks so another task may request it.
typedef struct {
  n2k_node_config_t cfg;
  n2k_node_ops_t ops;
  uint64_t name;
  n2k_node_state_t state;
  uint8_t address;
  bool claim_pending;      // a claim still has to be sent
  bool claim_timed;        // the pending claim restarts the wait when it goes out
  uint32_t claim_sent_ms;
  uint8_t seen[32];        // bitmap of addresses seen in claims
  n2k_qent_t *queue;
  size_t qcount, qhead, qlen;
  n2k_tx_t tx;
  bool tx_active;
  uint8_t stall;
  n2k_seq_t seq;
  uint32_t dropped;
} n2k_node_t;

void n2k_node_init(n2k_node_t *n, const n2k_node_config_t *cfg, const n2k_node_ops_t *ops,
                   n2k_qent_t *queue, size_t qcount);
void n2k_node_start(n2k_node_t *n);
void n2k_node_on_msg(n2k_node_t *n, const n2k_msg_t *m);
void n2k_node_tick(n2k_node_t *n);
n2k_node_state_t n2k_node_state(const n2k_node_t *n);
uint8_t n2k_node_address(const n2k_node_t *n);  // 254 when CANNOT_CLAIM
// Drops the queue and restarts the claim: the bus may have changed while it was down.
void n2k_node_bus_reset(n2k_node_t *n);

typedef enum { N2K_SEND_OK = 0, N2K_SEND_NOT_READY, N2K_SEND_QUEUE_FULL, N2K_SEND_BAD_LENGTH } n2k_send_t;

// Queues one message; never blocks and never touches the bus (n2k_node_tick sends).
// BAD_LENGTH: len 0, over N2K_FP_MAX, or over 8 on a PGN that is not fast-packet.
// NOT_READY: the node is not READY (still claiming, lost its address, or never started).
n2k_send_t n2k_node_send(n2k_node_t *n, uint32_t pgn, uint8_t dst, uint8_t prio,
                         const uint8_t *data, uint16_t len);

// Messages dropped by n2k_node_bus_reset, an address loss mid-send, or 5 ticks of bus refusal.
uint32_t n2k_node_dropped(const n2k_node_t *n);

#ifdef __cplusplus
}
#endif
