// Signal K device access request (spec: "Signal K access request").
#include "sk_access.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "platform.h"
#include "sk_transport.h"

static struct {
  void *lock;
  ska_state_t state;
  int http_error;
  char message[96];
  bool start;                 // asked to start; taken by the next poll
  char href[128];             // the pending request
  int64_t started_ms, next_ms;
  bool clock_set;             // started_ms/next_ms are set from the poll clock
  char client_id[37], description[64];
  void (*on_token)(const char *);
  void (*on_href)(const char *);
} s;

static void take(void) {
  if (!s.lock) s.lock = platform_lock_create();
  platform_lock(s.lock);
}
static void give(void) { platform_unlock(s.lock); }

void sk_access_reset(void) {
  void *lock = s.lock;
  memset(&s, 0, sizeof s);
  s.lock = lock;
}

void sk_access_set_identity(const char *client_id, const char *description) {
  take();
  snprintf(s.client_id, sizeof s.client_id, "%s", client_id);
  snprintf(s.description, sizeof s.description, "%s", description);
  give();
}

void sk_access_on_token(void (*fn)(const char *)) { s.on_token = fn; }
void sk_access_on_href(void (*fn)(const char *)) { s.on_href = fn; }

void sk_access_start(void) {
  take();
  s.start = true;
  give();
}

void sk_access_resume(const char *href, int64_t now_ms) {
  (void)now_ms;
  take();
  snprintf(s.href, sizeof s.href, "%s", href);
  s.state = SKA_PENDING;
  s.clock_set = false;   // timed from the first poll: the Signal K task's clock
  snprintf(s.message, sizeof s.message, "waiting for approval in the Signal K admin UI");
  give();
}

static void finish(ska_state_t st, int code, const char *msg) {   // lock held
  s.state = st;
  s.http_error = code;
  snprintf(s.message, sizeof s.message, "%s", msg);
  s.href[0] = 0;
}

static void set_href(const char *href) {   // outside the lock: the callback saves config
  if (s.on_href) s.on_href(href);
}

static void send_request(const char *base, int64_t now) {
  take();
  cJSON *b = cJSON_CreateObject();
  cJSON_AddStringToObject(b, "clientId", s.client_id);
  cJSON_AddStringToObject(b, "description", s.description);
  cJSON_AddStringToObject(b, "permissions", "readwrite");
  give();
  char *body = cJSON_PrintUnformatted(b);
  cJSON_Delete(b);
  char url[192], *resp = NULL;
  size_t len = 0;
  int status = 0;
  snprintf(url, sizeof url, "%s/signalk/v1/access/requests", base);
  bool ok = body && sk_http_request("POST", url, NULL, body, &status, &resp, &len);
  free(body);
  cJSON *r = ok && resp ? cJSON_Parse(resp) : NULL;
  const cJSON *href = cJSON_GetObjectItem(r, "href");
  const cJSON *msg = cJSON_GetObjectItem(r, "message");
  bool stored = false;
  take();
  if (!ok) {
    finish(SKA_ERROR, 0, "Signal K server unreachable");
  } else if (status == 202 && cJSON_IsString(href)) {
    snprintf(s.href, sizeof s.href, "%s", href->valuestring);
    s.state = SKA_PENDING;
    s.started_ms = now;
    s.next_ms = now + SKA_POLL_MS;
    s.clock_set = true;
    snprintf(s.message, sizeof s.message, "waiting for approval in the Signal K admin UI");
    stored = true;
  } else if (status == 400 && cJSON_IsString(msg) && strstr(msg->valuestring, "already requested")) {
    finish(SKA_ERROR, 400, "already requested: deny or remove the earlier request in the admin UI, then try again");
  } else {
    char m[96];
    snprintf(m, sizeof m, "Signal K refused the request (HTTP %d)", status);
    finish(SKA_ERROR, status, m);
  }
  char h[128];
  snprintf(h, sizeof h, "%s", s.href);
  give();
  cJSON_Delete(r);
  free(resp);
  if (stored) set_href(h);
}

static void check_request(const char *base, int64_t now) {
  take();
  char url[256];
  snprintf(url, sizeof url, "%s%s", base, s.href);
  give();
  char *resp = NULL;
  size_t len = 0;
  int status = 0;
  bool ok = sk_http_request("GET", url, NULL, NULL, &status, &resp, &len);
  cJSON *r = ok && resp ? cJSON_Parse(resp) : NULL;
  const cJSON *st = cJSON_GetObjectItem(r, "state");
  const cJSON *ar = cJSON_GetObjectItem(r, "accessRequest");
  const cJSON *perm = cJSON_GetObjectItem(ar, "permission"), *tok = cJSON_GetObjectItem(ar, "token");
  char token[256] = "";
  bool done = false;
  take();
  s.next_ms = now + SKA_POLL_MS;
  if (!ok) {
    // The server is down for now: keep the request and try again at the next poll.
  } else if (status != 200) {
    char m[96];
    snprintf(m, sizeof m, "the request is gone from the server (HTTP %d); request again", status);
    finish(SKA_ERROR, status, m);
    done = true;
  } else if (cJSON_IsString(st) && !strcmp(st->valuestring, "COMPLETED")) {
    if (cJSON_IsString(perm) && !strcmp(perm->valuestring, "APPROVED") && cJSON_IsString(tok)) {
      snprintf(token, sizeof token, "%s", tok->valuestring);
      finish(SKA_APPROVED, 0, "approved; token saved");
    } else {
      finish(SKA_DENIED, 0, "denied in the Signal K admin UI");
    }
    done = true;
  }
  give();
  cJSON_Delete(r);
  free(resp);
  if (token[0] && s.on_token) s.on_token(token);
  if (done) set_href("");
}

void sk_access_poll(const char *base_url, int64_t now) {
  take();
  bool start = s.start && s.state != SKA_PENDING;   // already waiting: keep that request
  s.start = false;
  if (s.state == SKA_PENDING && !s.clock_set) {
    s.started_ms = now;
    s.next_ms = now;
    s.clock_set = true;
  }
  bool expired = s.state == SKA_PENDING && now - s.started_ms >= SKA_EXPIRE_MS;
  if (expired) finish(SKA_EXPIRED, 0, "no answer after 30 minutes; request again");
  bool due = s.state == SKA_PENDING && now >= s.next_ms;
  give();
  if (expired) set_href("");
  if (start) send_request(base_url, now);
  else if (due) check_request(base_url, now);
}

ska_state_t sk_access_state(int *http_error) {
  take();
  ska_state_t st = s.state;
  if (http_error) *http_error = s.http_error;
  give();
  return st;
}

const char *sk_access_state_name(ska_state_t st) {
  static const char *names[] = {"idle", "pending", "approved", "denied", "expired", "error"};
  return st <= SKA_ERROR ? names[st] : "?";
}

const char *sk_access_message(void) {
  static char copy[96];   // read by one task at a time (console or UI)
  take();
  snprintf(copy, sizeof copy, "%s", s.message);
  give();
  return copy;
}

// A UUID-shaped id derived from the MAC (FNV-1a stretched over 16 bytes), so the same
// panel always asks as the same device.
void sk_access_client_id_from_mac(const uint8_t mac[6], char out[37]) {
  uint8_t b[16];
  uint32_t h = 2166136261u;
  for (int i = 0; i < 16; i++) {
    h ^= mac[i % 6] + (uint32_t)i * 31u;
    h *= 16777619u;
    b[i] = (uint8_t)(h >> 24);
  }
  b[6] = (uint8_t)((b[6] & 0x0f) | 0x40);   // version 4
  b[8] = (uint8_t)((b[8] & 0x3f) | 0x80);   // RFC 4122 variant
  snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3],
           b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}
