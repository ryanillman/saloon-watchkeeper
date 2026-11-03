// Linux transport: libcurl for HTTP and WebSocket (curl >= 8.11).
#include <curl/curl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include "sk_transport.h"

typedef struct { char *data; size_t len; } buf_t;

static size_t on_body(char *p, size_t size, size_t n, void *ud) {
  buf_t *b = ud;
  size_t add = size * n;
  char *next = realloc(b->data, b->len + add + 1);
  if (!next) return 0;
  memcpy(next + b->len, p, add);
  b->data = next;
  b->len += add;
  b->data[b->len] = 0;
  return add;
}

static struct curl_slist *auth_header(const char *token) {
  if (!token) return NULL;
  char h[300];
  snprintf(h, sizeof h, "Authorization: Bearer %s", token);
  return curl_slist_append(NULL, h);
}

bool sk_http_get(const char *url, const char *token, char **body, size_t *len) {
  CURL *h = curl_easy_init();
  if (!h) return false;
  buf_t b = {0};
  struct curl_slist *hdr = auth_header(token);
  curl_easy_setopt(h, CURLOPT_URL, url);
  curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, on_body);
  curl_easy_setopt(h, CURLOPT_WRITEDATA, &b);
  curl_easy_setopt(h, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(h, CURLOPT_FAILONERROR, 1L);
  if (hdr) curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdr);
  CURLcode rc = curl_easy_perform(h);
  curl_slist_free_all(hdr);
  curl_easy_cleanup(h);
  if (rc != CURLE_OK) {
    free(b.data);
    return false;
  }
  *body = b.data;
  *len = b.len;
  return true;
}

bool sk_http_post(const char *url, const char *token, const char *json_body, int *status) {
  CURL *h = curl_easy_init();
  if (!h) return false;
  struct curl_slist *hdr = auth_header(token);
  hdr = curl_slist_append(hdr, "Content-Type: application/json");
  buf_t b = {0};
  curl_easy_setopt(h, CURLOPT_URL, url);
  curl_easy_setopt(h, CURLOPT_POST, 1L);
  curl_easy_setopt(h, CURLOPT_POSTFIELDS, json_body ? json_body : "");
  curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, on_body);
  curl_easy_setopt(h, CURLOPT_WRITEDATA, &b);
  curl_easy_setopt(h, CURLOPT_TIMEOUT, 5L);
  curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdr);
  CURLcode rc = curl_easy_perform(h);
  long code = 0;
  if (rc == CURLE_OK) curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
  curl_slist_free_all(hdr);
  curl_easy_cleanup(h);
  free(b.data);
  *status = (int)code;
  return rc == CURLE_OK;
}

static struct {
  CURL *h;
  struct curl_slist *hdr;
  sk_msg_cb cb;
  void *ctx;
  buf_t msg;   // a text message being reassembled from fragments
} ws;

bool sk_ws_connect(const char *url, const char *token, sk_msg_cb cb, void *ctx) {
  sk_ws_close();
  ws.h = curl_easy_init();
  if (!ws.h) return false;
  ws.hdr = auth_header(token);
  ws.cb = cb;
  ws.ctx = ctx;
  curl_easy_setopt(ws.h, CURLOPT_URL, url);
  curl_easy_setopt(ws.h, CURLOPT_CONNECT_ONLY, 2L);   // WebSocket upgrade, then hand over the socket
  curl_easy_setopt(ws.h, CURLOPT_CONNECTTIMEOUT, 10L);
  if (ws.hdr) curl_easy_setopt(ws.h, CURLOPT_HTTPHEADER, ws.hdr);
  if (curl_easy_perform(ws.h) != CURLE_OK) {
    sk_ws_close();
    return false;
  }
  return true;
}

bool sk_ws_send(const char *text) {
  if (!ws.h) return false;
  size_t sent = 0, len = strlen(text);
  while (sent < len) {
    size_t n = 0;
    CURLcode rc = curl_ws_send(ws.h, text + sent, len - sent, &n, 0, CURLWS_TEXT);
    if (rc == CURLE_AGAIN) continue;
    if (rc != CURLE_OK) return false;
    sent += n;
  }
  return true;
}

bool sk_ws_poll(int timeout_ms) {
  if (!ws.h) return false;
  curl_socket_t sock;
  if (curl_easy_getinfo(ws.h, CURLINFO_ACTIVESOCKET, &sock) != CURLE_OK || sock == CURL_SOCKET_BAD) return false;
  struct pollfd pfd = {.fd = sock, .events = POLLIN};
  if (poll(&pfd, 1, timeout_ms) <= 0) return true;   // quiet is fine
  for (;;) {
    char chunk[4096];
    size_t n = 0;
    const struct curl_ws_frame *meta = NULL;
    CURLcode rc = curl_ws_recv(ws.h, chunk, sizeof chunk, &n, &meta);
    if (rc == CURLE_AGAIN) return true;
    if (rc != CURLE_OK || !meta) return false;
    if (meta->flags & CURLWS_CLOSE) return false;
    if (!(meta->flags & (CURLWS_TEXT | CURLWS_CONT))) continue;   // ping/pong/binary
    if (on_body(chunk, 1, n, &ws.msg) != n) return false;
    if (meta->bytesleft == 0 && !(meta->flags & CURLWS_CONT)) {
      if (ws.msg.len) ws.cb(ws.msg.data, ws.msg.len, ws.ctx);
      ws.msg.len = 0;
    }
  }
}

void sk_ws_close(void) {
  if (ws.h) curl_easy_cleanup(ws.h);
  curl_slist_free_all(ws.hdr);
  free(ws.msg.data);
  memset(&ws, 0, sizeof ws);
}

bool sk_resolve_auto(char *host, size_t host_len, unsigned *port) {
  snprintf(host, host_len, "localhost");
  *port = 3000;
  return true;
}
