#include "sk_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "sk_parse.h"
#include "sk_transport.h"

#define MAX_BOUND 128

static struct {
  cfg_sk_t cfg;
  bd_id_t bound[MAX_BOUND];
  size_t n_bound;
  bool resubscribe;
  sk_state_t state;
  int attempt;
  uint32_t discoveries;
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

static void set_bound(const bd_id_t *ids, size_t n) {
  if (n > MAX_BOUND) n = MAX_BOUND;
  memcpy(c.bound, ids, n * sizeof *ids);
  c.n_bound = n;
}

void sk_client_init(const cfg_sk_t *cfg, const bd_id_t *bound, size_t n) {
  if (!c.lock) c.lock = platform_lock_create();
  platform_lock(c.lock);
  c.cfg = *cfg;
  set_bound(bound, n);
  c.resubscribe = false;
  c.state = SK_IDLE;
  c.attempt = 0;
  platform_unlock(c.lock);
}

void sk_client_rebind(const bd_id_t *bound, size_t n) {
  platform_lock(c.lock);
  set_bound(bound, n);
  c.resubscribe = true;
  platform_unlock(c.lock);
}

static void on_message(const char *data, size_t len, void *ctx) {
  (void)ctx;
  sk_parse_message(data, len, platform_millis());
}

static bool subscribe(void) {
  platform_lock(c.lock);
  char *msg = sk_build_subscribe(c.bound, c.n_bound);
  c.resubscribe = false;
  platform_unlock(c.lock);
  bool ok = msg && sk_ws_send(msg);
  free(msg);
  return ok;
}

void sk_client_run_once(void) {
  char host[64];
  unsigned port = c.cfg.port;
  snprintf(host, sizeof host, "%s", c.cfg.host);
  if (strcmp(host, "auto") == 0 && !sk_resolve_auto(host, sizeof host, &port)) {
    set_state(SK_BACKOFF);
    return;
  }
  const char *token = c.cfg.token[0] ? c.cfg.token : NULL;
  char url[160];

  set_state(SK_DISCOVERING);
  snprintf(url, sizeof url, "http://%s:%u/signalk/v1/api/vessels/self", host, port);
  char *body = NULL;
  size_t len = 0;
  if (sk_http_get(url, token, &body, &len)) {
    sk_parse_full(body, len);
    platform_lock(c.lock);
    c.discoveries++;
    platform_unlock(c.lock);
  }
  free(body);

  set_state(SK_CONNECTING);
  snprintf(url, sizeof url, "ws://%s:%u/signalk/v1/stream?subscribe=none", host, port);
  if (sk_ws_connect(url, token, on_message, NULL) && subscribe()) {
    c.attempt = 0;
    set_state(SK_LIVE);
    for (;;) {
      platform_lock(c.lock);
      bool again = c.resubscribe;
      platform_unlock(c.lock);
      if (again && !subscribe()) break;
      if (!sk_ws_poll(200)) break;
    }
  }
  sk_ws_close();
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

void sk_client_start(void) { platform_task(run, NULL, "signalk", 8192); }

uint32_t sk_client_discovery_count(void) {
  platform_lock(c.lock);
  uint32_t n = c.discoveries;
  platform_unlock(c.lock);
  return n;
}
