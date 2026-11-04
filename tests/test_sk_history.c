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

static void test_backfill_asks_once_per_tracked_path_and_fills_history(void) {
  hist_track(wind);
  hist_tick(1000 * 60000LL);   // the record is running
  reply = "{\"data\":[[\"t\",4],[\"t\",5],[\"t\",6]]}";
  sk_history_backfill("http://boat:3000", "tok");
  TEST_ASSERT_EQUAL_INT(1, gets);
  TEST_ASSERT_EQUAL_STRING(
      "http://boat:3000/signalk/v2/api/history/values?paths=environment.wind.speedTrue&duration=PT12H&resolution=60",
      last_url);
  float out[3];
  TEST_ASSERT_EQUAL_UINT(3, hist_read(wind, 3, out));
  TEST_ASSERT_EQUAL_FLOAT(4, out[0]);
  TEST_ASSERT_EQUAL_FLOAT(6, out[2]);   // the last row is now; the minute's own sample was a gap
}

static void test_backfill_with_nothing_tracked_makes_no_request(void) {
  sk_history_backfill("http://boat:3000", NULL);
  TEST_ASSERT_EQUAL_INT(0, gets);
}

static void test_backfill_ignores_an_error_page(void) {
  hist_track(wind);
  hist_tick(1000 * 60000LL);
  reply = "<html><body>Not Found</body></html>";
  sk_history_backfill("http://boat:3000", NULL);
  float out[1];
  TEST_ASSERT_EQUAL_UINT(1, hist_read(wind, 1, out));
  TEST_ASSERT_TRUE(isnan(out[0]));
}

static void test_backfill_stops_at_the_first_unanswered_request(void) {
  hist_track(wind);
  hist_track(depth);
  reply = NULL;   // the server does not answer
  sk_history_backfill("http://boat:3000", NULL);
  TEST_ASSERT_EQUAL_INT(1, gets);
}

static void test_backfill_skips_a_path_with_odd_characters(void) {
  hist_track(bd_intern("a.b c&d"));
  reply = "{\"data\":[[\"t\",1]]}";
  sk_history_backfill("http://boat:3000", NULL);
  TEST_ASSERT_EQUAL_INT(0, gets);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_reads_a_series_and_turns_null_into_a_gap);
  RUN_TEST(test_parse_picks_the_column_for_the_path_index);
  RUN_TEST(test_parse_short_rows_and_non_numbers_are_gaps);
  RUN_TEST(test_parse_keeps_the_newest_rows_when_there_are_too_many);
  RUN_TEST(test_parse_rejects_other_shapes);
  RUN_TEST(test_backfill_asks_once_per_tracked_path_and_fills_history);
  RUN_TEST(test_backfill_with_nothing_tracked_makes_no_request);
  RUN_TEST(test_backfill_ignores_an_error_page);
  RUN_TEST(test_backfill_stops_at_the_first_unanswered_request);
  RUN_TEST(test_backfill_skips_a_path_with_odd_characters);
  return UNITY_END();
}
