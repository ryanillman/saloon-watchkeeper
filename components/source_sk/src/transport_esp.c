// ESP32 transport: esp_http_client, esp_websocket_client, and mDNS.
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "mdns.h"
#include "sk_transport.h"

static const char *TAG = "sk";

static char *auth_value(const char *token) {
  if (!token) return NULL;
  size_t n = strlen(token) + 8;
  char *v = malloc(n);
  if (v) snprintf(v, n, "Bearer %s", token);
  return v;
}

// Response bodies go to PSRAM: a 12 h history reply is ~25 KB and the buffer doubles as it grows, which in internal RAM
// (where malloc puts anything up to 16 KB) would hold 24 KB at once on a board with about 38 KB free. Falls back to
// ordinary malloc if PSRAM has no room. The caller frees with free(), which handles either.
static void *body_alloc(size_t n) {
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : malloc(n);
}
static void *body_grow(void *old, size_t n) {
  void *p = heap_caps_realloc(old, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : realloc(old, n);
}

bool sk_http_get(const char *url, const char *token, char **body, size_t *len) {
  esp_http_client_config_t cfg = {.url = url, .timeout_ms = 10000};
  esp_http_client_handle_t h = esp_http_client_init(&cfg);
  if (!h) return false;
  char *auth = auth_value(token);
  if (auth) esp_http_client_set_header(h, "Authorization", auth);
  bool ok = false;
  if (esp_http_client_open(h, 0) == ESP_OK && esp_http_client_fetch_headers(h) >= 0 &&
      esp_http_client_get_status_code(h) == 200) {
    size_t cap = 8192, n = 0;
    char *buf = body_alloc(cap);
    int r;
    while (buf && (r = esp_http_client_read(h, buf + n, (int)(cap - n - 1))) > 0) {
      n += (size_t)r;
      if (cap - n < 1024) {
        char *next = body_grow(buf, cap * 2);
        if (!next) break;
        buf = next;
        cap *= 2;
      }
    }
    if (buf) {
      buf[n] = 0;
      *body = buf;
      *len = n;
      ok = true;
    }
  }
  esp_http_client_close(h);
  esp_http_client_cleanup(h);
  free(auth);
  return ok;
}

bool sk_http_post(const char *url, const char *token, const char *json_body, int *status) {
  esp_http_client_config_t cfg = {.url = url, .method = HTTP_METHOD_POST, .timeout_ms = 5000};
  esp_http_client_handle_t h = esp_http_client_init(&cfg);
  if (!h) return false;
  char *auth = auth_value(token);
  if (auth) esp_http_client_set_header(h, "Authorization", auth);
  esp_http_client_set_header(h, "Content-Type", "application/json");
  esp_http_client_set_post_field(h, json_body ? json_body : "", json_body ? (int)strlen(json_body) : 0);
  bool ok = esp_http_client_perform(h) == ESP_OK;
  if (ok) *status = esp_http_client_get_status_code(h);
  esp_http_client_cleanup(h);
  free(auth);
  return ok;
}

bool sk_http_request(const char *method, const char *url, const char *token, const char *body,
                     int *status, char **resp, size_t *resp_len) {
  *resp = NULL;
  *resp_len = 0;
  esp_http_client_method_t m = !strcmp(method, "POST") ? HTTP_METHOD_POST
                               : !strcmp(method, "PUT") ? HTTP_METHOD_PUT : HTTP_METHOD_GET;
  esp_http_client_config_t cfg = {.url = url, .method = m, .timeout_ms = 5000};
  esp_http_client_handle_t h = esp_http_client_init(&cfg);
  if (!h) return false;
  char *auth = auth_value(token);
  if (auth) esp_http_client_set_header(h, "Authorization", auth);
  int blen = body ? (int)strlen(body) : 0;
  if (body) esp_http_client_set_header(h, "Content-Type", "application/json");
  bool ok = esp_http_client_open(h, blen) == ESP_OK;
  if (ok && blen) ok = esp_http_client_write(h, body, blen) == blen;
  if (ok) ok = esp_http_client_fetch_headers(h) >= 0;
  if (ok) {
    *status = esp_http_client_get_status_code(h);
    size_t cap = 1024, n = 0;   // access-request replies are small
    char *buf = malloc(cap);
    int r;
    while (buf && n < cap - 1 && (r = esp_http_client_read(h, buf + n, (int)(cap - n - 1))) > 0) n += (size_t)r;
    if (buf) {
      buf[n] = 0;
      *resp = buf;
      *resp_len = n;
    }
  }
  esp_http_client_close(h);
  esp_http_client_cleanup(h);
  free(auth);
  return ok;
}

// Messages are copied off the websocket task and delivered from sk_ws_poll,
// so parsing happens on the Signal K task.
typedef struct { char *data; size_t len; } msg_t;

// Deep enough to ride out a rediscovery GET or an ACK POST on a busy Venus.
// The queue storage lives in PSRAM; it is used only from task context.
#define WS_QUEUE_LEN 256

static struct {
  esp_websocket_client_handle_t h;
  QueueHandle_t q;
  sk_msg_cb cb;
  void *ctx;
  char *frag;   // text message being reassembled
  size_t frag_len;
  volatile bool closed;
  atomic_uint dropped;   // messages dropped since the last sk_ws_take_overflow
  char *auth;
} ws;

static char *msg_alloc(size_t n) {
  char *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : malloc(n);
}

static void on_ws(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg; (void)base;
  esp_websocket_event_data_t *e = data;
  if (id == WEBSOCKET_EVENT_DISCONNECTED || id == WEBSOCKET_EVENT_CLOSED || id == WEBSOCKET_EVENT_ERROR) {
    ws.closed = true;
    return;
  }
  if (id != WEBSOCKET_EVENT_DATA || (e->op_code != 0x1 && e->op_code != 0x0)) return;
  // A frame may arrive in pieces: payload_offset/payload_len describe the whole frame.
  if (e->payload_offset == 0) {
    free(ws.frag);
    ws.frag = msg_alloc((size_t)e->payload_len + 1);
    ws.frag_len = 0;
  }
  if (!ws.frag) {
    if (e->payload_offset + e->data_len >= e->payload_len) atomic_fetch_add(&ws.dropped, 1);   // lost to OOM
    return;
  }
  memcpy(ws.frag + e->payload_offset, e->data_ptr, (size_t)e->data_len);
  ws.frag_len = (size_t)(e->payload_offset + e->data_len);
  if (ws.frag_len >= (size_t)e->payload_len) {
    ws.frag[ws.frag_len] = 0;
    msg_t m = {ws.frag, ws.frag_len};
    if (xQueueSend(ws.q, &m, 0) != pdTRUE) {
      free(ws.frag);
      atomic_fetch_add(&ws.dropped, 1);
    }
    ws.frag = NULL;
  }
}

bool sk_ws_connect(const char *url, const char *token, sk_msg_cb cb, void *ctx) {
  sk_ws_close();
  if (!ws.q) ws.q = xQueueCreateWithCaps(WS_QUEUE_LEN, sizeof(msg_t), MALLOC_CAP_SPIRAM);
  if (!ws.q) ws.q = xQueueCreate(32, sizeof(msg_t));
  if (!ws.q) return false;
  atomic_store(&ws.dropped, 0);
  ws.cb = cb;
  ws.ctx = ctx;
  ws.closed = false;
  ws.auth = auth_value(token);
  char headers[320] = "";
  if (ws.auth) snprintf(headers, sizeof headers, "Authorization: %s\r\n", ws.auth);
  esp_websocket_client_config_t cfg = {
    .uri = url,
    .headers = ws.auth ? headers : NULL,
    .buffer_size = 4096,
    .disable_auto_reconnect = true,   // the client owns reconnects and invalidation
    .network_timeout_ms = 10000,
    // A connection that dies without a FIN (Wi-Fi drop, AP reboot, frozen host)
    // must not leave the glass reading "SK LIVE" for minutes.
    .ping_interval_sec = 5,
    .pingpong_timeout_sec = 15,
    .keep_alive_enable = true,
    .keep_alive_idle = 5,
    .keep_alive_interval = 5,
    .keep_alive_count = 3,
  };
  ws.h = esp_websocket_client_init(&cfg);
  if (!ws.h) return false;
  esp_websocket_register_events(ws.h, WEBSOCKET_EVENT_ANY, on_ws, NULL);
  if (esp_websocket_client_start(ws.h) != ESP_OK) return false;
  for (int i = 0; i < 100 && !esp_websocket_client_is_connected(ws.h) && !ws.closed; i++)
    vTaskDelay(pdMS_TO_TICKS(100));
  return esp_websocket_client_is_connected(ws.h);
}

bool sk_ws_send(const char *text) {
  return ws.h && esp_websocket_client_send_text(ws.h, text, (int)strlen(text), pdMS_TO_TICKS(5000)) >= 0;
}

bool sk_ws_poll(int timeout_ms) {
  if (!ws.h) return false;
  msg_t m;
  if (xQueueReceive(ws.q, &m, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
    ws.cb(m.data, m.len, ws.ctx);
    free(m.data);
    while (xQueueReceive(ws.q, &m, 0) == pdTRUE) {
      ws.cb(m.data, m.len, ws.ctx);
      free(m.data);
    }
  }
  return !ws.closed && esp_websocket_client_is_connected(ws.h);
}

bool sk_ws_take_overflow(void) {
  unsigned n = atomic_exchange(&ws.dropped, 0);
  if (!n) return false;
  ESP_LOGW(TAG, "%u messages dropped (receive queue full); resubscribing", (unsigned)n);
  return true;
}

void sk_ws_close(void) {
  if (ws.h) {
    esp_websocket_client_destroy(ws.h);
    ws.h = NULL;
  }
  msg_t m;
  while (ws.q && xQueueReceive(ws.q, &m, 0) == pdTRUE) free(m.data);
  free(ws.frag);
  ws.frag = NULL;
  free(ws.auth);
  ws.auth = NULL;
}

bool sk_resolve_auto(char *host, size_t host_len, unsigned *port) {
  static bool started;
  if (!started) started = mdns_init() == ESP_OK;
  mdns_result_t *r = NULL;
  if (mdns_query_ptr("_signalk-ws", "_tcp", 3000, 1, &r) != ESP_OK || !r || !r->addr) {
    if (r) mdns_query_results_free(r);
    ESP_LOGW(TAG, "no Signal K server found by mDNS");
    return false;
  }
  if (r->addr->addr.type == ESP_IPADDR_TYPE_V4)
    snprintf(host, host_len, IPSTR, IP2STR(&r->addr->addr.u_addr.ip4));
  else
    snprintf(host, host_len, "%s.local", r->hostname ? r->hostname : "signalk");
  *port = r->port;
  mdns_query_results_free(r);
  return true;
}
