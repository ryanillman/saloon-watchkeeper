#include "sk_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alerts.h"
#include "platform.h"
#include "sk_access.h"
#include "sk_cmd.h"
#include "sk_parse.h"
#include "history.h"
#include "sk_history.h"
#include "sk_transport.h"

#define MAX_BOUND 256

PLATFORM_EXT_BSS static struct {
  cfg_sk_t cfg;
  sk_sub_t bound[MAX_BOUND];
  size_t n_bound;
  bool resubscribe;
  bool reconnect;   // new host, port, or token: close and connect again
  bool rediscover;  // setup asked to read the tree again now
  sk_state_t state;
  int attempt;
  uint32_t discoveries;
  int32_t rediscover_ms;
  void *lock;
} c;

int sk_client_backoff_ms(int attempt) {
  if (attempt < 0) attempt = 0;
  if (attempt > 5) return 30000;
  int ms = 1000 << attempt;
  return ms > 30000 ? 30000 : ms;
}

static void set_state(sk_state_t s) {
  platform_lock(c.lock);
  c.state = s;
  platform_unlock(c.lock);
}

sk_state_t sk_client_state(void) {
  platform_lock(c.lock);
  sk_state_t s = c.state;
  platform_unlock(c.lock);
  return s;
}

static void set_bound(const sk_sub_t *ids, size_t n) {
  if (n > MAX_BOUND) n = MAX_BOUND;
  memcpy(c.bound, ids, n * sizeof *ids);
  c.n_bound = n;
}

void sk_client_init(const cfg_sk_t *cfg, const sk_sub_t *bound, size_t n) {
  if (!c.lock) c.lock = platform_lock_create();
  platform_lock(c.lock);
  c.cfg = *cfg;
  set_bound(bound, n);
  c.resubscribe = false;
  c.state = SK_IDLE;
  c.attempt = 0;
  if (!c.rediscover_ms) c.rediscover_ms = 20000;
  platform_unlock(c.lock);
}

void sk_client_rebind(const sk_sub_t *bound, size_t n) {
  platform_lock(c.lock);
  set_bound(bound, n);
  c.resubscribe = true;
  platform_unlock(c.lock);
}

static void on_message(const char *data, size_t len, void *ctx) {
  (void)ctx;
  sk_parse_message(data, len, platform_millis());
}

// The server does not dedupe subscriptions on one socket (each repeat doubles every update),
// so a resubscribe first cancels everything. The connect-time subscribe needs no cancel:
// the URL says subscribe=none.
static bool subscribe(bool replace) {
  if (replace && !sk_ws_send(sk_unsubscribe_all())) return false;
  platform_lock(c.lock);
  char *msg = sk_build_subscribe(c.bound, c.n_bound);
  c.resubscribe = false;
  platform_unlock(c.lock);
  bool ok = msg && sk_ws_send(msg);
  free(msg);
  return ok;
}

// Sends each queued acknowledge or silence; the server's answer decides the alert's send_failed.
__attribute__((noinline)) static void drain_requests(const char *base, const char *token) {   // noinline: keep its buffers out of the run loop's frame
  char id[64];
  alert_req_t req;
  while (alerts_next_request(id, &req)) {
    alert_t a;
    char path[160], url[320];
    int status = 0;
    bool ok = alerts_get(id, &a) && sk_build_alert_request(&a, req, path, sizeof path);
    if (ok) {
      snprintf(url, sizeof url, "%s%s", base, path);
      ok = sk_http_post(url, token, NULL, &status) && status >= 200 && status < 300;
    }
    alerts_request_result(id, ok);
  }
}

// Sends each command the UI queued (see sk_cmd.h). A PUT goes to the REST tree at the dotted path as {"value":N};
// a POST goes to the server path as given. The answer decides the command's result; nothing is retried.
__attribute__((noinline)) static void drain_commands(const char *base, const char *token) {   // noinline: its url and body buffers live only while a command is sent
  sk_cmd_t cmd;
  sk_cmd_handle_t h;
  while (sk_cmd_next(&cmd, &h, platform_millis())) {
    char url[320], body[64];
    const char *method = cmd.method[0] ? cmd.method : "POST", *payload = cmd.body[0] ? cmd.body : NULL;
    if (cmd.put_number) {
      size_t n = (size_t)snprintf(url, sizeof url, "%s/signalk/v1/api/vessels/self/", base);
      for (const char *p = cmd.path; *p && n < sizeof url - 1; p++) url[n++] = *p == '.' ? '/' : *p;
      url[n] = 0;
      snprintf(body, sizeof body, "{\"value\":%g}", cmd.value);
      method = "PUT";
      payload = body;
    } else {
      snprintf(url, sizeof url, "%s%s", base, cmd.path);
    }
    int status = 0;
    char *resp = NULL;
    size_t resp_len = 0;
    bool ok = sk_http_request(method, url, token, payload, &status, &resp, &resp_len);
    char reply_id[40] = "";   // a raise's reply names the new notification; that id is how it is cleared
    if (ok && status >= 200 && status < 300 && resp) sk_cmd_reply_id(resp, resp_len, reply_id, sizeof reply_id);
    free(resp);
    sk_cmd_finish_id(h, ok, status, reply_id);
  }
}

// Reads the REST snapshot; counts a discovery only when it parsed.
static void discover(const char *url, const char *token) {
  char *body = NULL;
  size_t len = 0;
  if (sk_http_get(url, token, &body, &len) && sk_parse_full(body, len)) {
    platform_lock(c.lock);
    c.discoveries++;
    platform_unlock(c.lock);
  }
  free(body);
}

void sk_client_set_rediscover_ms(int32_t ms) {
  if (!c.lock) c.lock = platform_lock_create();
  platform_lock(c.lock);
  c.rediscover_ms = ms;
  platform_unlock(c.lock);
}

void sk_client_rediscover_now(void) {
  if (!c.lock) return;
  platform_lock(c.lock);
  c.rediscover = true;
  platform_unlock(c.lock);
}

void sk_client_reconfigure(const cfg_sk_t *cfg) {
  if (!c.lock) c.lock = platform_lock_create();
  platform_lock(c.lock);
  c.cfg = *cfg;
  c.reconnect = true;
  c.attempt = 0;
  platform_unlock(c.lock);
}

void sk_client_run_once(void) {
  platform_lock(c.lock);
  cfg_sk_t cfg = c.cfg;   // a reconfigure from the UI task may replace it while this cycle runs
  c.reconnect = false;
  platform_unlock(c.lock);
  char host[64];
  unsigned port = cfg.port;
  snprintf(host, sizeof host, "%s", cfg.host);
  if (strcmp(host, "auto") == 0 && !sk_resolve_auto(host, sizeof host, &port)) {
    set_state(SK_BACKOFF);
    return;
  }
  const char *token = cfg.token[0] ? cfg.token : NULL;
  char url[160], rest[160], base[96];
  snprintf(base, sizeof base, "http://%s:%u", host, port);

  sk_access_poll(base, platform_millis());   // an access request needs only HTTP
  set_state(SK_DISCOVERING);
  snprintf(rest, sizeof rest, "http://%s:%u/signalk/v1/api/vessels/self", host, port);
  snprintf(url, sizeof url, "%s", rest);
  discover(url, token);

  set_state(SK_CONNECTING);
  snprintf(url, sizeof url, "ws://%s:%u/signalk/v1/stream?subscribe=none", host, port);
  if (sk_ws_connect(url, token, on_message, NULL) && subscribe(false)) {
    c.attempt = 0;
    set_state(SK_LIVE);
    sk_cmd_set_connected(true);
    alerts_source_up(true);
    printf("signalk: live at %s:%u\n", host, port);
    // Trends fill in from the server's history after a boot or a reconnect (never a fault). One path per request, one
    // request per pass of the loop below, so the live stream is read between them; the UI task may track more paths
    // later, once it has bound the roles, and those are asked for as they appear. A request nobody answers waits 30 s.
    hist_forget_backfill();
    int64_t fill_retry_ms = 0;
    if (sk_history_backfill(base, token) == SK_HIST_NO_ANSWER) fill_retry_ms = platform_millis() + 30000;
    int64_t last_discovery = platform_millis();
    for (;;) {
      platform_lock(c.lock);
      bool again = c.resubscribe, reconnect = c.reconnect, now_please = c.rediscover;
      c.rediscover = false;
      int32_t every = c.rediscover_ms;
      platform_unlock(c.lock);
      if (reconnect) break;   // closes below; the next cycle uses the new settings
      if (again && !subscribe(true)) break;
      if (!sk_ws_poll(200)) break;
      // Deltas were lost while this task was blocked. Notifications are sent only on
      // change, so a lost one would never come back: resubscribe to get them resent.
      if (sk_ws_take_overflow() && !subscribe(true)) break;
      drain_requests(base, token);
      drain_commands(base, token);
      if (platform_millis() >= fill_retry_ms && sk_history_backfill(base, token) == SK_HIST_NO_ANSWER)
        fill_retry_ms = platform_millis() + 30000;
      sk_access_poll(base, platform_millis());
      if (now_please || platform_millis() - last_discovery >= every) {   // engines that power up later
        discover(rest, token);
        last_discovery = platform_millis();
      }
    }
  }
  if (sk_client_state() == SK_LIVE) printf("signalk: connection closed\n");
  sk_ws_close();
  sk_cmd_set_connected(false);
  alerts_source_up(false);
  bd_invalidate_source(SK_SOURCE_ID);   // nothing from this server can be trusted now
  set_state(SK_BACKOFF);
}

static void run(void *arg) {
  (void)arg;
  for (;;) {
    sk_client_run_once();
    platform_sleep_ms((uint32_t)sk_client_backoff_ms(c.attempt++));
  }
}

void sk_client_start(void) {
  if (!platform_task(run, NULL, "signalk", 8192)) fprintf(stderr, "signalk: cannot start the client task; staying offline\n");
}

uint32_t sk_client_discovery_count(void) {
  platform_lock(c.lock);
  uint32_t n = c.discoveries;
  platform_unlock(c.lock);
  return n;
}
