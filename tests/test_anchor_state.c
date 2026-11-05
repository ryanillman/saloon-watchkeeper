#include <math.h>
#include <string.h>
#include "unity.h"
#include "alerts.h"
#include "anchor_state.h"
#include "boatdata.h"
#include "history.h"

#define MIN_MS 60000LL
#define T0 (1000 * MIN_MS)
#define CAP 720

static anchor_ids_t ids;
static anchor_pt_t track[CAP];

void setUp(void) {
  bd_init();
  hist_init();
  alerts_init();
  alerts_source_up(true);
  anchor_ids_bind(&ids);
}
void tearDown(void) {}

static void num(bd_id_t id, double v, int64_t t, int64_t src_s) {
  bd_set_number(id, v, 1, t);
  if (src_s) bd_set_source_time(id, src_s);
}
static void set_pos(int64_t t, int64_t src_s) {
  bd_set_position(ids.position, 48.5, -123.0, 1, t);
  if (src_s) bd_set_source_time(ids.position, src_s);
}
// One minute of anchored life: distance and bearing recorded into history at minute m.
static void minute(int m, double dist, double bearing) {
  num(ids.current_radius, dist, T0 + m * MIN_MS, 0);
  num(ids.bearing_true, bearing, T0 + m * MIN_MS, 0);
  hist_tick(T0 + m * MIN_MS);
}
static void full(int64_t t) {   // a healthy anchored boat now
  set_pos(t, 0);
  num(ids.max_radius, 50, t, 0);
  num(ids.current_radius, 30, t, 0);
  num(ids.bearing_true, 1.0, t, 0);
}
static anchor_view_t view_at(bool restoring, bool live, int64_t now) {
  anchor_view_t v;
  anchor_view_build(&ids, restoring, live, now, &v, track, CAP);
  return v;
}
static void raise(alert_state_t st) {
  alert_t a = {0};
  strcpy(a.id, "navigation.anchor");
  a.state = st;
  strcpy(a.message, "Anchor Dragging");
  strcpy(a.ref, "u-1");
  alerts_upsert(&a, 0);
}

static void test_view_not_set(void) {
  bd_set_number(ids.position, NAN, 1, T0);   // the plugin says null
  TEST_ASSERT_EQUAL(AM_NOT_SET, view_at(false, true, T0).mode);
  bd_init();
  anchor_ids_bind(&ids);   // never published, but the server is up
  TEST_ASSERT_EQUAL(AM_NOT_SET, view_at(false, true, T0).mode);
}

static void test_view_holding_warn_dragging_from_alert(void) {
  full(T0);
  anchor_view_t v = view_at(false, true, T0);
  TEST_ASSERT_EQUAL(AM_HOLDING, v.mode);
  TEST_ASSERT_EQUAL_DOUBLE(30, v.distance_m);
  TEST_ASSERT_EQUAL_DOUBLE(50, v.limit_m);
  TEST_ASSERT_EQUAL_DOUBLE(1.0, v.bearing_rad);
  raise(AL_ALERT);
  TEST_ASSERT_EQUAL(AM_WARN, view_at(false, true, T0).mode);
  raise(AL_WARN);
  TEST_ASSERT_EQUAL(AM_WARN, view_at(false, true, T0).mode);
  raise(AL_ALARM);
  TEST_ASSERT_EQUAL(AM_DRAGGING, view_at(false, true, T0).mode);
  raise(AL_EMERGENCY);
  TEST_ASSERT_EQUAL(AM_DRAGGING, view_at(false, true, T0).mode);
}

static void test_view_stale_is_no_data(void) {
  full(T0);
  TEST_ASSERT_EQUAL(AM_HOLDING, view_at(false, true, T0 + 89000).mode);
  TEST_ASSERT_EQUAL(AM_NO_DATA, view_at(false, true, T0 + 91000).mode);   // 90 s stale limit
  TEST_ASSERT_EQUAL(AM_NO_DATA, view_at(false, false, T0).mode);          // not connected: nothing is known
}

static void test_view_no_limit(void) {
  full(T0);
  bd_set_number(ids.max_radius, NAN, 1, T0);
  anchor_view_t v = view_at(false, true, T0);
  TEST_ASSERT_EQUAL(AM_NO_LIMIT, v.mode);
  TEST_ASSERT_EQUAL_DOUBLE(30, v.distance_m);
  TEST_ASSERT_TRUE(isnan(v.limit_m));
}

static void test_view_restoring(void) {
  TEST_ASSERT_EQUAL(AM_RESTORING, view_at(true, false, T0).mode);   // nothing known yet
  TEST_ASSERT_EQUAL(AM_RESTORING, view_at(true, true, T0).mode);    // connected, no anchor data yet
  full(T0);
  TEST_ASSERT_EQUAL(AM_HOLDING, view_at(true, true, T0).mode);      // data is back: restored
}

static void test_view_zone_shape(void) {
  full(T0);
  anchor_view_t v = view_at(false, true, T0);
  TEST_ASSERT_TRUE(v.circle);   // no watchZone published (anchoralarm): a circle
  bd_set_text(ids.watch_zone, "polygon", 1, T0);
  v = view_at(false, true, T0);
  TEST_ASSERT_FALSE(v.circle);
  TEST_ASSERT_EQUAL_STRING("polygon", v.zone);
  bd_set_text(ids.watch_zone, "circle", 1, T0);
  TEST_ASSERT_TRUE(view_at(false, true, T0).circle);
}

static void test_view_optional_values(void) {
  full(T0);
  anchor_view_t v = view_at(false, true, T0);
  TEST_ASSERT_TRUE(isnan(v.warn_m));
  TEST_ASSERT_TRUE(isnan(v.rode_m));
  num(ids.warning_radius, 40, T0, 0);
  num(ids.rode_length, 25, T0, 0);
  v = view_at(false, true, T0);
  TEST_ASSERT_EQUAL_DOUBLE(40, v.warn_m);
  TEST_ASSERT_EQUAL_DOUBLE(25, v.rode_m);
}

static void test_sub_ids(void) {
  bd_id_t out[16];
  size_t n = anchor_sub_ids(&ids, out, 16);
  TEST_ASSERT_EQUAL_UINT(7, n);
  bool has_dist = false, has_bearing = false;
  for (size_t i = 0; i < n; i++) {
    has_dist = has_dist || out[i] == ids.current_radius;
    has_bearing = has_bearing || out[i] == ids.bearing_true;
  }
  TEST_ASSERT_TRUE(has_dist && has_bearing);
}

static void test_track_polar_to_xy_radians(void) {
  set_pos(T0, 0);
  num(ids.max_radius, 50, T0, 0);
  minute(0, 30, M_PI);   // bearing to the anchor is due south (pi rad): the boat is due north of it
  anchor_view_t v = view_at(false, true, T0);
  TEST_ASSERT_GREATER_THAN_UINT(0, v.track_n);
  anchor_pt_t last = track[v.track_n - 1];
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0, last.x);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 30, last.y);
  minute(1, 20, M_PI / 2);   // anchor due east: the boat is 20 m west of it
  v = view_at(false, true, T0 + MIN_MS);
  last = track[v.track_n - 1];
  TEST_ASSERT_FLOAT_WITHIN(0.01, -20, last.x);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0, last.y);
}

static void test_track_gap_breaks_line(void) {
  set_pos(T0, 0);
  num(ids.max_radius, 50, T0, 0);
  minute(0, 10, 0);
  for (int m = 1; m <= 4; m++) hist_tick(T0 + m * MIN_MS);   // no fresh values for minutes 2..4
  minute(5, 12, 0);
  anchor_view_t v = view_at(false, true, T0 + 5 * MIN_MS);
  int gaps = 0;
  for (size_t i = 0; i < v.track_n; i++) gaps += isnan(track[i].x);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(1, gaps);
  TEST_ASSERT_FALSE(isnan(track[v.track_n - 1].x));   // the live point is real
}

static void test_track_starts_at_set_time_and_caps_at_12h(void) {
  for (int m = 0; m < 800; m++) minute(m, 20, 0);
  int64_t S = 1793844000;
  set_pos(T0 + 799 * MIN_MS, S);
  num(ids.max_radius, 50, T0 + 799 * MIN_MS, 0);
  num(ids.current_radius, 20, T0 + 799 * MIN_MS, S + 600);   // set 10 minutes before the latest update
  anchor_view_t v = view_at(false, true, T0 + 799 * MIN_MS);
  TEST_ASSERT_EQUAL_INT64(600, v.set_ago_s);
  TEST_ASSERT_EQUAL_UINT(12, v.track_n);   // 11 history minutes (10 back plus now) and the live point
  num(ids.current_radius, 20, T0 + 799 * MIN_MS, S + 5 * 86400);   // set five days ago: capped
  v = view_at(false, true, T0 + 799 * MIN_MS);
  TEST_ASSERT_EQUAL_UINT(CAP, v.track_n);
}

static void test_track_unknown_set_time_uses_all_history(void) {
  set_pos(T0, 0);
  num(ids.max_radius, 50, T0, 0);
  for (int m = 0; m < 5; m++) minute(m, 20, 0);   // a reboot: little history, no Signal K timestamps
  anchor_view_t v = view_at(false, true, T0 + 4 * MIN_MS);
  TEST_ASSERT_EQUAL_INT64(-1, v.set_ago_s);
  TEST_ASSERT_EQUAL_UINT(6, v.track_n);   // five recorded minutes and the live point
}

static void test_max_is_largest_distance(void) {
  set_pos(T0, 0);
  num(ids.max_radius, 50, T0, 0);
  minute(0, 10, 0);
  minute(1, 40, 0);
  minute(2, 25, 0);
  anchor_view_t v = view_at(false, true, T0 + 2 * MIN_MS);
  TEST_ASSERT_EQUAL_DOUBLE(40, v.max_m);
  num(ids.current_radius, 45, T0 + 2 * MIN_MS, 0);   // the live distance counts too
  v = view_at(false, true, T0 + 2 * MIN_MS);
  TEST_ASSERT_EQUAL_DOUBLE(45, v.max_m);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_view_not_set);
  RUN_TEST(test_view_holding_warn_dragging_from_alert);
  RUN_TEST(test_view_stale_is_no_data);
  RUN_TEST(test_view_no_limit);
  RUN_TEST(test_view_restoring);
  RUN_TEST(test_view_zone_shape);
  RUN_TEST(test_view_optional_values);
  RUN_TEST(test_sub_ids);
  RUN_TEST(test_track_polar_to_xy_radians);
  RUN_TEST(test_track_gap_breaks_line);
  RUN_TEST(test_track_starts_at_set_time_and_caps_at_12h);
  RUN_TEST(test_track_unknown_set_time_uses_all_history);
  RUN_TEST(test_max_is_largest_distance);
  return UNITY_END();
}
