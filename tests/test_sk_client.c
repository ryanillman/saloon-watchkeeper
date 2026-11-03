// Drives sk_client against a fake transport defined here.
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "alerts.h"
#include "boatdata.h"
#include "sk_client.h"
#include "sk_parse.h"
#include "sk_transport.h"

static sk_msg_cb cb;
static void *cb_ctx;
static int polls;
static int http_gets;
static int live_polls = 1;   // polls that keep the socket open
static const char *full_body;
static bool connect_ok;
static char last_sent[512];
static char last_ws_url[160];
static bool valid_while_live;
static int posts;
static char last_post[256];
static int post_status = 200;
static int live_hook;   // 1: queue an ack on the first poll; 2: also check send_failed while live
static int fail_seen = -1;
static int sends;
static int rebind_at_poll;   // sk_client_rebind during this poll (0: never)
static char sent_log[8][512];
static int overflow_at_poll;   // sk_ws_take_overflow reports a drop after this poll (0: never)
static bool overflow_taken;

bool sk_http_get(const char *url, const char *token, char **body, size_t *len) {
  (void)url; (void)token;
  http_gets++;
  const char *full = full_body ? full_body : "{\"uuid\":\"urn:mrn:signalk:uuid:abc\",\"propulsion\":{\"port\":{\"revolutions\":{\"value\":1}}}}";
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
  if (sends < 8) snprintf(sent_log[sends], sizeof sent_log[0], "%s", text);
  sends++;
  return true;
}

bool sk_ws_poll(int timeout_ms) {
  (void)timeout_ms;
  polls++;
  if (rebind_at_poll && polls == rebind_at_poll) {
    sk_sub_t one = {bd_intern("propulsion.port.revolutions"), 0};
    sk_client_rebind(&one, 1);
  }
  if (polls > 1 && polls <= live_polls) return true;
  if (polls == 1) {
    if (live_hook == 1 || live_hook == 2) {
      const char *n = "{\"updates\":[{\"values\":[{\"path\":\"notifications.a.b\",\"value\":{\"state\":\"alarm\",\"message\":\"m\",\"id\":\"u-9\"}}]}]}";
      cb(n, strlen(n), cb_ctx);
      alerts_request("a.b", AL_REQ_ACK);
    }
    const char *d = "{\"updates\":[{\"values\":[{\"path\":\"propulsion.port.revolutions\",\"value\":30}]}]}";
    cb(d, strlen(d), cb_ctx);
    return true;
  }
  if (live_hook == 2 && polls == 4) {
    alert_t a;
    fail_seen = alerts_get("a.b", &a) && a.send_failed;
  }
  bd_sample_t s;
  bd_get(bd_find("propulsion.port.revolutions"), &s);
  valid_while_live = s.valid && sk_client_state() == SK_LIVE;
  return false;   // server went away
}

void sk_ws_close(void) {}

bool sk_ws_take_overflow(void) {
  if (overflow_at_poll && polls == overflow_at_poll && !overflow_taken) {
    overflow_taken = true;
    return true;
  }
  return false;
}

bool sk_http_post(const char *url, const char *token, const char *json_body, int *status) {
  (void)token; (void)json_body;
  posts++;
  snprintf(last_post, sizeof last_post, "%s", url);
  *status = post_status;
  return true;
}

bool sk_resolve_auto(char *host, size_t n, unsigned *port) {
  (void)host; (void)n; (void)port;
  return false;
}

static bd_id_t rpm;

void setUp(void) {
  bd_init();
  alerts_init();
  posts = 0;
  last_post[0] = 0;
  post_status = 200;
  live_hook = 0;
  sends = 0;
  rebind_at_poll = 0;
  overflow_at_poll = 0;
  overflow_taken = false;
  sk_parse_reset();
  connect_ok = true;
  last_sent[0] = 0;
  http_gets = 0;
  live_polls = 1;
  full_body = NULL;
  sk_client_set_rediscover_ms(20000);
  rpm = bd_intern("propulsion.port.revolutions");
  cfg_t c;
  cfg_defaults(&c);
  strcpy(c.sk.host, "localhost");
  sk_sub_t sub = {rpm, 0};
  sk_client_init(&c.sk, &sub, 1);
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

static void test_rediscovers_while_live(void) {   // an engine that powers up after the display
  sk_client_set_rediscover_ms(0);   // every poll, for the test
  live_polls = 4;
  uint32_t before = sk_client_discovery_count();
  sk_client_run_once();
  TEST_ASSERT_GREATER_THAN_INT(1, http_gets);
  TEST_ASSERT_GREATER_THAN_UINT32(before + 1, sk_client_discovery_count());
}

static void test_unparseable_snapshot_is_not_a_discovery(void) {
  full_body = "not json";
  uint32_t before = sk_client_discovery_count();
  sk_client_run_once();
  TEST_ASSERT_EQUAL_UINT32(before, sk_client_discovery_count());
}

static void test_disconnect_clears_alerts(void) {
  live_hook = 1;
  sk_client_run_once();
  alerts_snapshot_t s;
  alerts_snapshot(&s);
  TEST_ASSERT_EQUAL_UINT8(0, s.count);
  TEST_ASSERT_FALSE(s.source_up);
}

static void test_queued_ack_sent_while_live(void) {
  live_hook = 1;
  live_polls = 3;
  sk_client_run_once();
  TEST_ASSERT_EQUAL_INT(1, posts);
  TEST_ASSERT_NOT_NULL(strstr(last_post, "/signalk/v2/api/notifications/u-9/acknowledge"));
}

static void test_post_failure_marks_send_failed(void) {
  post_status = 401;
  live_hook = 2;   // check the flag while still live
  live_polls = 3;
  fail_seen = -1;
  sk_client_run_once();
  TEST_ASSERT_EQUAL_INT(1, posts);
  TEST_ASSERT_EQUAL_INT(1, fail_seen);
}

static void test_no_post_after_reconnect_for_dropped_request(void) {
  alerts_source_up(false);
  alerts_request("a.b", AL_REQ_ACK);   // source down: dropped, marked failed
  sk_client_run_once();
  TEST_ASSERT_EQUAL_INT(0, posts);
}

static void test_overflow_resubscribes(void) {   // lost deltas: the server resends on subscribe
  live_polls = 4;
  overflow_at_poll = 2;
  sk_client_run_once();
  TEST_ASSERT_EQUAL_INT(3, sends);   // connect subscribe, then unsubscribe-all + subscribe
  TEST_ASSERT_NOT_NULL(strstr(sent_log[0], "propulsion.port.revolutions"));
  TEST_ASSERT_EQUAL_STRING(sk_unsubscribe_all(), sent_log[1]);
  TEST_ASSERT_NOT_NULL(strstr(sent_log[2], "propulsion.port.revolutions"));
}

static void test_no_overflow_subscribes_once(void) {   // a quiet session sends one subscribe, no unsubscribe
  live_polls = 4;
  sk_client_run_once();
  TEST_ASSERT_EQUAL_INT(1, sends);
  TEST_ASSERT_NULL(strstr(sent_log[0], "unsubscribe"));
}

static void test_rebind_unsubscribes_first(void) {   // the server does not dedupe repeated subscribes
  live_polls = 4;
  rebind_at_poll = 2;
  sk_client_run_once();
  TEST_ASSERT_EQUAL_INT(3, sends);
  TEST_ASSERT_NULL(strstr(sent_log[0], "unsubscribe"));   // fresh socket: nothing to cancel
  TEST_ASSERT_EQUAL_STRING(sk_unsubscribe_all(), sent_log[1]);
  TEST_ASSERT_NOT_NULL(strstr(sent_log[2], "propulsion.port.revolutions"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_backoff_sequence);
  RUN_TEST(test_cycle_subscribes_to_bound_paths);
  RUN_TEST(test_value_valid_while_live);
  RUN_TEST(test_close_invalidates_sk_paths);
  RUN_TEST(test_failed_connect_goes_to_backoff);
  RUN_TEST(test_rediscovers_while_live);
  RUN_TEST(test_unparseable_snapshot_is_not_a_discovery);
  RUN_TEST(test_disconnect_clears_alerts);
  RUN_TEST(test_queued_ack_sent_while_live);
  RUN_TEST(test_post_failure_marks_send_failed);
  RUN_TEST(test_no_post_after_reconnect_for_dropped_request);
  RUN_TEST(test_overflow_resubscribes);
  RUN_TEST(test_no_overflow_subscribes_once);
  RUN_TEST(test_rebind_unsubscribes_first);
  return UNITY_END();
}
