#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "alerts.h"

void setUp(void) { alerts_init(); alerts_source_up(true); }
void tearDown(void) {}

static void raise_ex(const char *id, alert_state_t st, bool audible, bool acked, const char *ref, int64_t t) {
  alert_t a = {0};
  snprintf(a.id, sizeof a.id, "%s", id);
  a.state = st;
  snprintf(a.message, sizeof a.message, "msg %s", id);
  a.audible = audible;
  a.acked = acked;
  snprintf(a.ref, sizeof a.ref, "%s", ref);
  alerts_upsert(&a, t);
}
static void raise(const char *id, alert_state_t st, int64_t t) { raise_ex(id, st, true, false, "uuid", t); }
static alert_t get(const char *id) {
  alert_t a;
  TEST_ASSERT_TRUE(alerts_get(id, &a));
  return a;
}

static void test_upsert_and_clear(void) {
  alerts_snapshot_t s;
  raise("a.b", AL_WARN, 100);
  alerts_snapshot(&s);
  TEST_ASSERT_TRUE(s.source_up);
  TEST_ASSERT_EQUAL(1, s.count);
  TEST_ASSERT_EQUAL_STRING("a.b", s.a[0].id);
  TEST_ASSERT_EQUAL(AL_WARN, s.a[0].state);
  TEST_ASSERT_EQUAL_INT64(100, s.a[0].since_ms);
  alerts_clear("a.b");
  alerts_snapshot(&s);
  TEST_ASSERT_EQUAL(0, s.count);
}

static void test_normal_clears(void) {
  alerts_snapshot_t s;
  raise("a.b", AL_WARN, 100);
  raise("a.b", AL_NORMAL, 200);
  alerts_snapshot(&s);
  TEST_ASSERT_EQUAL(0, s.count);
}

static void test_sorted_by_severity_then_newest(void) {
  alerts_snapshot_t s;
  raise("w.old", AL_WARN, 100);
  raise("w.new", AL_WARN, 300);
  raise("alarm", AL_ALARM, 200);
  alerts_snapshot(&s);
  TEST_ASSERT_EQUAL(3, s.count);
  TEST_ASSERT_EQUAL_STRING("alarm", s.a[0].id);
  TEST_ASSERT_EQUAL_STRING("w.new", s.a[1].id);
  TEST_ASSERT_EQUAL_STRING("w.old", s.a[2].id);
}

static void test_escalation_resets_ack(void) {
  raise("a", AL_WARN, 100);
  alerts_request("a", AL_REQ_ACK);
  alerts_request("a", AL_REQ_SILENCE);
  TEST_ASSERT_TRUE(get("a").acked);
  raise("a", AL_ALARM, 500);
  alert_t a = get("a");
  TEST_ASSERT_FALSE(a.acked);
  TEST_ASSERT_FALSE(a.silenced);
  TEST_ASSERT_EQUAL_INT64(500, a.since_ms);
  // a clear followed by a raise also resets
  alerts_request("a", AL_REQ_ACK);
  raise("a", AL_NORMAL, 600);
  raise("a", AL_ALARM, 700);
  a = get("a");
  TEST_ASSERT_FALSE(a.acked);
  TEST_ASSERT_EQUAL_INT64(700, a.since_ms);
}

static void test_same_state_keeps_ack(void) {
  raise("a", AL_WARN, 100);
  alerts_request("a", AL_REQ_ACK);
  raise("a", AL_WARN, 900);
  alert_t a = get("a");
  TEST_ASSERT_TRUE(a.acked);
  TEST_ASSERT_EQUAL_INT64(100, a.since_ms);
}

static void test_source_ack_merges_with_local(void) {
  raise_ex("a", AL_WARN, true, false, "u", 100);
  alerts_request("a", AL_REQ_ACK);
  raise_ex("a", AL_WARN, true, false, "u", 200);   // source says not acked: local still holds
  TEST_ASSERT_TRUE(get("a").acked);
  raise_ex("b", AL_WARN, true, false, "u", 100);
  TEST_ASSERT_FALSE(get("b").acked);
  raise_ex("b", AL_WARN, true, true, "u", 200);    // source acked an un-acked alert
  TEST_ASSERT_TRUE(get("b").acked);
  raise_ex("b", AL_ALARM, true, false, "u", 300);  // escalation resets source-derived too
  TEST_ASSERT_FALSE(get("b").acked);
}

static void test_source_down_clears_and_flags(void) {
  alerts_snapshot_t s;
  raise("a", AL_WARN, 100);
  alerts_source_up(false);
  alerts_snapshot(&s);
  TEST_ASSERT_FALSE(s.source_up);
  TEST_ASSERT_EQUAL(0, s.count);
}

static void test_overflow_drops_least_severe(void) {
  alerts_snapshot_t s;
  char id[16];
  for (int i = 0; i < ALERTS_MAX; i++) { snprintf(id, sizeof id, "w%d", i); raise(id, AL_WARN, 100 + i); }
  raise("alarm", AL_ALARM, 500);
  alerts_snapshot(&s);
  TEST_ASSERT_EQUAL(ALERTS_MAX, s.count);
  TEST_ASSERT_EQUAL_STRING("alarm", s.a[0].id);
  int warns = 0;
  for (int i = 0; i < s.count; i++) warns += s.a[i].state == AL_WARN;
  TEST_ASSERT_EQUAL(ALERTS_MAX - 1, warns);

  alerts_init(); alerts_source_up(true);
  for (int i = 0; i < ALERTS_MAX; i++) { snprintf(id, sizeof id, "x%d", i); raise(id, AL_ALARM, 100 + i); }
  raise("late.warn", AL_WARN, 900);
  alerts_snapshot(&s);
  TEST_ASSERT_EQUAL(ALERTS_MAX, s.count);
  TEST_ASSERT_FALSE(alerts_get("late.warn", &(alert_t){0}));
}

static void test_for_path(void) {
  raise("propulsion.port.temperature", AL_ALARM, 100);
  TEST_ASSERT_EQUAL(AL_ALARM, alerts_for_path("propulsion.port.temperature"));
  TEST_ASSERT_EQUAL(AL_NORMAL, alerts_for_path("propulsion.port"));
  TEST_ASSERT_EQUAL(AL_NORMAL, alerts_for_path("nothing"));
}

static void test_get_missing(void) {
  alert_t a;
  TEST_ASSERT_FALSE(alerts_get("nope", &a));
}

static void test_request_while_down_marks_failed(void) {
  char id[64];
  alert_req_t r;
  raise("a", AL_ALARM, 100);
  alerts_source_up(false);
  raise("a", AL_ALARM, 100);   // a late upsert while down is still shown
  alerts_request("a", AL_REQ_ACK);
  alert_t a = get("a");
  TEST_ASSERT_TRUE(a.acked);
  TEST_ASSERT_TRUE(a.send_failed);
  TEST_ASSERT_FALSE(alerts_next_request(id, &r));
}

static void test_request_without_ref_marks_failed(void) {
  char id[64];
  alert_req_t r;
  raise_ex("a", AL_WARN, true, false, "", 100);
  alerts_request("a", AL_REQ_SILENCE);
  alert_t a = get("a");
  TEST_ASSERT_TRUE(a.silenced);
  TEST_ASSERT_TRUE(a.send_failed);
  TEST_ASSERT_FALSE(alerts_next_request(id, &r));
}

static void test_request_round_trip(void) {
  char id[64];
  alert_req_t r;
  raise("a", AL_ALARM, 100);
  alerts_request("a", AL_REQ_ACK);
  TEST_ASSERT_TRUE(get("a").acked);
  TEST_ASSERT_FALSE(get("a").send_failed);
  TEST_ASSERT_TRUE(alerts_next_request(id, &r));
  TEST_ASSERT_EQUAL_STRING("a", id);
  TEST_ASSERT_EQUAL(AL_REQ_ACK, r);
  TEST_ASSERT_FALSE(alerts_next_request(id, &r));
  alerts_request_result("a", false);
  TEST_ASSERT_TRUE(get("a").send_failed);
  alerts_request("a", AL_REQ_ACK);
  alerts_request_result("a", true);
  TEST_ASSERT_FALSE(get("a").send_failed);
}

static void test_queue_overflow_marks_oldest_failed(void) {
  char id[64], n[16];
  alert_req_t r;
  for (int i = 0; i < 9; i++) { snprintf(n, sizeof n, "q%d", i); raise(n, AL_WARN, 100 + i); }
  for (int i = 0; i < 9; i++) { snprintf(n, sizeof n, "q%d", i); alerts_request(n, AL_REQ_ACK); }
  TEST_ASSERT_TRUE(get("q0").send_failed);
  TEST_ASSERT_FALSE(get("q1").send_failed);
  TEST_ASSERT_TRUE(alerts_next_request(id, &r));
  TEST_ASSERT_EQUAL_STRING("q1", id);
}

static void test_clear_purges_queued_request(void) {
  char id[64];
  alert_req_t r;
  raise("a", AL_ALARM, 100);
  alerts_request("a", AL_REQ_ACK);
  alerts_clear("a");
  raise("a", AL_ALARM, 200);
  TEST_ASSERT_FALSE(alerts_next_request(id, &r));
  alerts_request("a", AL_REQ_SILENCE);
  raise("a", AL_NORMAL, 300);
  raise("a", AL_ALARM, 400);
  TEST_ASSERT_FALSE(alerts_next_request(id, &r));
  TEST_ASSERT_FALSE(get("a").acked);
}

static void test_eviction_purges_queued_request(void) {
  char id[64], n[16];
  alert_req_t r;
  for (int i = 0; i < ALERTS_MAX; i++) { snprintf(n, sizeof n, "w%d", i); raise(n, AL_WARN, 100 + i); }
  alerts_request("w0", AL_REQ_ACK);   // w0 is the oldest warn, the eviction victim
  raise("alarm", AL_ALARM, 500);
  TEST_ASSERT_FALSE(alerts_next_request(id, &r));
}

static alerts_snapshot_t snap(void) { alerts_snapshot_t s; alerts_snapshot(&s); return s; }

static void test_buzzer_pattern(void) {
  raise("a", AL_ALARM, 0);
  alerts_snapshot_t s = snap();
  TEST_ASSERT_TRUE(alerts_buzzer_on(&s, true, 0));
  TEST_ASSERT_FALSE(alerts_buzzer_on(&s, true, 300));
  TEST_ASSERT_TRUE(alerts_buzzer_on(&s, true, 1000));
  TEST_ASSERT_FALSE(alerts_buzzer_on(&s, false, 0));
  alerts_request("a", AL_REQ_SILENCE);
  s = snap();
  TEST_ASSERT_FALSE(alerts_buzzer_on(&s, true, 0));
  alerts_init(); alerts_source_up(true);
  raise("a", AL_ALARM, 0);
  alerts_request("a", AL_REQ_ACK);
  s = snap();
  TEST_ASSERT_FALSE(alerts_buzzer_on(&s, true, 0));
  alerts_init(); alerts_source_up(true);
  raise_ex("a", AL_ALARM, false, false, "u", 0);
  s = snap();
  TEST_ASSERT_FALSE(alerts_buzzer_on(&s, true, 0));
  alerts_init(); alerts_source_up(true);
  raise("w", AL_WARN, 0);
  s = snap();
  TEST_ASSERT_FALSE(alerts_buzzer_on(&s, true, 0));
  alerts_init(); alerts_source_up(true);
  raise("e", AL_EMERGENCY, 0);
  s = snap();
  TEST_ASSERT_TRUE(alerts_buzzer_on(&s, true, 0));
  TEST_ASSERT_FALSE(alerts_buzzer_on(&s, true, 200));
  TEST_ASSERT_TRUE(alerts_buzzer_on(&s, true, 300));
}

// An ACK on an alarm another display already acknowledged must not be sent (the server
// refuses it) and must not show ACK NOT SENT.
static void test_ack_on_acked_alarm_sends_nothing(void) {
  raise_ex("a", AL_ALARM, true, true, "uuid", 0);   // the server says acknowledged
  alerts_request("a", AL_REQ_ACK);
  char id[64];
  alert_req_t req;
  TEST_ASSERT_FALSE(alerts_next_request(id, &req));
  TEST_ASSERT_FALSE(get("a").send_failed);
  raise("b", AL_ALARM, 0);
  alerts_request("b", AL_REQ_ACK);
  TEST_ASSERT_TRUE(alerts_next_request(id, &req));
  alerts_request("b", AL_REQ_ACK);   // tapped again: already acknowledged here
  TEST_ASSERT_FALSE(alerts_next_request(id, &req));
}

static void test_silence_on_silenced_alarm_sends_nothing(void) {
  raise("a", AL_ALARM, 0);
  alerts_request("a", AL_REQ_SILENCE);
  char id[64];
  alert_req_t req;
  TEST_ASSERT_TRUE(alerts_next_request(id, &req));
  alerts_request("a", AL_REQ_SILENCE);
  TEST_ASSERT_FALSE(alerts_next_request(id, &req));
}

// The server reports the alarm acknowledged: every display knows, so nothing is "not sent".
static void test_source_ack_clears_send_failed(void) {
  raise("a", AL_ALARM, 0);
  alerts_request("a", AL_REQ_ACK);
  char id[64];
  alert_req_t req;
  alerts_next_request(id, &req);
  alerts_request_result("a", false);
  TEST_ASSERT_TRUE(get("a").send_failed);
  raise_ex("a", AL_ALARM, true, true, "uuid", 0);
  TEST_ASSERT_FALSE(get("a").send_failed);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_upsert_and_clear);
  RUN_TEST(test_normal_clears);
  RUN_TEST(test_sorted_by_severity_then_newest);
  RUN_TEST(test_escalation_resets_ack);
  RUN_TEST(test_same_state_keeps_ack);
  RUN_TEST(test_source_ack_merges_with_local);
  RUN_TEST(test_source_down_clears_and_flags);
  RUN_TEST(test_overflow_drops_least_severe);
  RUN_TEST(test_for_path);
  RUN_TEST(test_get_missing);
  RUN_TEST(test_request_while_down_marks_failed);
  RUN_TEST(test_request_without_ref_marks_failed);
  RUN_TEST(test_request_round_trip);
  RUN_TEST(test_queue_overflow_marks_oldest_failed);
  RUN_TEST(test_clear_purges_queued_request);
  RUN_TEST(test_eviction_purges_queued_request);
  RUN_TEST(test_buzzer_pattern);
  RUN_TEST(test_ack_on_acked_alarm_sends_nothing);
  RUN_TEST(test_silence_on_silenced_alarm_sends_nothing);
  RUN_TEST(test_source_ack_clears_send_failed);
  return UNITY_END();
}
