// SocketCAN as an n2k_bus driver (Linux): the simulator reads a real or virtual bus with it.
// Non-blocking. Own frames are not looped back. Extended-ID data frames only.
#pragma once
#include <linux/can.h>
#include <stdbool.h>
#include "n2k.h"
#include "n2k_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct can_socketcan can_socketcan_t;

// NULL with errno set on failure (no such interface, no permission). The interface must already be up
// at 250 kbit/s (sudo ip link set can0 up type can bitrate 250000).
can_socketcan_t *can_socketcan_open(const char *ifname);
void can_socketcan_close(can_socketcan_t *c);

// recv: never blocks; false when nothing is waiting. A bus-off error frame makes state() report BUS_OFF
// until recover() (which n2k_bus calls every step while off) or a data frame clears it. send: false when the
// socket would block or errors. The interface itself is restarted from outside.
n2k_can_ops_t can_socketcan_ops(can_socketcan_t *c);

// false for standard-ID, remote and error frames.
bool can_socketcan_convert(const struct can_frame *cf, n2k_frame_t *out);

#ifdef __cplusplus
}
#endif
