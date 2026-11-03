// Drives sk_client against a fake transport defined here.
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "boatdata.h"
#include "sk_client.h"
#include "sk_parse.h"
#include "sk_transport.h"

static sk_msg_cb cb;
static void *cb_ctx;
static int polls;
static bool connect_ok;
static char last_sent[512];
static char last_ws_url[160];
static bool valid_while_live;

bool sk_http_get(const char *url, const char *token, char **body, size_t *len) {
  (void)url; (void)token;
  const char *full = "{\"uuid\":\"urn:mrn:signalk:uuid:abc\",\"propulsion\":{\"port\":{\"revolutions\":{\"value\":1}}}}";
  *body = strdup(full);
  *len = strlen(full);
  return true;
}

bool sk_ws_connect(const char *url, const char *token, sk_msg_cb c, void *ctx) {
  (void)token;
  snprintf(last_ws_url, sizeof last_ws_url, "%s", url);
  cb = c;
  cb_ctx = ctx;
  polls = 0;
  return connect_ok;
}

bool sk_ws_send(const char *text) {
  snprintf(last_sent, sizeof last_sent, "%s", text);
  return true;
}

bool sk_ws_poll(int timeout_ms) {
  (void)timeout_ms;
  polls++;
  if (polls == 1) {
    const char *d = "{\"updates\":[{\"values\":[{\"path\":\"propulsion.port.revolutions\",\"value\":30}]}]}";
    cb(d, strlen(d), cb_ctx);
    return true;
  }
  bd_sample_t s;
  bd_get(bd_find("propulsion.port.revolutions"), &s);
  valid_while_live = s.valid && sk_client_state() == SK_LIVE;
  return false;   // server went away
}

void sk_ws_close(void) {}

bool sk_resolve_auto(char *host, size_t n, unsigned *port) {
  (void)host; (void)n; (void)port;
  return false;
}

static bd_id_t rpm;

void setUp(void) {
  bd_init();
  sk_parse_reset();
  connect_ok = true;
  last_sent[0] = 0;
  rpm = bd_intern("propulsion.port.revolutions");
  cfg_t c;
  cfg_defaults(&c);
  strcpy(c.sk.host, "localhost");
  sk_client_init(&c.sk, &rpm, 1);
}
void tearDown(void) {}

static void test_backoff_sequence(void) {
  int want[] = {1000, 2000, 4000, 8000, 16000, 30000, 30000};
  for (int i = 0; i < 7; i++) TEST_ASSERT_EQUAL_INT(want[i], sk_client_backoff_ms(i));
}

static void test_cycle_subscribes_to_bound_paths(void) {
  sk_client_run_once();
  TEST_ASSERT_NOT_NULL(strstr(last_sent, "propulsion.port.revolutions"));
  TEST_ASSERT_NOT_NULL(strstr(last_ws_url, "ws://localhost:3000/signalk/v1/stream?subscribe=none"));
}

static void test_value_valid_while_live(void) {
  sk_client_run_once();
  TEST_ASSERT_TRUE(valid_while_live);
}

static void test_close_invalidates_sk_paths(void) {
  sk_client_run_once();
  bd_sample_t s;
  bd_get(rpm, &s);
  TEST_ASSERT_FALSE(s.valid);
  TEST_ASSERT_EQUAL(SK_BACKOFF, sk_client_state());
}

static void test_failed_connect_goes_to_backoff(void) {
  connect_ok = false;
  sk_client_run_once();
  TEST_ASSERT_EQUAL(SK_BACKOFF, sk_client_state());
  TEST_ASSERT_EQUAL_STRING("", last_sent);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_backoff_sequence);
  RUN_TEST(test_cycle_subscribes_to_bound_paths);
  RUN_TEST(test_value_valid_while_live);
  RUN_TEST(test_close_invalidates_sk_paths);
  RUN_TEST(test_failed_connect_goes_to_backoff);
  return UNITY_END();
}
