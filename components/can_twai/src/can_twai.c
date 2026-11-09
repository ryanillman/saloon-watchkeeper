#include "can_twai.h"
#include <string.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "can_twai";

#define DEFAULT_RX_QUEUE 32
#define DEFAULT_TX_DEPTH 4
#define MAX_TX_DEPTH 8

// The driver keeps a pointer to a frame until its tx-done event, so frames and their data
// live in this pool, not on the caller's stack.
typedef struct {
  twai_frame_t frame;
  uint8_t data[8];
  volatile bool busy;
} tx_slot_t;

static twai_node_handle_t node;
static QueueHandle_t rx_q;
static tx_slot_t pool[MAX_TX_DEPTH + 1];
static size_t pool_n;
static portMUX_TYPE pool_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile twai_error_state_t node_state = TWAI_ERROR_ACTIVE;
static volatile uint32_t rx_dropped, tx_failed, bus_errors;

static bool IRAM_ATTR on_rx_done(twai_node_handle_t h, const twai_rx_done_event_data_t *e, void *ctx) {
  (void)e;
  (void)ctx;
  uint8_t buf[8];
  twai_frame_t fr = {.buffer = buf, .buffer_len = sizeof buf};
  if (twai_node_receive_from_isr(h, &fr) != ESP_OK) return false;
  if (!fr.header.ide || fr.header.rtr) return false;  // NMEA 2000 is extended data frames only
  n2k_frame_t f;
  f.id = fr.header.id & 0x1FFFFFFF;
  f.len = fr.header.dlc > 8 ? 8 : (uint8_t)fr.header.dlc;
  memcpy(f.data, buf, f.len);
  BaseType_t woke = pdFALSE;
  if (xQueueSendFromISR(rx_q, &f, &woke) != pdTRUE) rx_dropped++;  // never block in the ISR
  return woke == pdTRUE;
}

static bool IRAM_ATTR on_tx_done(twai_node_handle_t h, const twai_tx_done_event_data_t *e, void *ctx) {
  (void)h;
  (void)ctx;
  const tx_slot_t *s = (const tx_slot_t *)e->done_tx_frame;  // frame is the first member
  if (s >= pool && s < pool + pool_n) ((tx_slot_t *)s)->busy = false;
  if (!e->is_tx_success) tx_failed++;
  return false;
}

static bool IRAM_ATTR on_state_change(twai_node_handle_t h, const twai_state_change_event_data_t *e, void *ctx) {
  (void)h;
  (void)ctx;
  node_state = e->new_sta;
  return false;
}

static bool IRAM_ATTR on_error(twai_node_handle_t h, const twai_error_event_data_t *e, void *ctx) {
  (void)h;
  (void)e;
  (void)ctx;
  bus_errors++;
  return false;
}

esp_err_t can_twai_open(const can_twai_cfg_t *cfg) {
  if (node) return ESP_ERR_INVALID_STATE;
  uint8_t rxlen = cfg->rx_queue_len ? cfg->rx_queue_len : DEFAULT_RX_QUEUE;
  uint8_t depth = cfg->tx_queue_depth ? cfg->tx_queue_depth : DEFAULT_TX_DEPTH;
  if (depth > MAX_TX_DEPTH) depth = MAX_TX_DEPTH;
  rx_q = xQueueCreate(rxlen, sizeof(n2k_frame_t));
  if (!rx_q) return ESP_ERR_NO_MEM;
  memset(pool, 0, sizeof pool);
  pool_n = (size_t)depth + 1;  // one in the controller plus the driver's queue
  rx_dropped = tx_failed = bus_errors = 0;
  node_state = TWAI_ERROR_ACTIVE;

  twai_onchip_node_config_t nc = {
      .io_cfg = {.tx = cfg->tx_gpio, .rx = cfg->rx_gpio, .quanta_clk_out = -1, .bus_off_indicator = -1},
      .bit_timing = {.bitrate = 250000},
      .fail_retry_cnt = 3,
      .tx_queue_depth = depth,
      .flags = {.enable_listen_only = cfg->listen_only},
  };
  esp_err_t err = twai_new_node_onchip(&nc, &node);
  if (err == ESP_OK) {
    twai_event_callbacks_t cbs = {.on_tx_done = on_tx_done, .on_rx_done = on_rx_done,
                                  .on_state_change = on_state_change, .on_error = on_error};
    err = twai_node_register_event_callbacks(node, &cbs, NULL);
  }
  if (err == ESP_OK) err = twai_node_enable(node);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(err));
    can_twai_close();
    return err;
  }
  ESP_LOGI(TAG, "250 kbit/s on tx=%d rx=%d%s", cfg->tx_gpio, cfg->rx_gpio, cfg->listen_only ? " (listen-only)" : "");
  return ESP_OK;
}

void can_twai_close(void) {
  if (node) {
    twai_node_disable(node);
    twai_node_delete(node);
    node = NULL;
  }
  if (rx_q) {
    vQueueDelete(rx_q);
    rx_q = NULL;
  }
}

static bool ops_recv(n2k_frame_t *f, void *ctx) {
  (void)ctx;
  return rx_q && xQueueReceive(rx_q, f, 0) == pdTRUE;
}

static bool ops_send(const n2k_frame_t *f, void *ctx) {
  (void)ctx;
  if (!node || f->len > 8) return false;
  tx_slot_t *s = NULL;
  taskENTER_CRITICAL(&pool_lock);
  for (size_t i = 0; i < pool_n; i++)
    if (!pool[i].busy) {
      pool[i].busy = true;
      s = &pool[i];
      break;
    }
  taskEXIT_CRITICAL(&pool_lock);
  if (!s) return false;
  memcpy(s->data, f->data, f->len);
  s->frame = (twai_frame_t){.header = {.id = f->id, .dlc = f->len, .ide = 1},
                            .buffer = s->data,
                            .buffer_len = f->len};
  if (twai_node_transmit(node, &s->frame, 0) != ESP_OK) {
    s->busy = false;
    return false;
  }
  return true;
}

static n2k_can_state_t ops_state(void *ctx) {
  (void)ctx;
  return node_state == TWAI_ERROR_BUS_OFF ? N2K_CAN_BUS_OFF : N2K_CAN_OK;
}

static void ops_recover(void *ctx) {
  (void)ctx;
  if (node) twai_node_recover(node);  // errors are fine: recovery may already be running
}

n2k_can_ops_t can_twai_ops(void) {
  return (n2k_can_ops_t){ops_recv, ops_send, ops_state, ops_recover, NULL};
}

void can_twai_stats(can_twai_stats_t *out) {
  out->rx_dropped = rx_dropped;
  out->tx_failed = tx_failed;
  out->bus_errors = bus_errors;
  out->tx_err_count = out->rx_err_count = 0;
  twai_node_status_t st;
  if (node && twai_node_get_info(node, &st, NULL) == ESP_OK) {
    out->tx_err_count = st.tx_error_count;
    out->rx_err_count = st.rx_error_count;
  }
}
