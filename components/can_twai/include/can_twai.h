// ESP32 TWAI driver for n2k_bus (ESP-IDF 5.5 esp_driver_twai node API), 250 kbit/s.
// One node per image. RX frames are queued from the ISR; a full queue drops and counts.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "n2k_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int tx_gpio, rx_gpio;
  bool listen_only;       // no ACKs and no transmissions: a silent tap
  uint8_t rx_queue_len;   // frames buffered between the ISR and the bus task (0 = 32)
  uint8_t tx_queue_depth; // frames the driver may hold (0 = 4)
} can_twai_cfg_t;

esp_err_t can_twai_open(const can_twai_cfg_t *cfg);
void can_twai_close(void);

// The driver as n2k_bus wants it.
n2k_can_ops_t can_twai_ops(void);

typedef struct {
  uint32_t rx_dropped;  // frames lost because the RX queue was full
  uint32_t tx_failed;   // frames the controller gave up on (no ACK after the retries)
  uint32_t bus_errors;
  uint16_t tx_err_count, rx_err_count;
} can_twai_stats_t;

void can_twai_stats(can_twai_stats_t *out);

#ifdef __cplusplus
}
#endif
