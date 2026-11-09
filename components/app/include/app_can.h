// The NMEA 2000 bus as the app uses it: started from config.json's sources.nmea2000, listen-only (no node, never
// transmits). Portable: the simulator and the firmware pass their own CAN driver as n2k_can_ops_t.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "boatdata.h"
#include "config.h"
#include "n2k_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts the source from cfg->n2k (ids and mode). False when it is already started. The driver must stay valid
// until app_can_stop.
bool app_can_start(const cfg_t *cfg, n2k_can_ops_t ops);
void app_can_stop(void);   // forgets the source (tests, and a later config change); does not close the driver

// One pass: receive up to 256 frames into boatdata, then let the source notice silence. Call it from exactly one
// task, often enough that the driver's receive queue does not fill (the firmware's CAN task does ~every 10 ms; the
// simulator once per loop). A no-op before app_can_start.
void app_can_poll(void);

typedef struct {
  bool started;
  bd_src_state_t state;
  uint32_t rx_frames, values, unmapped, bus_off_events;
} app_can_status_t;
void app_can_status(app_can_status_t *out);

// False only when the bus is enabled in "only" mode: that mode has no Signal K client.
bool app_network_wanted(const cfg_t *cfg);

#ifdef __cplusplus
}
#endif
