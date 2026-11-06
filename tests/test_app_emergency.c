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
#include "emergency_page.h"
#include "role.h"
#include "platform.h"
#include "sk_cmd.h"
#include "sk_parse.h"
#include "ui_holdbtn.h"
#include "ui_quick.h"
#include "ui_shell.h"

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

static void answer_all_with_id(int status, const char *id) {
  sk_cmd_t c;
  sk_cmd_handle_t h;
  while (take(&c, &h)) sk_cmd_finish_id(h, true, status, id);
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
  TEST_ASSERT_EQUAL_STRING("/signalk/v2/api/notifications", c.path);   // the server's Notifications API, not a per-nature path
  TEST_ASSERT_EQUAL_STRING("", c.method);                               // a POST
  TEST_ASSERT_EQUAL_UINT16(3000, c.expire_ms);
  TEST_ASSERT_NOT_NULL(strstr(c.body, "\"state\":\"emergency\""));
  TEST_ASSERT_NOT_NULL(strstr(c.body, "\"path\":\"mob\""));
  TEST_ASSERT_NOT_NULL(strstr(c.body, "\"idInPath\":true"));
  TEST_ASSERT_NOT_NULL(strstr(c.body, "\"latitude\":41.500000"));
  sk_cmd_finish_id(h, true, 200, "7c0e98fc-2e6a-453c-a7d9-7cf715e2c47b");
  TEST_ASSERT_TRUE(take(&c, &h));
  TEST_ASSERT_EQUAL_STRING("/signalk/v2/api/resources/waypoints", c.path);
  TEST_ASSERT_NOT_NULL(strstr(c.body, "\"coordinates\":[-70.250000,41.500000]"));
  sk_cmd_finish_id(h, true, 201, "26d57d50-5339-4940-9371-5ae2e1ea316c");   // the waypoint's id must not become the MOB's
  app_tick();
  em_state_t s = snap();
  TEST_ASSERT_EQUAL(EM_SENT, s.send);
  TEST_ASSERT_EQUAL(EM_SENT, s.waypoint_send);
  TEST_ASSERT_EQUAL_STRING("7c0e98fc-2e6a-453c-a7d9-7cf715e2c47b", s.id);
  alert_t a;
  TEST_ASSERT_FALSE(send_alert(&a));
}

static void test_fire_goes_to_the_generic_endpoint_with_its_own_path(void) {
  TEST_ASSERT_TRUE(app_emergency_press(EM_FIRE));
  sk_cmd_t c;
  sk_cmd_handle_t h;
  TEST_ASSERT_TRUE(take(&c, &h));
  TEST_ASSERT_EQUAL_STRING("/signalk/v2/api/notifications", c.path);   // POST /notifications/fire is a 404 on a real server
  TEST_ASSERT_NOT_NULL(strstr(c.body, "\"path\":\"fire\""));
  TEST_ASSERT_NOT_NULL(strstr(c.body, "\"message\":\"FIRE\""));
  sk_cmd_finish_id(h, true, 200, "f-uuid");
  app_tick();
  TEST_ASSERT_EQUAL_STRING("f-uuid", snap().id);
  TEST_ASSERT_FALSE(take(&c, &h));   // no waypoint for a fire
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

static void test_cancel_puts_state_normal_to_the_notifications_id(void) {
  TEST_ASSERT_TRUE(app_emergency_press(EM_MOB));
  answer_all_with_id(200, "7c0e98fc-2e6a-453c-a7d9-7cf715e2c47b");
  app_tick();
  TEST_ASSERT_TRUE(app_emergency_cancel());
  sk_cmd_t c;
  sk_cmd_handle_t h;
  TEST_ASSERT_TRUE(take(&c, &h));
  TEST_ASSERT_EQUAL_STRING("/signalk/v2/api/notifications/7c0e98fc-2e6a-453c-a7d9-7cf715e2c47b", c.path);   // POST /notifications/mob would raise another MOB
  TEST_ASSERT_EQUAL_STRING("PUT", c.method);
  TEST_ASSERT_EQUAL_STRING("{\"state\":\"normal\"}", c.body);
  sk_cmd_finish(h, true, 200);
  app_tick();
  TEST_ASSERT_FALSE(em_active());
  alert_t a;
  TEST_ASSERT_FALSE(send_alert(&a));
}

static void test_cancel_without_a_known_id_is_refused_and_says_so(void) {
  TEST_ASSERT_TRUE(app_emergency_press(EM_MOB));   // not answered yet: the server has not told us the id
  TEST_ASSERT_FALSE(app_emergency_cancel());
  TEST_ASSERT_TRUE(em_active());
  sk_cmd_t c;
  sk_cmd_handle_t h;
  TEST_ASSERT_TRUE(take(&c, &h));   // only the raise is queued; nothing was sent for the cancel
  TEST_ASSERT_EQUAL_STRING("/signalk/v2/api/notifications", c.path);
  TEST_ASSERT_FALSE(take(&c, &h) && !strcmp(c.method, "PUT"));
  alert_t a;
  TEST_ASSERT_TRUE(send_alert(&a));
  TEST_ASSERT_NOT_NULL(strstr(a.message, "CANCEL NOT SENT"));
}

static void test_id_comes_from_the_echo_when_the_reply_had_none(void) {
  TEST_ASSERT_TRUE(app_emergency_press(EM_MOB));
  answer_all(200);   // a reply without an id
  app_tick();
  TEST_ASSERT_EQUAL_STRING("", snap().id);
  const char *j = "{\"updates\":[{\"values\":[{\"path\":\"notifications.mob.echo-uuid\",\"value\":{\"state\":\"emergency\","
                  "\"id\":\"echo-uuid\"}}]}]}";
  sk_parse_message(j, strlen(j), 1000);
  TEST_ASSERT_EQUAL_STRING("echo-uuid", snap().id);
  TEST_ASSERT_TRUE(app_emergency_cancel());
  sk_cmd_t c;
  sk_cmd_handle_t h;
  TEST_ASSERT_TRUE(take(&c, &h));
  TEST_ASSERT_EQUAL_STRING("/signalk/v2/api/notifications/echo-uuid", c.path);
}

static void test_a_mob_raised_elsewhere_is_cancelled_by_its_id(void) {
  const char *j = "{\"updates\":[{\"values\":[{\"path\":\"notifications.mob.7c0e98fc-2e6a-453c-a7d9-7cf715e2c47b\",\"value\":"
                  "{\"state\":\"emergency\",\"position\":{\"latitude\":12.5,\"longitude\":-45.5},"
                  "\"id\":\"7c0e98fc-2e6a-453c-a7d9-7cf715e2c47b\"}}]}]}";
  sk_parse_message(j, strlen(j), 1000);   // the helm's button, through the server's own MOB endpoint
  em_state_t s = snap();
  TEST_ASSERT_EQUAL(EM_MOB, s.nature);
  TEST_ASSERT_EQUAL_DOUBLE(12.5, s.lat);
  TEST_ASSERT_TRUE(app_emergency_cancel());
  sk_cmd_t c;
  sk_cmd_handle_t h;
  TEST_ASSERT_TRUE(take(&c, &h));
  TEST_ASSERT_EQUAL_STRING("/signalk/v2/api/notifications/7c0e98fc-2e6a-453c-a7d9-7cf715e2c47b", c.path);
  TEST_ASSERT_EQUAL_STRING("PUT", c.method);
}

static void test_cancel_that_cannot_be_sent_keeps_the_emergency(void) {
  TEST_ASSERT_TRUE(app_emergency_press(EM_MOB));
  answer_all_with_id(200, "uuid-1");
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

static void test_vessel_name_config_overrides_signalk(void) {
  const char *j = "{\"name\":\"FromSignalK\",\"mmsi\":\"316009999\"}";
  sk_parse_full(j, strlen(j));
  TEST_ASSERT_EQUAL_STRING("FromSignalK", app_vessel_name());
  TEST_ASSERT_EQUAL_STRING("316009999", app_vessel_mmsi());
  cfg_t next = *app_config();
  strcpy(next.vessel_name, "Saloon");
  app_apply_config(&next);
  TEST_ASSERT_EQUAL_STRING("Saloon", app_vessel_name());
}

static int8_t nav_idx(void) { return app_alert_role("mob"); }

static void test_script_page_reads_the_call_with_the_apps_vessel_name(void) {
  cfg_t next = *app_config();
  strcpy(next.vessel_name, "Saloon");
  app_apply_config(&next);
  ui_shell_goto((uint8_t)nav_idx(), 1);
  app_tick();
  TEST_ASSERT_NOT_NULL(emergency_page_part("script"));
  TEST_ASSERT_NOT_NULL(strstr(lv_label_get_text(emergency_page_part("script")), "No emergency declared"));
  TEST_ASSERT_TRUE(app_emergency_press(EM_FIRE));
  app_tick();
  TEST_ASSERT_NOT_NULL(strstr(lv_label_get_text(emergency_page_part("script")), "This is Saloon Saloon Saloon"));
  TEST_ASSERT_NOT_NULL(strstr(lv_label_get_text(emergency_page_part("script")), "We have a fire on board. Over."));
  ui_shell_goto(0, 0);
}

// ---- declaring from the quick panel ----

static uint32_t now_ms;
static uint32_t fake_tick(void) { return now_ms; }
static void run(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 50) {
    now_ms += 50;
    lv_timer_handler();
  }
}
static void open_quick(void) {
  lv_obj_send_event(ui_shell_strip_tap(), LV_EVENT_CLICKED, NULL);
  lv_timer_handler();
}
static void hold(ui_quick_part_t p, uint32_t ms) {
  lv_obj_t *b = ui_quick_part(p);
  TEST_ASSERT_NOT_NULL(b);
  lv_obj_send_event(b, LV_EVENT_PRESSED, NULL);
  run(ms);
  if (ui_quick_is_open()) lv_obj_send_event(b, LV_EVENT_RELEASED, NULL);   // a completed hold closed the panel and deleted the button
  run(100);
}
static void close_quick(void) {
  ui_quick_close();
  run(100);
}

static void test_quick_panel_has_the_emergency_row_only_when_it_can_send(void) {
  open_quick();
  TEST_ASSERT_NOT_NULL(ui_quick_part(UI_QUICK_BTN_MOB));
  TEST_ASSERT_NOT_NULL(ui_quick_part(UI_QUICK_BTN_FIRE));
  TEST_ASSERT_NOT_NULL(ui_quick_part(UI_QUICK_BTN_SINKING));   // no FLOOD or OTHER button
  close_quick();
  alerts_source_up(false);
  open_quick();
  TEST_ASSERT_NULL(ui_quick_part(UI_QUICK_BTN_MOB));   // Signal K is down: nothing could be sent
  close_quick();
}

// The initiating display goes where every other display goes: a MOB to the compass (the target to steer to), the rest to the script.
static void test_a_quick_hold_declares_and_goes_where_the_other_displays_go(void) {
  const struct { ui_quick_part_t part; em_nature_t nature; const char *path; uint8_t page; } cases[] = {
    {UI_QUICK_BTN_MOB, EM_MOB, "\"path\":\"mob\"", 0}, {UI_QUICK_BTN_FIRE, EM_FIRE, "\"path\":\"fire\"", 1},
    {UI_QUICK_BTN_SINKING, EM_SINKING, "\"path\":\"sinking\"", 1}};
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    em_reset();
    sk_cmd_init();
    {
      cfg_report_t rep;
      app_init(NULL, &rep);
      app_bind();
      app_start_ui(disp, SIZE_WIDE);
      add_navigation();
      alerts_source_up(true);
      sk_cmd_set_connected(true);   // after app_init, which resets the queue
      pos_id = bd_intern("navigation.position");
      set_pos(41.5, -70.25);
    }
    ui_shell_goto(0, 0);
    open_quick();
    hold(cases[i].part, 1100);
    em_state_t s = snap();
    TEST_ASSERT_EQUAL_MESSAGE(cases[i].nature, s.nature, "declared");
    TEST_ASSERT_EQUAL(EM_ORIGIN_LOCAL, s.origin);
    TEST_ASSERT_FALSE_MESSAGE(ui_quick_is_open(), "the panel closes");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)nav_idx(), ui_shell_nav()->role, "on the Navigation role");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(cases[i].page, ui_shell_nav()->page, "a MOB on the compass, the others on the script");
    // the same page the alert's GO button opens on any other display
    TEST_ASSERT_EQUAL_UINT8(role_find("navigation")->page_for(NULL, SIZE_WIDE, em_nature_path(cases[i].nature)), ui_shell_nav()->page);
    sk_cmd_t c;
    sk_cmd_handle_t h;
    TEST_ASSERT_TRUE(take(&c, &h));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(c.body, cases[i].path), c.body);
    sk_cmd_finish_id(h, true, 200, "uuid-q");
    app_tick();
  }
  ui_shell_goto(0, 0);
}
static bool sos_shown(void) {
  lv_obj_t *o = ui_shell_sos();
  return o && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void test_sos_shows_for_a_mob_and_goes_to_the_script(void) {
  app_tick();
  TEST_ASSERT_FALSE(sos_shown());
  em_on_notification(EM_FIRE, true, false, 0, 0, NULL, 1000);   // not a MOB
  app_tick();
  TEST_ASSERT_FALSE(sos_shown());
  em_reset();
  TEST_ASSERT_TRUE(app_emergency_press(EM_MOB));
  app_tick();
  TEST_ASSERT_TRUE(sos_shown());
  ui_shell_goto(0, 0);   // wherever the crew are
  lv_obj_send_event(ui_shell_sos(), LV_EVENT_CLICKED, NULL);
  int8_t nav = app_alert_role("mob");
  TEST_ASSERT_EQUAL_UINT8((uint8_t)nav, ui_shell_nav()->role);
  TEST_ASSERT_EQUAL_UINT8(1, ui_shell_nav()->page);
  alerts_source_up(false);   // the script (position, name) matters most when the network is gone
  app_tick();
  TEST_ASSERT_TRUE(sos_shown());
  em_cancel_sent();
  app_tick();
  TEST_ASSERT_FALSE(sos_shown());
  ui_shell_goto(0, 0);
}

static int8_t nav_role(void) { return app_alert_role("mob"); }

static void test_a_quick_short_press_declares_nothing(void) {
  open_quick();
  hold(UI_QUICK_BTN_MOB, 400);
  TEST_ASSERT_FALSE(em_active());
  TEST_ASSERT_TRUE(ui_quick_is_open());
  sk_cmd_t c;
  sk_cmd_handle_t h;
  TEST_ASSERT_FALSE(take(&c, &h));
  close_quick();
}

static void test_a_quick_cancel_hold_sends_the_put_to_the_notifications_id(void) {
  TEST_ASSERT_TRUE(app_emergency_press(EM_FIRE));
  answer_all_with_id(200, "f-uuid");
  app_tick();
  open_quick();
  TEST_ASSERT_NOT_NULL(ui_quick_part(UI_QUICK_BTN_CANCEL));
  hold(UI_QUICK_BTN_CANCEL, 1100);
  sk_cmd_t c;
  sk_cmd_handle_t h;
  TEST_ASSERT_TRUE(take(&c, &h));
  TEST_ASSERT_EQUAL_STRING("PUT", c.method);
  TEST_ASSERT_EQUAL_STRING("/signalk/v2/api/notifications/f-uuid", c.path);
  TEST_ASSERT_TRUE_MESSAGE(em_active(), "it stays declared until the server has accepted the cancel");
}

static void test_something_declared_elsewhere_shows_cancel_and_fills_no_button(void) {
  em_on_notification(EM_FLOOD, true, false, 0, 0, "fl-1", 1000);   // flooding has no button here
  open_quick();
  TEST_ASSERT_NOT_NULL(ui_quick_part(UI_QUICK_BTN_CANCEL));   // (LVGL's assert loops forever on a NULL object)
  TEST_ASSERT_FALSE(lv_obj_has_flag(ui_quick_part(UI_QUICK_BTN_CANCEL), LV_OBJ_FLAG_HIDDEN));
  lv_color_t alarm = ui_theme()->alarm;
  const ui_quick_part_t btns[] = {UI_QUICK_BTN_MOB, UI_QUICK_BTN_FIRE, UI_QUICK_BTN_SINKING};
  for (size_t i = 0; i < 3; i++) {
    TEST_ASSERT_NOT_NULL(ui_quick_part(btns[i]));
    TEST_ASSERT_FALSE(lv_color_eq(lv_obj_get_style_bg_color(ui_quick_part(btns[i]), LV_PART_MAIN), alarm));
  }
  close_quick();
  em_reset();
  em_on_notification(EM_FIRE, true, false, 0, 0, "f-2", 2000);
  open_quick();
  TEST_ASSERT_NOT_NULL(ui_quick_part(UI_QUICK_BTN_FIRE));
  TEST_ASSERT_TRUE(lv_color_eq(lv_obj_get_style_bg_color(ui_quick_part(UI_QUICK_BTN_FIRE), LV_PART_MAIN), alarm));   // a fire raised elsewhere is filled
  close_quick();
}

static void test_fire_flood_sinking_select_the_script_page_automatically(void) {
  const em_nature_t natures[] = {EM_FIRE, EM_FLOOD, EM_SINKING, EM_OTHER};
  for (size_t i = 0; i < sizeof natures / sizeof natures[0]; i++) {
    em_reset();
    app_tick();   // nothing declared: the shell notes that
    ui_shell_goto(0, 0);
    em_on_notification(natures[i], true, false, 0, 0, NULL, 1000);
    app_tick();
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)nav_role(), ui_shell_nav()->role, em_nature_label(natures[i]));
    TEST_ASSERT_EQUAL_UINT8(1, ui_shell_nav()->page);
    ui_shell_goto(0, 0);   // the crew leave the page
    app_tick();
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, ui_shell_nav()->role, "no repeat jump while it stays declared");
  }
  ui_shell_goto(0, 0);
}

static void test_mob_does_not_jump_the_page_by_itself(void) {
  em_reset();
  app_tick();
  ui_shell_goto(0, 0);
  em_on_notification(EM_MOB, true, true, 41.5, -70.25, NULL, 1000);
  app_tick();
  TEST_ASSERT_EQUAL_UINT8(0, ui_shell_nav()->role);   // GO on the banner (page_for) and SOS take the crew there
}

static void test_a_press_on_this_display_does_not_need_the_jump_but_a_new_nature_still_jumps(void) {
  em_reset();
  app_tick();
  em_on_notification(EM_FIRE, true, false, 0, 0, NULL, 1000);
  app_tick();
  ui_shell_goto(0, 0);
  em_on_notification(EM_FIRE, false, false, 0, 0, NULL, 2000);   // out
  app_tick();
  em_on_notification(EM_FLOOD, true, false, 0, 0, NULL, 3000);   // a new declaration
  app_tick();
  TEST_ASSERT_EQUAL_UINT8(1, ui_shell_nav()->page);
  ui_shell_goto(0, 0);
}

int main(void) {
  lv_init();
  lv_tick_set_cb(fake_tick);
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
  RUN_TEST(test_fire_goes_to_the_generic_endpoint_with_its_own_path);
  RUN_TEST(test_cancel_puts_state_normal_to_the_notifications_id);
  RUN_TEST(test_cancel_without_a_known_id_is_refused_and_says_so);
  RUN_TEST(test_id_comes_from_the_echo_when_the_reply_had_none);
  RUN_TEST(test_a_mob_raised_elsewhere_is_cancelled_by_its_id);
  RUN_TEST(test_cancel_that_cannot_be_sent_keeps_the_emergency);
  RUN_TEST(test_second_press_after_failure_is_a_new_capture);
  RUN_TEST(test_second_press_while_sending_is_ignored);
  RUN_TEST(test_waypoint_failure_does_not_fail_the_notification);
  RUN_TEST(test_no_fix_sends_no_position_and_no_waypoint);
  RUN_TEST(test_incoming_notification_sets_the_state);
  RUN_TEST(test_position_is_subscribed_with_the_navigation_role);
  RUN_TEST(test_alert_role_maps_emergency_ids_to_navigation);
  RUN_TEST(test_vessel_name_config_overrides_signalk);
  RUN_TEST(test_script_page_reads_the_call_with_the_apps_vessel_name);
  RUN_TEST(test_quick_panel_has_the_emergency_row_only_when_it_can_send);
  RUN_TEST(test_a_quick_hold_declares_and_goes_where_the_other_displays_go);
  RUN_TEST(test_a_quick_short_press_declares_nothing);
  RUN_TEST(test_a_quick_cancel_hold_sends_the_put_to_the_notifications_id);
  RUN_TEST(test_something_declared_elsewhere_shows_cancel_and_fills_no_button);
  RUN_TEST(test_sos_shows_for_a_mob_and_goes_to_the_script);
  RUN_TEST(test_fire_flood_sinking_select_the_script_page_automatically);
  RUN_TEST(test_mob_does_not_jump_the_page_by_itself);
  RUN_TEST(test_a_press_on_this_display_does_not_need_the_jump_but_a_new_nature_still_jumps);
  return UNITY_END();
}
