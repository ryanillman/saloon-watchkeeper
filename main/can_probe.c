#include "can_probe.h"
#include "sdkconfig.h"
#if CONFIG_BI_DIAG_CAN_PROBE
#include <string.h>
#include "board.h"
#include "can_twai.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "n2k_bus_task.h"

static const char *TAG = "can_probe";

#define PGN_ALERT 126983  // verify: canboat
#define ALERT_LEN 28
#define LOG_PER_SECOND 20

static n2k_bus_t bus;
static n2k_node_t node;
static n2k_qent_t queue[8];
static n2k_fp_slot_t slots[8];
static SemaphoreHandle_t mutex;

static uint32_t now_ms(void *ctx) {
  (void)ctx;
  return (uint32_t)(esp_timer_get_time() / 1000);
}
static void lock(void *ctx) {
  (void)ctx;
  xSemaphoreTake(mutex, portMAX_DELAY);
}
static void unlock(void *ctx) {
  (void)ctx;
  xSemaphoreGive(mutex);
}

// Runs on the bus task. Log at most LOG_PER_SECOND lines so a busy bus cannot flood the console.
static void on_msg(const n2k_msg_t *m, void *ctx) {
  (void)ctx;
  static uint32_t window_start, in_window;
  uint32_t t = now_ms(NULL);
  if (t - window_start >= 1000) {
    window_start = t;
    in_window = 0;
  }
  if (in_window++ >= LOG_PER_SECOND) return;
  ESP_LOGI(TAG, "rx pgn=%u src=%u len=%u b0=%02x", (unsigned)m->pgn, m->src, m->len, m->len ? m->data[0] : 0);
}

void can_probe_run(void) {
  const board_t *board = board_get();
  ESP_LOGI(TAG, "CAN probe on %s", board->id);
  ESP_ERROR_CHECK(board->init());

  int tx, rx;
  ESP_ERROR_CHECK(board_can_pins(&tx, &rx));
  ESP_ERROR_CHECK(board_can_enable());  // 7" boards: native USB is gone from here on
  ESP_ERROR_CHECK(can_twai_open(&(can_twai_cfg_t){.tx_gpio = tx, .rx_gpio = rx, .listen_only = false}));

  uint8_t mac[6];
  ESP_ERROR_CHECK(esp_efuse_mac_get_default(mac));
  n2k_node_config_t cfg = {
    .name = {.unique_number = (uint32_t)mac[3] << 16 | (uint32_t)mac[4] << 8 | mac[5],
             .manufacturer = 2046,  // verify: an unassigned/test value
             .function = 130, .device_class = 25, .industry_group = 4},
    .preferred_address = (uint8_t)(0x40 + (mac[5] & 0x3F)),
  };
  cfg.product.n2k_db_version = 2100;
  cfg.product.product_code = 1;
  strcpy(cfg.product.model_id, "bi-can-probe");
  strncpy(cfg.product.sw_version, esp_app_get_description()->version, sizeof cfg.product.sw_version - 1);

  mutex = xSemaphoreCreateMutex();
  n2k_node_ops_t nops = {n2k_bus_tx_frame, now_ms, lock, unlock, &bus};
  n2k_node_init(&node, &cfg, &nops, queue, 8);
  n2k_can_ops_t can = can_twai_ops();
  n2k_bus_init(&bus, &can, &node, slots, 8, on_msg, NULL, now_ms, NULL);
  n2k_node_start(&node);
  ESP_ERROR_CHECK(n2k_bus_task_start(&bus, 4096, 5));
  ESP_LOGI(TAG, "claiming address 0x%02x, name unique=%u", cfg.preferred_address, (unsigned)cfg.name.unique_number);

  uint8_t counter = 0;
  for (uint32_t sec = 0;; sec++) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (sec % 5 == 4) {
      uint8_t alert[ALERT_LEN];
      memset(alert, 0xA5, sizeof alert);
      alert[0] = counter++;
      n2k_send_t r = n2k_node_send(&node, PGN_ALERT, N2K_BROADCAST, 2, alert, sizeof alert);
      if (r != N2K_SEND_OK) ESP_LOGW(TAG, "send alert %u: result %d (node state %d)", alert[0], (int)r, (int)n2k_node_state(&node));
    }
    if (sec % 10 == 9) {
      const n2k_bus_stats_t *s = n2k_bus_stats(&bus);
      can_twai_stats_t d;
      can_twai_stats(&d);
      ESP_LOGI(TAG, "can: state=%d addr=0x%02x rx=%u tx=%u refused=%u drop=%u failed=%u bus_off=%u txerr=%u rxerr=%u node_dropped=%u",
               (int)n2k_node_state(&node), n2k_node_address(&node), (unsigned)s->rx_frames, (unsigned)s->tx_frames,
               (unsigned)s->tx_refused, (unsigned)d.rx_dropped, (unsigned)d.tx_failed, (unsigned)s->bus_off_events,
               d.tx_err_count, d.rx_err_count, (unsigned)n2k_node_dropped(&node));
      ESP_LOGI(TAG, "RAM: internal free %u, lowest %u, largest block %u; PSRAM free %u",
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
  }
}
#endif  // CONFIG_BI_DIAG_CAN_PROBE
