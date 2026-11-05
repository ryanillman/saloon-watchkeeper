#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "alerts.h"
#include "anchor_record.h"
#include "anchor_state.h"
#include "anchor_watch.h"

#define WATCH_ID "navigation.anchor.watch"
static char dir[64];
static anchor_watch_t w;

void setUp(void) {
  strcpy(dir, "/tmp/bi_watch_XXXXXX");
  TEST_ASSERT_NOT_NULL(mkdtemp(dir));
  alerts_init();
  alerts_source_up(true);
}
void tearDown(void) {
  char cmd[96];
  snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
  system(cmd);
}

static anchor_view_t view(anchor_mode_t m, double limit) {
  anchor_view_t v;
  memset(&v, 0, sizeof v);
  v.mode = m;
  v.limit_m = limit;
  v.distance_m = 20;
  return v;
}
static void tick(anchor_mode_t m, int64_t now, double limit) {
  anchor_view_t v = view(m, limit);
  anchor_watch_tick(&w, &v, m != AM_NO_DATA && m != AM_RESTORING, now, dir);
}
static void arm_on_disk(double limit) {
  anchor_record_t r = {true, limit, 0};
  TEST_ASSERT_TRUE(anchor_record_save(dir, &r));
}
static bool on_disk_armed(void) {
  anchor_record_t r = {0};
  return anchor_record_load(dir, &r) && r.armed;
}
static bool alarm_up(const char *words) {
  alert_t a;
  return alerts_get(WATCH_ID, &a) && a.state == AL_ALARM && a.audible && strstr(a.message, words);
}

static void test_not_armed_no_alert(void) {
  anchor_watch_init(&w, dir, 0, 60000);
  TEST_ASSERT_FALSE(anchor_watch_restoring(&w));
  tick(AM_NOT_SET, 5000, 0);
  tick(AM_NO_DATA, 500000, 0);   // never had an anchor: nothing to lose
  alert_t a;
  TEST_ASSERT_FALSE(alerts_get(WATCH_ID, &a));
}

static void test_armed_boot_server_reports_none_raises_not_restored(void) {
  arm_on_disk(30);
  anchor_watch_init(&w, dir, 0, 60000);
  TEST_ASSERT_TRUE(anchor_watch_restoring(&w));
  tick(AM_NOT_SET, 5000, 0);   // fresh data from a live server: no anchor
  TEST_ASSERT_TRUE(alarm_up("NOT RESTORED"));
  TEST_ASSERT_TRUE(on_disk_armed());   // the record is not silently disarmed (this also covers an anchor raised elsewhere)
}

static void test_armed_boot_no_data_for_120s_raises_not_restored(void) {
  arm_on_disk(30);
  anchor_watch_init(&w, dir, 1000, 60000);
  tick(AM_RESTORING, 120999, 0);   // 119999 ms after boot
  alert_t a;
  TEST_ASSERT_FALSE(alerts_get(WATCH_ID, &a));
  tick(AM_RESTORING, 121000, 0);   // 120 s
  TEST_ASSERT_TRUE(alarm_up("NOT RESTORED"));
}

static void test_restart_gap_does_not_alarm_while_restoring(void) {
  arm_on_disk(30);
  anchor_watch_init(&w, dir, 0, 60000);
  tick(AM_RESTORING, 10000, 0);
  tick(AM_RESTORING, 60000, 0);   // the plugin is still catching up after a restart
  alert_t a;
  TEST_ASSERT_FALSE(alerts_get(WATCH_ID, &a));
  TEST_ASSERT_TRUE(anchor_watch_restoring(&w));
}

static void test_armed_boot_restored_clears_and_keeps_record(void) {
  arm_on_disk(30);
  anchor_watch_init(&w, dir, 0, 60000);
  tick(AM_NOT_SET, 5000, 0);
  TEST_ASSERT_TRUE(alarm_up("NOT RESTORED"));
  tick(AM_HOLDING, 8000, 30);   // the plugin catches up after all
  alert_t a;
  TEST_ASSERT_FALSE(alerts_get(WATCH_ID, &a));
  TEST_ASSERT_FALSE(anchor_watch_restoring(&w));
  TEST_ASSERT_TRUE(on_disk_armed());
}

static void test_ack_leaves_record_armed(void) {
  arm_on_disk(30);
  anchor_watch_init(&w, dir, 0, 60000);
  tick(AM_NOT_SET, 5000, 0);
  alerts_request(WATCH_ID, AL_REQ_ACK);
  tick(AM_NOT_SET, 6000, 0);
  alert_t a;
  TEST_ASSERT_TRUE(alerts_get(WATCH_ID, &a));
  TEST_ASSERT_TRUE(a.acked);          // still there, silenced
  TEST_ASSERT_TRUE(on_disk_armed());  // but the record is kept until the user raises or drops
  TEST_ASSERT_TRUE(anchor_watch_restoring(&w));
}

static void test_user_clear_disarms_and_writes(void) {
  arm_on_disk(30);
  anchor_watch_init(&w, dir, 0, 60000);
  tick(AM_NOT_SET, 5000, 0);
  anchor_watch_user_cleared(&w, dir);
  alert_t a;
  TEST_ASSERT_FALSE(alerts_get(WATCH_ID, &a));
  TEST_ASSERT_FALSE(anchor_watch_restoring(&w));
  TEST_ASSERT_FALSE(on_disk_armed());
}

static void test_record_written_only_on_live_fresh_transition(void) {
  anchor_watch_init(&w, dir, 0, 60000);
  tick(AM_NOT_SET, 1000, 0);
  TEST_ASSERT_EQUAL_UINT32(0, w.writes);
  tick(AM_HOLDING, 2000, 50);   // an anchor appears
  TEST_ASSERT_EQUAL_UINT32(1, w.writes);
  tick(AM_HOLDING, 3000, 50);   // steady
  TEST_ASSERT_EQUAL_UINT32(1, w.writes);
  tick(AM_HOLDING, 4000, 60);   // the limit changed
  TEST_ASSERT_EQUAL_UINT32(2, w.writes);
  tick(AM_NO_DATA, 5000, 0);    // stale or disconnected: never written
  tick(AM_NO_DATA, 9000, 0);
  TEST_ASSERT_EQUAL_UINT32(2, w.writes);
  tick(AM_NOT_SET, 10000, 0);   // the server says the anchor is up
  TEST_ASSERT_EQUAL_UINT32(3, w.writes);
  TEST_ASSERT_FALSE(on_disk_armed());
}

static void test_watch_lost_after_60s_stale_and_clears_on_resume(void) {
  anchor_watch_init(&w, dir, 0, 60000);
  tick(AM_HOLDING, 1000, 30);
  tick(AM_NO_DATA, 5000, 0);   // data went stale at 5 s
  tick(AM_NO_DATA, 64999, 0);
  alert_t a;
  TEST_ASSERT_FALSE(alerts_get(WATCH_ID, &a));
  tick(AM_NO_DATA, 65000, 0);
  TEST_ASSERT_TRUE(alarm_up("WATCH LOST"));
  tick(AM_HOLDING, 70000, 30);
  TEST_ASSERT_FALSE(alerts_get(WATCH_ID, &a));
}

static void test_lost_alarm_survives_source_down(void) {
  anchor_watch_init(&w, dir, 0, 60000);
  tick(AM_HOLDING, 1000, 30);
  tick(AM_NO_DATA, 5000, 0);
  tick(AM_NO_DATA, 65000, 0);
  alerts_source_up(false);   // the connection is what was lost
  TEST_ASSERT_TRUE(alarm_up("WATCH LOST"));
}

static void test_no_sd_disables_boot_check_but_not_watch_lost(void) {
  arm_on_disk(30);
  anchor_watch_init(&w, NULL, 0, 60000);   // no card
  TEST_ASSERT_FALSE(w.have_sd);
  TEST_ASSERT_FALSE(anchor_watch_restoring(&w));
  anchor_view_t v = view(AM_NOT_SET, 0);
  anchor_watch_tick(&w, &v, true, 5000, NULL);
  alert_t a;
  TEST_ASSERT_FALSE(alerts_get(WATCH_ID, &a));   // no boot check
  v = view(AM_HOLDING, 30);
  anchor_watch_tick(&w, &v, true, 6000, NULL);
  v = view(AM_NO_DATA, 0);
  anchor_watch_tick(&w, &v, false, 7000, NULL);
  anchor_watch_tick(&w, &v, false, 67000, NULL);
  TEST_ASSERT_TRUE(alarm_up("WATCH LOST"));
  TEST_ASSERT_EQUAL_UINT32(0, w.writes);
}

static void test_raised_elsewhere_while_watching_disarms_quietly(void) {
  anchor_watch_init(&w, dir, 0, 60000);
  tick(AM_HOLDING, 1000, 30);
  TEST_ASSERT_TRUE(on_disk_armed());
  tick(AM_NOT_SET, 2000, 0);   // someone raised it from the plugin's own page while we watched
  alert_t a;
  TEST_ASSERT_FALSE(alerts_get(WATCH_ID, &a));
  TEST_ASSERT_FALSE(on_disk_armed());
}

static void test_write_failure_turns_off_the_sd_flag(void) {
  anchor_watch_init(&w, dir, 0, 60000);
  char gone[96];
  snprintf(gone, sizeof gone, "%s/missing", dir);
  anchor_view_t v = view(AM_HOLDING, 30);
  anchor_watch_tick(&w, &v, true, 1000, gone);
  TEST_ASSERT_FALSE(w.have_sd);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_not_armed_no_alert);
  RUN_TEST(test_armed_boot_server_reports_none_raises_not_restored);
  RUN_TEST(test_armed_boot_no_data_for_120s_raises_not_restored);
  RUN_TEST(test_restart_gap_does_not_alarm_while_restoring);
  RUN_TEST(test_armed_boot_restored_clears_and_keeps_record);
  RUN_TEST(test_ack_leaves_record_armed);
  RUN_TEST(test_user_clear_disarms_and_writes);
  RUN_TEST(test_record_written_only_on_live_fresh_transition);
  RUN_TEST(test_watch_lost_after_60s_stale_and_clears_on_resume);
  RUN_TEST(test_lost_alarm_survives_source_down);
  RUN_TEST(test_no_sd_disables_boot_check_but_not_watch_lost);
  RUN_TEST(test_raised_elsewhere_while_watching_disarms_quietly);
  RUN_TEST(test_write_failure_turns_off_the_sd_flag);
  return UNITY_END();
}
