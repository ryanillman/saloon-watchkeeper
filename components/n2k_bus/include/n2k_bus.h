// The NMEA 2000 bus loop: receive, reassemble, feed the node, tick it, restart the
// claim after bus-off. Pure C; the CAN driver comes in through n2k_can_ops_t, so the
// loop is tested on the host with a fake driver. The ESP task wrapper is n2k_bus_task.h.
#pragma once
#include "n2k.h"
#include "n2k_node.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { N2K_CAN_OK, N2K_CAN_BUS_OFF } n2k_can_state_t;

typedef struct {
  bool (*recv)(n2k_frame_t *f, void *ctx);        // never blocks; false when nothing is waiting
  bool (*send)(const n2k_frame_t *f, void *ctx);  // never blocks; false when the bus refuses
  n2k_can_state_t (*state)(void *ctx);
  void (*recover)(void *ctx);                     // start bus-off recovery; may be called every step while off
  void *ctx;
} n2k_can_ops_t;

typedef struct {
  uint32_t rx_frames, tx_frames, tx_refused, bus_off_events;
} n2k_bus_stats_t;

// Fields are private; defined here so callers can place the struct.
typedef struct {
  n2k_can_ops_t can;
  n2k_node_t *node;  // NULL: listen-only, the driver's send is never called
  n2k_rx_t rx;
  uint32_t (*now_ms)(void *ctx);
  void *now_ctx;
  n2k_msg_cb app_cb;
  void *app_ctx;
  n2k_bus_stats_t stats;
  bool was_off;
} n2k_bus_t;

// `node`, when given, must have been created with ops.tx_frame = n2k_bus_tx_frame and
// ops.ctx = this bus. `app_cb` sees every received message (claims and requests too).
void n2k_bus_init(n2k_bus_t *b, const n2k_can_ops_t *can, n2k_node_t *node, n2k_fp_slot_t *slots,
                  size_t nslots, n2k_msg_cb app_cb, void *app_ctx, uint32_t (*now_ms)(void *ctx),
                  void *now_ctx);

// The node's tx_frame: sends through the driver and counts tx_frames / tx_refused.
bool n2k_bus_tx_frame(const n2k_frame_t *f, void *bus);

// One pass of the loop. While the driver reports bus-off nothing is received and
// recover() is called; when it returns to OK the node's claim restarts. Otherwise up
// to max_frames frames are received, then the node ticks.
void n2k_bus_step(n2k_bus_t *b, uint32_t max_frames);

const n2k_bus_stats_t *n2k_bus_stats(const n2k_bus_t *b);

#ifdef __cplusplus
}
#endif
