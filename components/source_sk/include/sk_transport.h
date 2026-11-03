#pragma once
// HTTP and WebSocket, one implementation per platform (libcurl on Linux,
// esp_http_client/esp_websocket_client on the ESP32).
#include <stdbool.h>
#include <stddef.h>

typedef void (*sk_msg_cb)(const char *data, size_t len, void *ctx);

bool sk_http_get(const char *url, const char *token, char **body, size_t *len);   // caller frees body
// POST with Bearer token and Content-Type: application/json. json_body may be NULL (empty body).
// Returns true when a response arrived; *status is the HTTP code.
bool sk_http_post(const char *url, const char *token, const char *json_body, int *status);

// Any method; returns the response body too (caller frees *resp). False when no connection.
bool sk_http_request(const char *method, const char *url, const char *token, const char *body,
                     int *status, char **resp, size_t *resp_len);
bool sk_ws_connect(const char *url, const char *token, sk_msg_cb cb, void *ctx);
bool sk_ws_send(const char *text);
bool sk_ws_poll(int timeout_ms);   // delivers complete text messages; false once the socket is closed
void sk_ws_close(void);
// True once (then cleared) when messages were dropped since the last call because
// the receive side fell behind. Notifications are sent only on change, so the
// caller must resubscribe: the server then resends current values and notifications.
bool sk_ws_take_overflow(void);

// Finds a Signal K server for host "auto" (mDNS _signalk-ws._tcp on the ESP32,
// localhost:3000 on Linux).
bool sk_resolve_auto(char *host, size_t host_len, unsigned *port);
