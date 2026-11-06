#include <math.h>
#include <string.h>
#include "emergency.h"
#include "unity.h"

void setUp(void) { em_reset(); }
void tearDown(void) {}

static em_state_t snap(void) {
  em_state_t s;
  em_snapshot(&s);
  return s;
}

static void test_press_captures_and_sends(void) {
  em_press(EM_MOB, true, 41.5, -70.25, 1000, 1700000000);
  em_state_t s = snap();
  TEST_ASSERT_EQUAL(EM_MOB, s.nature);
  TEST_ASSERT_EQUAL(EM_ORIGIN_LOCAL, s.origin);
  TEST_ASSERT_EQUAL(EM_SENDING, s.send);
  TEST_ASSERT_TRUE(s.has_pos);
  TEST_ASSERT_EQUAL_DOUBLE(41.5, s.lat);
  TEST_ASSERT_EQUAL_DOUBLE(-70.25, s.lon);
  TEST_ASSERT_TRUE(em_active());
}

static void test_press_without_fix_has_no_position(void) {
  em_press(EM_MOB, false, NAN, NAN, 1000, 1700000000);
  em_state_t s = snap();
  TEST_ASSERT_FALSE(s.has_pos);
  char body[300];
  TEST_ASSERT_TRUE(em_raise_body(&s, body, sizeof body) > 0);
  TEST_ASSERT_NULL(strstr(body, "position"));
  char wp[300];
  TEST_ASSERT_EQUAL(0, em_waypoint_body(&s, wp, sizeof wp));
}

static void test_echo_never_overwrites_local_capture(void) {
  em_press(EM_MOB, true, 41.5, -70.25, 1000, 1700000000);
  em_on_notification(EM_MOB, true, true, 10.0, 10.0, NULL, 2000);
  em_state_t s = snap();
  TEST_ASSERT_EQUAL(EM_ORIGIN_LOCAL, s.origin);
  TEST_ASSERT_EQUAL_DOUBLE(41.5, s.lat);
  TEST_ASSERT_EQUAL_DOUBLE(-70.25, s.lon);
}

static void test_replay_adopts_payload_position(void) {
  em_on_notification(EM_MOB, true, true, 12.5, -45.5, NULL, 5000);
  em_state_t s = snap();
  TEST_ASSERT_EQUAL(EM_MOB, s.nature);
  TEST_ASSERT_EQUAL(EM_ORIGIN_NOTIFICATION, s.origin);
  TEST_ASSERT_TRUE(s.has_pos);
  TEST_ASSERT_EQUAL_DOUBLE(12.5, s.lat);
  em_reset();
  em_on_notification(EM_MOB, true, false, 0, 0, NULL, 5000);
  s = snap();
  TEST_ASSERT_EQUAL(EM_MOB, s.nature);
  TEST_ASSERT_FALSE(s.has_pos);   // never our own position at receipt
}

static void test_notification_normal_clears(void) {
  em_on_notification(EM_FIRE, true, false, 0, 0, NULL, 1000);
  TEST_ASSERT_TRUE(em_active());
  em_on_notification(EM_FIRE, false, false, 0, 0, NULL, 2000);
  TEST_ASSERT_FALSE(em_active());
  em_on_notification(EM_FIRE, true, false, 0, 0, NULL, 3000);
  em_on_notification(EM_FLOOD, false, false, 0, 0, NULL, 4000);   // another nature going normal leaves it
  TEST_ASSERT_TRUE(em_active());
}

static void test_cancel_not_sent_keeps_state(void) {
  em_press(EM_MOB, true, 41.5, -70.25, 1000, 1700000000);
  em_set_send(EM_NOT_SENT);   // a cancel that could not be sent
  TEST_ASSERT_TRUE(em_active());
  em_cancel_sent();
  TEST_ASSERT_FALSE(em_active());
}

static void test_stale_normal_does_not_clear_a_press_in_flight(void) {
  em_press(EM_MOB, true, 41.5, -70.25, 1000, 1700000000);   // the notification is still being sent
  em_on_notification(EM_MOB, false, false, 0, 0, NULL, 1100);      // an old "normal" replayed by the server
  TEST_ASSERT_TRUE(em_active());
  em_set_send(EM_SENT);
  em_on_notification(EM_MOB, false, false, 0, 0, NULL, 2000);      // once sent, a normal is a real clear (cancelled elsewhere)
  TEST_ASSERT_FALSE(em_active());
}

static void test_mob_is_not_displaced_by_another_nature(void) {
  em_press(EM_MOB, true, 41.5, -70.25, 1000, 1700000000);
  em_on_notification(EM_FIRE, true, false, 0, 0, NULL, 2000);
  TEST_ASSERT_EQUAL(EM_MOB, snap().nature);
}

static void ddm(double deg, bool lat, const char *want) {
  char out[24];
  em_format_ddm(deg, lat, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING(want, out);
}

static void test_ddm_rounding(void) {
  ddm(41.5402833, true, "41\xC2\xB0" "32.417'N");
  ddm(41.9999999, true, "42\xC2\xB0" "00.000'N");   // never 41 degrees 60.000 minutes
  ddm(-70.25, false, "070\xC2\xB0" "15.000'W");
  ddm(0.0, true, "00\xC2\xB0" "00.000'N");
  ddm(-0.0, true, "00\xC2\xB0" "00.000'N");
  ddm(-1e-9, true, "00\xC2\xB0" "00.000'N");       // rounds to zero: no hemisphere flip
  ddm(-33.8688, true, "33\xC2\xB0" "52.128'S");
  ddm(179.9999999, false, "180\xC2\xB0" "00.000'E");
}

static void test_raise_body_matches_the_servers_notifications_api(void) {
  em_press(EM_MOB, true, 41.5, -70.25, 1000, 1700000000);
  em_state_t s = snap();
  char body[300];
  TEST_ASSERT_TRUE(em_raise_body(&s, body, sizeof body) > 0);
  // POST /signalk/v2/api/notifications: state and message are required; path with idInPath gives notifications.mob.<uuid>
  TEST_ASSERT_NOT_NULL(strstr(body, "\"state\":\"emergency\""));
  TEST_ASSERT_NOT_NULL(strstr(body, "\"message\":\"MAN OVERBOARD\""));
  TEST_ASSERT_NOT_NULL(strstr(body, "\"path\":\"mob\""));
  TEST_ASSERT_NOT_NULL(strstr(body, "\"idInPath\":true"));
  TEST_ASSERT_NOT_NULL(strstr(body, "\"includeCreatedAt\":true"));
  TEST_ASSERT_NOT_NULL(strstr(body, "\"data\":{\"position\":{\"latitude\":41.500000,\"longitude\":-70.250000}"));
  TEST_ASSERT_NOT_NULL(strstr(body, "\"time\":\"2023-11-14T22:13:20Z\""));
  char tiny[10];
  TEST_ASSERT_EQUAL(0, em_raise_body(&s, tiny, sizeof tiny));   // overflow reports 0
}

static void test_fire_body_has_its_own_path_and_no_position(void) {
  em_press(EM_FIRE, true, 41.5, -70.25, 1000, 1700000000);
  em_state_t s = snap();
  char body[300];
  TEST_ASSERT_TRUE(em_raise_body(&s, body, sizeof body) > 0);
  TEST_ASSERT_NOT_NULL(strstr(body, "\"path\":\"fire\""));
  TEST_ASSERT_NOT_NULL(strstr(body, "\"message\":\"FIRE\""));
  TEST_ASSERT_NULL(strstr(body, "position"));
}

static void test_cancel_body_is_a_put_of_state_normal(void) {
  char body[40];
  TEST_ASSERT_TRUE(em_cancel_body(body, sizeof body) > 0);
  TEST_ASSERT_EQUAL_STRING("{\"state\":\"normal\"}", body);
}

static void test_server_id_is_learned_from_the_reply_or_the_echo(void) {
  em_press(EM_MOB, true, 41.5, -70.25, 1000, 1700000000);
  TEST_ASSERT_EQUAL_STRING("", snap().id);
  em_on_notification(EM_MOB, true, true, 10.0, 10.0, "uuid-from-echo", 2000);   // the echo: id only, not the position
  em_state_t s = snap();
  TEST_ASSERT_EQUAL_STRING("uuid-from-echo", s.id);
  TEST_ASSERT_EQUAL_DOUBLE(41.5, s.lat);
  em_set_server_id("uuid-from-reply");   // the POST reply is authoritative
  TEST_ASSERT_EQUAL_STRING("uuid-from-reply", snap().id);
  em_on_notification(EM_MOB, true, false, 0, 0, "uuid-from-echo", 3000);
  TEST_ASSERT_EQUAL_STRING("uuid-from-reply", snap().id);   // a later echo does not replace it
}

static void test_adopted_notification_keeps_its_id(void) {
  em_on_notification(EM_MOB, true, true, 12.5, -45.5, "uuid-1", 5000);
  TEST_ASSERT_EQUAL_STRING("uuid-1", snap().id);
}

static void test_normal_for_another_notification_does_not_clear(void) {
  em_on_notification(EM_MOB, true, true, 12.5, -45.5, "uuid-new", 5000);
  em_on_notification(EM_MOB, false, false, 0, 0, "uuid-old", 6000);   // an earlier MOB being cleared
  TEST_ASSERT_TRUE(em_active());
  em_on_notification(EM_MOB, false, false, 0, 0, "uuid-new", 7000);
  TEST_ASSERT_FALSE(em_active());
  em_on_notification(EM_FIRE, true, false, 0, 0, "uuid-f", 8000);
  em_on_notification(EM_FIRE, false, false, 0, 0, NULL, 9000);   // no id on the normal: it matches by nature
  TEST_ASSERT_FALSE(em_active());
}

static void test_waypoint_body_is_geojson_point_lon_first(void) {
  em_press(EM_MOB, true, 41.5, -70.25, 1000, 1700000000);
  em_state_t s = snap();
  char wp[300];
  TEST_ASSERT_TRUE(em_waypoint_body(&s, wp, sizeof wp) > 0);
  TEST_ASSERT_NOT_NULL(strstr(wp, "\"name\":\"MOB 22:13:20\""));
  TEST_ASSERT_NOT_NULL(strstr(wp, "\"coordinates\":[-70.250000,41.500000]"));
  TEST_ASSERT_NOT_NULL(strstr(wp, "\"type\":\"Point\""));
}

static void test_target_bearing_and_distance(void) {
  em_press(EM_MOB, true, 41.1, -70.0, 1000, 1700000000);
  em_state_t s = snap();
  double brg, dist;
  TEST_ASSERT_TRUE(em_target(&s, 41.0, -70.0, &brg, &dist));
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.0, brg);
  TEST_ASSERT_FLOAT_WITHIN(20.0, 11119.0, dist);
  TEST_ASSERT_TRUE(em_target(&s, 41.2, -70.0, &brg, &dist));   // MOB is now south
  TEST_ASSERT_FLOAT_WITHIN(0.001, 3.14159, brg);
  TEST_ASSERT_FALSE(em_target(&s, NAN, -70.0, &brg, &dist));
  em_press(EM_MOB, false, NAN, NAN, 2000, 1700000001);
  s = snap();
  TEST_ASSERT_FALSE(em_target(&s, 41.0, -70.0, &brg, &dist));
}

static void say(double deg, bool is_lat, const char *want) {
  char out[100];
  TEST_ASSERT_TRUE(em_speak_coordinate(deg, is_lat, out, sizeof out));
  TEST_ASSERT_EQUAL_STRING(want, out);
}

static void test_a_coordinate_is_spoken_digit_by_digit_in_degrees_and_minutes(void) {
  // Hundredths of a minute: about 60 ft north-south, which is what a call needs (a tenth would be 600 ft).
  say(48.5402833, true, "four eight degrees three two decimal four two minutes north");
  say(-123.1766667, false, "one two three degrees one zero decimal six zero minutes west");
  say(5.5, true, "zero five degrees three zero decimal zero zero minutes north");   // fixed width: no ambiguity
  say(-70.25, false, "zero seven zero degrees one five decimal zero zero minutes west");
  say(-33.8688, true, "three three degrees five two decimal one three minutes south");
  say(151.2093, false, "one five one degrees one two decimal five six minutes east");
  say(0.0, true, "zero zero degrees zero zero decimal zero zero minutes north");
  say(41.9999999, true, "four two degrees zero zero decimal zero zero minutes north");        // minutes never reach sixty
  say(-1e-9, true, "zero zero degrees zero zero decimal zero zero minutes north");             // rounds to zero: north,
  say(1e-9, false, "zero zero zero degrees zero zero decimal zero zero minutes east");         // not south or west
}

static void test_a_coordinate_refuses_what_it_cannot_say(void) {
  char out[100] = "x";
  TEST_ASSERT_FALSE(em_speak_coordinate(NAN, true, out, sizeof out));
  TEST_ASSERT_EQUAL_STRING("", out);
  TEST_ASSERT_FALSE(em_speak_coordinate(INFINITY, false, out, sizeof out));
  TEST_ASSERT_FALSE(em_speak_coordinate(48.5, true, out, 20));   // too small: nothing partial is left behind
  TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_script_is_broken_into_lines_to_read_aloud(void) {
  char out[600];
  TEST_ASSERT_TRUE(em_script_text(EM_MOB, "SALOON", true, 48.5402833, -123.1766667, out, sizeof out) > 0);
  TEST_ASSERT_EQUAL_STRING(
      "MAYDAY MAYDAY MAYDAY\n"
      "This is SALOON SALOON SALOON\n"
      "Position four eight degrees three two decimal four two minutes north\n"
      "one two three degrees one zero decimal six zero minutes west\n"
      "We have a man overboard. Over.",
      out);
}

static void test_script_has_a_sentence_for_every_nature(void) {
  const struct { em_nature_t n; const char *tail; } cases[] = {
    {EM_MOB, "We have a man overboard. Over."},   {EM_FIRE, "We have a fire on board. Over."},
    {EM_FLOOD, "We have flooding. Over."},        {EM_SINKING, "We are sinking. Over."},
    {EM_OTHER, "We have an emergency. Over."}};
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    char out[600];
    TEST_ASSERT_TRUE(em_script_text(cases[i].n, "SALOON", true, 41.5, -70.25, out, sizeof out) > 0);
    size_t len = strlen(out), tl = strlen(cases[i].tail);
    TEST_ASSERT_TRUE_MESSAGE(len > tl && !strcmp(out + len - tl, cases[i].tail), cases[i].tail);
  }
}

static void test_script_says_what_is_missing_instead_of_guessing(void) {
  char out[600];
  TEST_ASSERT_TRUE(em_script_text(EM_FIRE, "", false, 0, 0, out, sizeof out) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "This is (BOAT NAME, said three times)\n"));
  TEST_ASSERT_NOT_NULL(strstr(out, "Position (NO GPS FIX - read the position from the chartplotter)\nWe have a fire on board. Over."));
  TEST_ASSERT_NULL(strstr(out, "degrees"));   // one line: there is no longitude to say
  TEST_ASSERT_TRUE(em_script_text(EM_FIRE, NULL, true, 41.5, -70.25, out, sizeof out) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "(BOAT NAME"));
  TEST_ASSERT_NOT_NULL(strstr(out, "degrees"));
}

static void test_script_fits_the_longest_name_and_reports_overflow(void) {
  char name[32], out[600];
  memset(name, 'W', 31);
  name[31] = 0;
  TEST_ASSERT_TRUE(em_script_text(EM_MOB, name, true, -123.456789, -123.456789, out, sizeof out) > 0);
  TEST_ASSERT_EQUAL(0, em_script_text(EM_MOB, "SALOON", true, 41.5, -70.25, out, 50));   // overflow reports 0
  TEST_ASSERT_EQUAL(0, em_script_text(EM_NONE, "SALOON", true, 41.5, -70.25, out, sizeof out));   // no emergency, no call
}

static void test_nature_from_path_accepts_the_servers_id_suffix(void) {
  TEST_ASSERT_EQUAL(EM_MOB, em_nature_from_path("mob.7c0e98fc-2e6a-453c-a7d9-7cf715e2c47b"));   // notifications.mob.<uuid>
  TEST_ASSERT_EQUAL(EM_FIRE, em_nature_from_path("fire.abc"));
  TEST_ASSERT_EQUAL(EM_FLOOD, em_nature_from_path("flooding.a.b"));
  TEST_ASSERT_EQUAL(EM_NONE, em_nature_from_path("mobile"));
  TEST_ASSERT_EQUAL(EM_NONE, em_nature_from_path("firewall.x"));
  TEST_ASSERT_EQUAL(EM_NONE, em_nature_from_path("mob."));
}

static void test_nature_paths(void) {
  TEST_ASSERT_EQUAL(EM_MOB, em_nature_from_path("mob"));
  TEST_ASSERT_EQUAL(EM_FLOOD, em_nature_from_path("flooding"));
  TEST_ASSERT_EQUAL(EM_NONE, em_nature_from_path("mobile"));
  TEST_ASSERT_EQUAL(EM_NONE, em_nature_from_path("propulsion.port.temperature"));
  TEST_ASSERT_EQUAL_STRING("sinking", em_nature_path(EM_SINKING));
  TEST_ASSERT_EQUAL(EM_OTHER, em_nature_from_path(em_nature_path(EM_OTHER)));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_press_captures_and_sends);
  RUN_TEST(test_press_without_fix_has_no_position);
  RUN_TEST(test_echo_never_overwrites_local_capture);
  RUN_TEST(test_replay_adopts_payload_position);
  RUN_TEST(test_notification_normal_clears);
  RUN_TEST(test_cancel_not_sent_keeps_state);
  RUN_TEST(test_stale_normal_does_not_clear_a_press_in_flight);
  RUN_TEST(test_mob_is_not_displaced_by_another_nature);
  RUN_TEST(test_ddm_rounding);
  RUN_TEST(test_raise_body_matches_the_servers_notifications_api);
  RUN_TEST(test_fire_body_has_its_own_path_and_no_position);
  RUN_TEST(test_cancel_body_is_a_put_of_state_normal);
  RUN_TEST(test_server_id_is_learned_from_the_reply_or_the_echo);
  RUN_TEST(test_adopted_notification_keeps_its_id);
  RUN_TEST(test_normal_for_another_notification_does_not_clear);
  RUN_TEST(test_a_coordinate_is_spoken_digit_by_digit_in_degrees_and_minutes);
  RUN_TEST(test_a_coordinate_refuses_what_it_cannot_say);
  RUN_TEST(test_script_is_broken_into_lines_to_read_aloud);
  RUN_TEST(test_script_has_a_sentence_for_every_nature);
  RUN_TEST(test_script_says_what_is_missing_instead_of_guessing);
  RUN_TEST(test_script_fits_the_longest_name_and_reports_overflow);
  RUN_TEST(test_nature_from_path_accepts_the_servers_id_suffix);
  RUN_TEST(test_waypoint_body_is_geojson_point_lon_first);
  RUN_TEST(test_target_bearing_and_distance);
  RUN_TEST(test_nature_paths);
  return UNITY_END();
}
