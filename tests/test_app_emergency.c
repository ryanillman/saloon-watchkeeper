// The emergency buttons as the app wires them: availability, the press, the answer, the failures.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "alerts.h"
#include "app.h"
#include "boatdata.h"
#include "config.h"
#include "emergency.h"
#include "platform.h"
#include "sk_cmd.h"
#include "sk_parse.h"

static uint8_t buf[800 * 48 * 2];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  lv_display_flush_ready(d);
}
static lv_display_t *disp;
static bd_id_t pos_id;

static void add_navigation(void) {
  cfg_t next = *app_config();
  memset(&next.roles[next.role_count], 0, sizeof next.roles[0]);
  strcpy(next.roles[next.role_count++].role, "navigation");
  app_apply_config(&next);
}

static void set_pos(double lat, double lon) { bd_set_position(pos_id, lat, lon, BD_SRC_SK, platform_millis()); }

void setUp(void) {
  cfg_report_t rep;
  sk_cmd_init();
  app_init(NULL, &rep);
  app_bind();
  app_start_ui(disp, SIZE_WIDE);
  add_navigation();
  alerts_source_up(true);
  sk_cmd_set_connected(true);
  pos_id = bd_intern("navigation.position");
  set_pos(41.5, -70.25);
}
void tearDown(void) {}

static em_state_t snap(void) {
  em_state_t s;
  em_snapshot(&s);
  return s;
}

// The oldest queued command, as the Signal K task would take it.
static bool take(sk_cmd_t *c, sk_cmd_handle_t *h) { return sk_cmd_next(c, h, platform_millis()); }

static void answer_all(int status) {
  sk_cmd_t c;
  sk_cmd_handle_t h;
  while (take(&c, &h)) sk_cmd_finish(h, true, status);
}

static bool send_alert(alert_t *a) { return alerts_get("device.emergency.send", a); }

static void test_press_not_available_when_signalk_down(void) {
  alerts_source_up(false);
  TEST_ASSERT_FALSE(app_emergency_available());
  TEST_ASSERT_FALSE(app_emergency_press(EM_MOB));
  TEST_ASSERT_FALSE(em_active());
  sk_cmd_t c;
  sk_cmd_handle_t h;
  TEST_ASSERT_FALSE(take(&c, &h));
}

static void test_not_available_without_navigation_role(void) {
  cfg_report_t rep;
  app_init(NULL, &rep);   // the default roles have no Navigation
  app_bind();
  app_start_ui(disp, SIZE_WIDE);
  alerts_source_up(true);
  TEST_ASSERT_FALSE(app_emergency_available());
}

static void test_press_submits_notification_and_waypoint(void) {
  TEST_ASSERT_TRUE(app_emergency_press(EM_MOB));
  sk_cmd_t c;
  sk_cmd_handle_t h;
  TEST_ASSERT_TRUE(take(&c, &h));
  TEST_ASSERT_EQUAL_STRING("/signalk/v2/api/notifications/mob", c.path);
  TEST_ASSERT_EQUAL_UINT16(3000, c.expire_ms);
  TEST_ASSERT_NOT_NULL(strstr(c.body, "\"state\":\"emergency\""));
  TEST_ASSERT_NOT_NULL(strstr(c.body, "\"latitude\":41.500000"));
  sk_cmd_finish(h, true, 200);
  TEST_ASSERT_TRUE(take(&c, &h));
  TEST_ASSERT_EQUAL_STRING("/signalk/v2/api/resources/waypoints", c.path);
  TEST_ASSERT_NOT_NULL(strstr(c.body, "\"coordinates\":[-70.250000,41.500000]"));
  sk_cmd_finish(h, true, 201);
  app_tick();
  em_state_t s = snap();
  TEST_ASSERT_EQUAL(EM_SENT, s.send);
  TEST_ASSERT_EQUAL(EM_SENT, s.waypoint_send);
  alert_t a;
  TEST_ASSERT_FALSE(send_alert(&a));
}

static void test_press_updates_state_even_when_send_fails(void) {
  TEST_ASSERT_TRUE(app_emergency_press(EM_MOB));
  answer_all(500);
  app_tick();
  em_state_t s = snap();
  TEST_ASSERT_EQUAL(EM_MOB, s.nature);
  TEST_ASSERT_EQUAL(EM_ORIGIN_LOCAL, s.origin);
  TEST_ASSERT_EQUAL(EM_NOT_SENT, s.send);
  TEST_ASSERT_TRUE(s.has_pos);
  alert_t a;
  TEST_ASSERT_TRUE(send_alert(&a));
  TEST_ASSERT_EQUAL(AL_ALARM, a.state);
  TEST_ASSERT_NOT_NULL(strstr(a.message, "NOT SENT"));
  TEST_ASSERT_NOT_NULL(strstr(a.message, "TELL HELM"));
}

static void test_no_write_access_is_named(void) {
  TEST_ASSERT_TRUE(app_emergency_press(EM_FIRE));
  answer_all(403);
  app_tick();
  alert_t a;
  TEST_ASSERT_TRUE(send_alert(&a));
  TEST_ASSERT_NOT_NULL(strstr(a.message, "NO WRITE ACCESS"));
}

static void test_drop_mid_send_ends_not_sent(void) {
  TEST_ASSERT_TRUE(app_emergency_press(EM_MOB));
  sk_cmd_t c;
  sk_cmd_handle_t h;
  TEST_ASSERT_TRUE(take(&c, &h));   // handed to the HTTP client, no answer yet
  app_tick();
  TEST_ASSERT_EQUAL(EM_SENDING, snap().send);
  sk_cmd_set_connected(false);       // the connection goes away mid-send
  app_tick();
  TEST_ASSERT_EQUAL(EM_NOT_SENT, snap().send);
  alert_t a;
  TEST_ASSERT_TRUE(send_alert(&a));
}

static void test_refused_submit_is_not_sent(void) {
  sk_cmd_set_connected(false);   // Signal K connected as far as alerts know, but the command path is down
  TEST_ASSERT_TRUE(app_emergency_press(EM_MOB));
  em_state_t s = snap();
  TEST_ASSERT_EQUAL(EM_MOB, s.nature);
  TEST_ASSERT_EQUAL(EM_NOT_SENT, s.send);
  alert_t a;
  TEST_ASSERT_TRUE(send_alert(&a));
}

static void test_cancel_success_clears_state_and_alert(void) {
  TEST_ASSERT_TRUE(app_emergency_press(EM_MOB));
  answer_all(200);
  app_tick();
  TEST_ASSERT_TRUE(app_emergency_cancel());
  sk_cmd_t c;
  sk_cmd_handle_t h;
  TEST_ASSERT_TRUE(take(&c, &h));
  TEST_ASSERT_EQUAL_STRING("/signalk/v2/api/notifications/mob", c.path);
  TEST_ASSERT_NOT_NULL(strstr(c.body, "\"state\":\"normal\""));
  sk_cmd_finish(h, true, 200);
  app_tick();
  TEST_ASSERT_FALSE(em_active());
}

static void test_cancel_that_cannot_be_sent_keeps_the_emergency(void) {
  TEST_ASSERT_TRUE(app_emergency_press(EM_MOB));
  answer_all(200);
  app_tick();
  sk_cmd_set_connected(false);
  TEST_ASSERT_FALSE(app_emergency_cancel());
  TEST_ASSERT_TRUE(em_active());
  TEST_ASSERT_EQUAL(EM_NOT_SENT, snap().send);
  alert_t a;
  TEST_ASSERT_TRUE(send_alert(&a));
  sk_cmd_set_connected(true);
  TEST_ASSERT_TRUE(app_emergency_cancel());
  answer_all(500);   // the server refused the cancel
  app_tick();
  TEST_ASSERT_TRUE(em_active());
}

static void test_second_press_after_failure_is_a_new_capture(void) {
  TEST_ASSERT_TRUE(app_emergency_press(EM_MOB));
  answer_all(500);
  app_tick();
  set_pos(42.0, -71.0);
  TEST_ASSERT_TRUE(app_emergency_press(EM_MOB));
  em_state_t s = snap();
  TEST_ASSERT_EQUAL_DOUBLE(42.0, s.lat);
  TEST_ASSERT_EQUAL_DOUBLE(-71.0, s.lon);
  TEST_ASSERT_EQUAL(EM_SENDING, s.send);
}

static void test_second_press_while_sending_is_ignored(void) {
  TEST_ASSERT_TRUE(app_emergency_press(EM_MOB));
  set_pos(42.0, -71.0);
  TEST_ASSERT_FALSE(app_emergency_press(EM_MOB));
  TEST_ASSERT_EQUAL_DOUBLE(41.5, snap().lat);
}

static void test_waypoint_failure_does_not_fail_the_notification(void) {
  TEST_ASSERT_TRUE(app_emergency_press(EM_MOB));
  sk_cmd_t c;
  sk_cmd_handle_t h;
  TEST_ASSERT_TRUE(take(&c, &h));
  sk_cmd_finish(h, true, 200);
  TEST_ASSERT_TRUE(take(&c, &h));
  sk_cmd_finish(h, true, 404);
  app_tick();
  em_state_t s = snap();
  TEST_ASSERT_EQUAL(EM_SENT, s.send);
  TEST_ASSERT_EQUAL(EM_NOT_SENT, s.waypoint_send);
  alert_t a;
  TEST_ASSERT_FALSE(send_alert(&a));
}

static void test_no_fix_sends_no_position_and_no_waypoint(void) {
  bd_set_invalid(pos_id);
  TEST_ASSERT_TRUE(app_emergency_press(EM_MOB));
  TEST_ASSERT_FALSE(snap().has_pos);
  sk_cmd_t c;
  sk_cmd_handle_t h;
  TEST_ASSERT_TRUE(take(&c, &h));
  TEST_ASSERT_NULL(strstr(c.body, "position"));
  sk_cmd_finish(h, true, 200);
  TEST_ASSERT_FALSE(take(&c, &h));   // no waypoint without a position
}

static void test_incoming_notification_sets_the_state(void) {
  const char *j = "{\"updates\":[{\"values\":[{\"path\":\"notifications.mob\",\"value\":{\"state\":\"emergency\","
                  "\"message\":\"MAN OVERBOARD\",\"data\":{\"position\":{\"latitude\":12.5,\"longitude\":-45.5}}}}]}]}";
  sk_parse_message(j, strlen(j), 1000);
  em_state_t s = snap();
  TEST_ASSERT_EQUAL(EM_MOB, s.nature);
  TEST_ASSERT_EQUAL(EM_ORIGIN_NOTIFICATION, s.origin);
  TEST_ASSERT_EQUAL_DOUBLE(12.5, s.lat);
  const char *n = "{\"updates\":[{\"values\":[{\"path\":\"notifications.propulsion.port.temperature\",\"value\":{\"state\":\"alarm\"}}]}]}";
  em_reset();
  sk_parse_message(n, strlen(n), 1000);
  TEST_ASSERT_FALSE(em_active());   // not one of ours
}

static void test_position_is_subscribed_with_the_navigation_role(void) {
  sk_sub_t subs[64];
  size_t n = app_bound_ids(subs, 64);
  bool found = false;
  for (size_t i = 0; i < n; i++)
    if (subs[i].id == pos_id) found = true;
  TEST_ASSERT_TRUE(found);
}

static void test_alert_role_maps_emergency_ids_to_navigation(void) {
  int8_t nav = -1;
  for (uint8_t i = 0; i < app_config()->role_count; i++)
    if (!strcmp(app_config()->roles[i].role, "navigation")) nav = (int8_t)i;
  TEST_ASSERT_TRUE(nav >= 0);
  const char *ids[] = {"mob", "fire", "flooding", "sinking", "emergency"};
  for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) TEST_ASSERT_EQUAL_INT8(nav, app_alert_role(ids[i]));
  TEST_ASSERT_EQUAL_INT8(-1, app_alert_role("mobile"));   // the boundary check holds
  TEST_ASSERT_EQUAL_INT8(-1, app_alert_role("firewall"));
}

int main(void) {
  lv_init();
  disp = lv_display_create(800, 480);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  UNITY_BEGIN();
  RUN_TEST(test_press_not_available_when_signalk_down);
  RUN_TEST(test_not_available_without_navigation_role);
  RUN_TEST(test_press_submits_notification_and_waypoint);
  RUN_TEST(test_press_updates_state_even_when_send_fails);
  RUN_TEST(test_no_write_access_is_named);
  RUN_TEST(test_drop_mid_send_ends_not_sent);
  RUN_TEST(test_refused_submit_is_not_sent);
  RUN_TEST(test_cancel_success_clears_state_and_alert);
  RUN_TEST(test_cancel_that_cannot_be_sent_keeps_the_emergency);
  RUN_TEST(test_second_press_after_failure_is_a_new_capture);
  RUN_TEST(test_second_press_while_sending_is_ignored);
  RUN_TEST(test_waypoint_failure_does_not_fail_the_notification);
  RUN_TEST(test_no_fix_sends_no_position_and_no_waypoint);
  RUN_TEST(test_incoming_notification_sets_the_state);
  RUN_TEST(test_position_is_subscribed_with_the_navigation_role);
  RUN_TEST(test_alert_role_maps_emergency_ids_to_navigation);
  return UNITY_END();
}
