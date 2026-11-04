// The History API reply parser and the backfill requests, against a fake transport.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "boatdata.h"
#include "history.h"
#include "sk_history.h"
#include "sk_transport.h"
#include "unity.h"

static int gets;
static char last_url[512];
static const char *reply;   // body to return; NULL = no response at all
bool sk_http_get(const char *url, const char *token, char **body, size_t *len) {
  (void)token;
  gets++;
  snprintf(last_url, sizeof last_url, "%s", url);
  if (!reply) return false;
  *body = strdup(reply);
  *len = strlen(reply);
  return true;
}

static bd_id_t wind, depth;
void setUp(void) {
  bd_init();
  hist_init();
  wind = bd_intern("environment.wind.speedTrue");
  depth = bd_intern("environment.depth.belowTransducer");
  gets = 0;
  last_url[0] = 0;
  reply = NULL;
}
void tearDown(void) {}

static void test_parse_reads_a_series_and_turns_null_into_a_gap(void) {
  const char *j = "{\"values\":[{\"path\":\"a\"}],\"data\":[[\"2026-11-04T18:00:00Z\",1.0],"
                  "[\"2026-11-04T18:01:00Z\",null],[\"2026-11-04T18:02:00Z\",3.0]]}";
  float v[8];
  TEST_ASSERT_EQUAL_UINT(3, sk_history_parse(j, strlen(j), 0, v, 8));
  TEST_ASSERT_EQUAL_FLOAT(1, v[0]);
  TEST_ASSERT_TRUE(isnan(v[1]));
  TEST_ASSERT_EQUAL_FLOAT(3, v[2]);
}

static void test_parse_picks_the_column_for_the_path_index(void) {
  const char *j = "{\"values\":[{\"path\":\"a\"},{\"path\":\"b\"}],\"data\":[[\"t\",1,10],[\"t\",2,20]]}";
  float v[4];
  TEST_ASSERT_EQUAL_UINT(2, sk_history_parse(j, strlen(j), 1, v, 4));
  TEST_ASSERT_EQUAL_FLOAT(10, v[0]);
  TEST_ASSERT_EQUAL_FLOAT(20, v[1]);
}

static void test_parse_short_rows_and_non_numbers_are_gaps(void) {
  const char *j = "{\"data\":[[\"t\"],[\"t\",\"x\"],[\"t\",true],[\"t\",5]]}";
  float v[4];
  TEST_ASSERT_EQUAL_UINT(4, sk_history_parse(j, strlen(j), 0, v, 4));
  TEST_ASSERT_TRUE(isnan(v[0]) && isnan(v[1]) && isnan(v[2]));
  TEST_ASSERT_EQUAL_FLOAT(5, v[3]);
}

static void test_parse_keeps_the_newest_rows_when_there_are_too_many(void) {
  const char *j = "{\"data\":[[\"t\",1],[\"t\",2],[\"t\",3],[\"t\",4]]}";
  float v[2];
  TEST_ASSERT_EQUAL_UINT(2, sk_history_parse(j, strlen(j), 0, v, 2));
  TEST_ASSERT_EQUAL_FLOAT(3, v[0]);
  TEST_ASSERT_EQUAL_FLOAT(4, v[1]);
}

static void test_parse_rejects_other_shapes(void) {
  float v[4];
  const char *bad[] = {"", "<html>404</html>", "[]", "{}", "{\"data\":[]}", "{\"data\":\"x\"}", "{\"data\":[1,2]}"};
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, sk_history_parse(bad[i], strlen(bad[i]), 0, v, 4), bad[i]);
}

static void test_backfill_asks_for_one_path_per_call_and_fills_history(void) {
  hist_track(wind);
  hist_track(depth);
  hist_tick(1000 * 60000LL);   // the record is running
  reply = "{\"data\":[[\"t\",4],[\"t\",5],[\"t\",6]]}";
  TEST_ASSERT_EQUAL(SK_HIST_ASKED, sk_history_backfill("http://boat:3000", "tok"));
  TEST_ASSERT_EQUAL_INT(1, gets);
  TEST_ASSERT_EQUAL_STRING(
      "http://boat:3000/signalk/v2/api/history/values?paths=environment.wind.speedTrue&duration=PT12H&resolution=60",
      last_url);
  float out[3];
  TEST_ASSERT_EQUAL_UINT(3, hist_read(wind, 3, out));
  TEST_ASSERT_EQUAL_FLOAT(4, out[0]);
  TEST_ASSERT_EQUAL_FLOAT(6, out[2]);   // the last row is now; the minute's own sample was a gap
  TEST_ASSERT_EQUAL(SK_HIST_ASKED, sk_history_backfill("http://boat:3000", "tok"));   // the next path
  TEST_ASSERT_EQUAL_INT(2, gets);
  TEST_ASSERT_NOT_NULL(strstr(last_url, "paths=environment.depth.belowTransducer"));
  TEST_ASSERT_EQUAL(SK_HIST_IDLE, sk_history_backfill("http://boat:3000", "tok"));   // both done: no request
  TEST_ASSERT_EQUAL_INT(2, gets);
}

static void test_backfill_with_nothing_tracked_makes_no_request(void) {
  TEST_ASSERT_EQUAL(SK_HIST_IDLE, sk_history_backfill("http://boat:3000", NULL));
  TEST_ASSERT_EQUAL_INT(0, gets);
}

static void test_backfill_a_path_tracked_later_is_asked_for_later(void) {
  hist_track(wind);
  reply = "{\"data\":[[\"t\",1]]}";
  sk_history_backfill("http://boat:3000", NULL);
  TEST_ASSERT_EQUAL(SK_HIST_IDLE, sk_history_backfill("http://boat:3000", NULL));
  hist_track(depth);   // a rebind found a new trend path while connected
  TEST_ASSERT_EQUAL(SK_HIST_ASKED, sk_history_backfill("http://boat:3000", NULL));
  TEST_ASSERT_NOT_NULL(strstr(last_url, "belowTransducer"));
}

static void test_backfill_ignores_an_error_page_and_does_not_ask_again(void) {
  hist_track(wind);
  hist_tick(1000 * 60000LL);
  reply = "<html><body>Not Found</body></html>";
  TEST_ASSERT_EQUAL(SK_HIST_ASKED, sk_history_backfill("http://boat:3000", NULL));
  float out[1];
  TEST_ASSERT_EQUAL_UINT(1, hist_read(wind, 1, out));
  TEST_ASSERT_TRUE(isnan(out[0]));
  TEST_ASSERT_EQUAL(SK_HIST_IDLE, sk_history_backfill("http://boat:3000", NULL));   // an answer, even a useless one, is final
}

static void test_backfill_unanswered_is_tried_twice_then_given_up(void) {
  hist_track(wind);
  reply = NULL;   // the server does not answer
  TEST_ASSERT_EQUAL(SK_HIST_NO_ANSWER, sk_history_backfill("http://boat:3000", NULL));
  TEST_ASSERT_EQUAL(SK_HIST_NO_ANSWER, sk_history_backfill("http://boat:3000", NULL));
  TEST_ASSERT_EQUAL_INT(2, gets);
  TEST_ASSERT_EQUAL(SK_HIST_IDLE, sk_history_backfill("http://boat:3000", NULL));
  TEST_ASSERT_EQUAL_INT(2, gets);
}

static void test_backfill_skips_a_path_with_odd_characters_without_a_request(void) {
  hist_track(bd_intern("a.b c&d"));
  reply = "{\"data\":[[\"t\",1]]}";
  TEST_ASSERT_EQUAL(SK_HIST_IDLE, sk_history_backfill("http://boat:3000", NULL));
  TEST_ASSERT_EQUAL_INT(0, gets);
}

static void test_parse_a_full_twelve_hour_reply(void) {
  static char body[40000];
  size_t len = (size_t)snprintf(body, sizeof body, "{\"context\":\"vessels.self\",\"range\":{\"from\":\"a\",\"to\":\"b\"},"
                                                   "\"values\":[{\"path\":\"x\",\"method\":\"average\"},{\"path\":\"y\",\"method\":\"average\"}],\"data\":[");
  for (int i = 0; i < 720; i++)
    len += (size_t)snprintf(body + len, sizeof body - len, "%s[\"2026-11-04T%02d:%02d:00.000Z\",%.4f,%d]", i ? "," : "", i / 60, i % 60, 0.5 + i * 0.0005, i);
  len += (size_t)snprintf(body + len, sizeof body - len, "]}");
  static float v[720];
  TEST_ASSERT_EQUAL_UINT(720, sk_history_parse(body, len, 0, v, 720));
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 0.5, v[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 0.5 + 719 * 0.0005, v[719]);
  TEST_ASSERT_EQUAL_UINT(720, sk_history_parse(body, len, 1, v, 720));
  TEST_ASSERT_EQUAL_FLOAT(719, v[719]);
}

static void test_parse_finds_the_top_level_data_key_only(void) {
  const char *j = "{\"values\":[{\"data\":5,\"path\":\"d\"}],\"range\":{\"data\":[[\"t\",99]]},\"note\":\"\\\"data\\\"\","
                  "\"data\":[[\"t\",1],[\"t\",2]]}";
  float v[4];
  TEST_ASSERT_EQUAL_UINT(2, sk_history_parse(j, strlen(j), 0, v, 4));
  TEST_ASSERT_EQUAL_FLOAT(1, v[0]);
  TEST_ASSERT_EQUAL_FLOAT(2, v[1]);
}

static void test_parse_numbers_in_any_json_form_and_a_truncated_reply(void) {
  const char *j = "{\"data\":[[\"t\",-1.5e1],[\"t\", 2 ],[\"t\",0.25]]}";
  float v[4];
  TEST_ASSERT_EQUAL_UINT(3, sk_history_parse(j, strlen(j), 0, v, 4));
  TEST_ASSERT_EQUAL_FLOAT(-15, v[0]);
  TEST_ASSERT_EQUAL_FLOAT(2, v[1]);
  const char *cut = "{\"data\":[[\"t\",1],[\"t\",2";   // the connection dropped mid-body
  TEST_ASSERT_EQUAL_UINT(0, sk_history_parse(cut, strlen(cut), 0, v, 4));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_reads_a_series_and_turns_null_into_a_gap);
  RUN_TEST(test_parse_picks_the_column_for_the_path_index);
  RUN_TEST(test_parse_short_rows_and_non_numbers_are_gaps);
  RUN_TEST(test_parse_keeps_the_newest_rows_when_there_are_too_many);
  RUN_TEST(test_parse_rejects_other_shapes);
  RUN_TEST(test_backfill_asks_for_one_path_per_call_and_fills_history);
  RUN_TEST(test_backfill_with_nothing_tracked_makes_no_request);
  RUN_TEST(test_backfill_a_path_tracked_later_is_asked_for_later);
  RUN_TEST(test_backfill_ignores_an_error_page_and_does_not_ask_again);
  RUN_TEST(test_backfill_unanswered_is_tried_twice_then_given_up);
  RUN_TEST(test_backfill_skips_a_path_with_odd_characters_without_a_request);
  RUN_TEST(test_parse_a_full_twelve_hour_reply);
  RUN_TEST(test_parse_finds_the_top_level_data_key_only);
  RUN_TEST(test_parse_numbers_in_any_json_form_and_a_truncated_reply);
  return UNITY_END();
}
