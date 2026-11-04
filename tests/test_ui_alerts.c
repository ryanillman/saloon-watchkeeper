#include <stdio.h>
#include <string.h>
#include "alerts.h"
#include "ui_alerts.h"
#include "ui_gauge.h"
#include "ui_shell.h"
#include "unity.h"

static int flushes;
static uint8_t buf[800 * 48 * 2];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  flushes++;
  lv_display_flush_ready(d);
}

static lv_obj_t *scr, *strip;
static int8_t role_value;
static int8_t role_of(const char *id) { (void)id; return role_value; }

static void raise(const char *id, alert_state_t st, const char *msg) {
  alert_t a = {0};
  snprintf(a.id, sizeof a.id, "%s", id);
  a.state = st;
  snprintf(a.message, sizeof a.message, "%s", msg);
  a.audible = true;
  snprintf(a.ref, sizeof a.ref, "ref-%s", id);
  alerts_upsert(&a, 1000);
}

static void tick(int64_t now) {
  alerts_snapshot_t s;
  alerts_snapshot(&s);
  ui_alerts_update(&s, now);
  lv_refr_now(NULL);
}

static void click(ui_alerts_part_t p) { lv_obj_send_event(ui_alerts_part(p), LV_EVENT_CLICKED, NULL); }

void setUp(void) {
  ui_theme_set_current(PALETTE_DAY);
  alerts_init();
  alerts_source_up(true);
  role_value = 0;
  lv_obj_clean(scr);
  strip = lv_obj_create(scr);
  lv_obj_set_size(strip, 800, 40);
  ui_alerts_attach(scr, strip, SIZE_WIDE, role_of, NULL);
  tick(0);
}
void tearDown(void) {}

static void test_alarm_shows_banner_with_go(void) {
  raise("propulsion.port.temperature", AL_ALARM, "Port coolant high");
  tick(0);
  TEST_ASSERT_TRUE(ui_alerts_banner_visible());
  TEST_ASSERT_EQUAL_STRING("Port coolant high", ui_alerts_banner_text());
  TEST_ASSERT_TRUE(ui_alerts_go_visible());
}

static void test_go_hidden_without_role(void) {
  role_value = -1;
  raise("navigation.anchor.alarm", AL_ALARM, "Anchor dragging");
  tick(0);
  TEST_ASSERT_TRUE(ui_alerts_banner_visible());
  TEST_ASSERT_FALSE(ui_alerts_go_visible());
}

static void test_warn_shows_marker_not_banner(void) {
  raise("tanks.fuel.0.currentLevel", AL_WARN, "Fuel low");
  raise("tanks.freshWater.0.currentLevel", AL_WARN, "Fresh water low");
  tick(0);
  TEST_ASSERT_FALSE(ui_alerts_banner_visible());
  TEST_ASSERT_EQUAL_STRING("2", ui_alerts_marker_text());
}

static void test_marker_hidden_with_no_alerts(void) {
  tick(0);
  TEST_ASSERT_EQUAL_STRING("", ui_alerts_marker_text());
}

static void test_offline_text_when_source_down(void) {
  TEST_ASSERT_EQUAL_STRING("", ui_alerts_offline_text());
  alerts_source_up(false);
  tick(0);
  TEST_ASSERT_EQUAL_STRING("SK ALERTS OFFLINE", ui_alerts_offline_text());
  TEST_ASSERT_FALSE(ui_alerts_banner_visible());
}

static void test_ack_button_requests_ack(void) {
  raise("propulsion.port.temperature", AL_ALARM, "Port coolant high");
  tick(0);
  tick(1000);   // past the tap guard
  lv_obj_send_event(ui_alerts_part(UI_ALERTS_ACK), LV_EVENT_CLICKED, NULL);
  alert_t a;
  TEST_ASSERT_TRUE(alerts_get("propulsion.port.temperature", &a));
  TEST_ASSERT_TRUE(a.acked);
  TEST_ASSERT_FALSE(a.silenced);
}

static void test_silence_button_requests_silence(void) {
  raise("propulsion.port.temperature", AL_ALARM, "Port coolant high");
  tick(0);
  tick(1000);   // past the tap guard
  lv_obj_send_event(ui_alerts_part(UI_ALERTS_SILENCE), LV_EVENT_CLICKED, NULL);
  alert_t a;
  TEST_ASSERT_TRUE(alerts_get("propulsion.port.temperature", &a));
  TEST_ASSERT_TRUE(a.silenced);
}

static void test_banner_flashes_until_acked(void) {
  raise("propulsion.port.temperature", AL_ALARM, "Port coolant high");
  tick(0);
  lv_color_t on = lv_obj_get_style_bg_color(ui_alerts_part(UI_ALERTS_BANNER), 0);
  tick(500);
  lv_color_t off = lv_obj_get_style_bg_color(ui_alerts_part(UI_ALERTS_BANNER), 0);
  TEST_ASSERT_FALSE(lv_color_eq(on, off));
  TEST_ASSERT_EQUAL_STRING("", ui_alerts_acked_text());
}

static void test_acked_alarm_leaves_banner_for_bell(void) {
  raise("a.one", AL_ALARM, "One");
  tick(0);
  TEST_ASSERT_TRUE(ui_alerts_banner_visible());
  alerts_request("a.one", AL_REQ_ACK);
  tick(500);
  TEST_ASSERT_FALSE(ui_alerts_banner_visible());
  TEST_ASSERT_NOT_EQUAL(0, strlen(ui_alerts_acked_text()));
  TEST_ASSERT_NOT_NULL(strstr(ui_alerts_acked_text(), "1"));
  TEST_ASSERT_EQUAL_STRING("", ui_alerts_marker_text());   // not counted as an alert/warn
  click(UI_ALERTS_ACKED_MARKER);   // opens the same list, which still lists the alarm
  tick(600);
  TEST_ASSERT_TRUE(ui_alerts_list_visible());
  TEST_ASSERT_EQUAL(1, ui_alerts_list_rows());
  click(UI_ALERTS_CLOSE);
  alerts_clear("a.one");
  tick(700);
  TEST_ASSERT_EQUAL_STRING("", ui_alerts_acked_text());
}

static void test_banner_moves_to_next_unacked_alarm(void) {
  raise("a.one", AL_ALARM, "One");
  raise("a.two", AL_ALARM, "Two");
  alerts_request("a.one", AL_REQ_ACK);
  tick(0);
  TEST_ASSERT_EQUAL_STRING("Two", ui_alerts_banner_text());
  TEST_ASSERT_EQUAL_STRING("", ui_alerts_corner_text());   // only one still needs attention
}

static void test_ack_not_sent_replaces_count(void) {
  raise("a.one", AL_ALARM, "One");
  raise("a.two", AL_ALARM, "Two");
  tick(0);
  TEST_ASSERT_EQUAL_STRING("+1", ui_alerts_corner_text());
  alerts_source_up(false);   // the source drops: list clears
  raise("a.one", AL_ALARM, "One");
  alerts_request("a.one", AL_REQ_ACK);   // source down: send_failed
  tick(0);
  TEST_ASSERT_EQUAL_STRING("ACK NOT SENT", ui_alerts_corner_text());
}

static bool inside(lv_obj_t *o, lv_obj_t *box) {
  lv_area_t a, b;
  lv_obj_get_coords(o, &a);
  lv_obj_get_coords(box, &b);
  return a.x1 >= b.x1 && a.x2 <= b.x2 && a.y1 >= b.y1 && a.y2 <= b.y2;
}

static void test_long_message_keeps_buttons_visible(void) {
  char msg[97];
  memset(msg, 'W', 96);
  msg[96] = 0;
  for (int i = 8; i < 96; i += 9) msg[i] = ' ';
  raise("propulsion.port.temperature", AL_ALARM, msg);
  tick(0);
  lv_obj_t *banner = ui_alerts_part(UI_ALERTS_BANNER);
  lv_obj_update_layout(scr);
  static const ui_alerts_part_t parts[] = {UI_ALERTS_ACK, UI_ALERTS_SILENCE, UI_ALERTS_GO};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *p = ui_alerts_part(parts[i]);
    TEST_ASSERT_FALSE(lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN));
    TEST_ASSERT_TRUE(inside(p, banner));
  }
}

// An alerting field is tinted (background and border in the alert colour);
// its digits stay in the normal colour so they read on the tint.
static void test_field_alert_tints_background(void) {
  lv_obj_t *r = ui_readout_create(scr, "OIL", "bar", 1);
  ui_readout_set(r, 3.0, true);
  TEST_ASSERT_EQUAL(LV_OPA_TRANSP, lv_obj_get_style_bg_opa(r, 0));
  TEST_ASSERT_EQUAL(0, lv_obj_get_style_border_width(r, 0));

  ui_field_set_alert(r, AL_ALARM);
  TEST_ASSERT_TRUE(lv_color_eq(ui_theme()->alarm, lv_obj_get_style_bg_color(r, 0)));
  lv_opa_t alarm_opa = lv_obj_get_style_bg_opa(r, 0);
  TEST_ASSERT_TRUE(alarm_opa > LV_OPA_TRANSP && alarm_opa < LV_OPA_COVER);
  TEST_ASSERT_EQUAL(3, lv_obj_get_style_border_width(r, 0));
  TEST_ASSERT_TRUE(lv_color_eq(ui_theme()->alarm, lv_obj_get_style_border_color(r, 0)));
  TEST_ASSERT_TRUE(lv_color_eq(ui_theme()->fg, ui_field_value_color(r)));

  ui_field_set_alert(r, AL_WARN);
  TEST_ASSERT_TRUE(lv_color_eq(ui_theme()->warn, lv_obj_get_style_bg_color(r, 0)));
  TEST_ASSERT_TRUE(lv_obj_get_style_bg_opa(r, 0) < alarm_opa);   // lighter, so alarm stays distinct at night
  TEST_ASSERT_TRUE(lv_color_eq(ui_theme()->fg, ui_field_value_color(r)));

  ui_field_set_alert(r, AL_NORMAL);
  TEST_ASSERT_EQUAL(LV_OPA_TRANSP, lv_obj_get_style_bg_opa(r, 0));
  TEST_ASSERT_EQUAL(0, lv_obj_get_style_border_width(r, 0));
}

static void test_list_has_a_row_per_alert(void) {
  raise("a.one", AL_WARN, "One");
  raise("a.two", AL_ALERT, "Two");
  tick(0);
  lv_obj_send_event(ui_alerts_part(UI_ALERTS_MARKER), LV_EVENT_CLICKED, NULL);
  tick(0);
  TEST_ASSERT_TRUE(ui_alerts_list_visible());
  TEST_ASSERT_EQUAL(2, ui_alerts_list_rows());
  lv_obj_send_event(ui_alerts_part(UI_ALERTS_CLOSE), LV_EVENT_CLICKED, NULL);
  tick(0);
  TEST_ASSERT_FALSE(ui_alerts_list_visible());
}

static void test_ack_not_sent_fits_its_label(void) {
  raise("a.one", AL_ALARM, "One");
  alerts_source_up(false);
  raise("a.one", AL_ALARM, "One");
  alerts_request("a.one", AL_REQ_ACK);
  tick(0);
  lv_obj_t *c = ui_alerts_part(UI_ALERTS_CORNER);
  lv_obj_update_layout(scr);
  const char *txt = ui_alerts_corner_text();
  TEST_ASSERT_EQUAL_STRING("ACK NOT SENT", txt);
  int32_t need = lv_text_get_width(txt, strlen(txt), lv_obj_get_style_text_font(c, 0), 0);
  TEST_ASSERT_GREATER_OR_EQUAL_INT32(need, lv_obj_get_content_width(c));
  TEST_ASSERT_TRUE(inside(c, ui_alerts_part(UI_ALERTS_BANNER)));
  lv_obj_t *msg = ui_alerts_part(UI_ALERTS_BANNER);
  (void)msg;
}

static void test_banner_is_compact_on_wide(void) {
  raise("propulsion.port.temperature", AL_ALARM, "Port coolant high");
  tick(0);
  lv_obj_update_layout(scr);
  TEST_ASSERT_LESS_OR_EQUAL_INT32(70, lv_obj_get_height(ui_alerts_part(UI_ALERTS_BANNER)));
  // buttons share the message's row
  lv_area_t m, b;
  lv_obj_get_coords(ui_alerts_part(UI_ALERTS_ACK), &b);
  TEST_ASSERT_TRUE(b.y2 - b.y1 < 60);
  (void)m;
}

static bool is_white(lv_color_t c) { return lv_color_eq(c, lv_color_white()); }

static void test_night_banner_never_white(void) {
  ui_theme_set_current(PALETTE_NIGHT);
  alerts_init();
  alerts_source_up(true);
  lv_obj_clean(scr);
  strip = lv_obj_create(scr);
  lv_obj_set_size(strip, 800, 40);
  ui_alerts_attach(scr, strip, SIZE_WIDE, role_of, NULL);
  raise("propulsion.port.temperature", AL_ALARM, "Port coolant high");
  raise("a.two", AL_ALARM, "Two");
  static const ui_alerts_part_t btn[] = {UI_ALERTS_ACK, UI_ALERTS_SILENCE, UI_ALERTS_GO};
  for (int64_t now = 0; now <= 500; now += 500) {
    tick(now);
    TEST_ASSERT_FALSE(is_white(lv_obj_get_style_bg_color(ui_alerts_part(UI_ALERTS_BANNER), 0)));
    TEST_ASSERT_FALSE(is_white(lv_obj_get_style_text_color(ui_alerts_part(UI_ALERTS_BANNER), 0)));
    lv_obj_t *lbl[] = {ui_alerts_part(UI_ALERTS_BANNER)};
    (void)lbl;
    TEST_ASSERT_FALSE(is_white(lv_obj_get_style_text_color(ui_alerts_part(UI_ALERTS_CORNER), 0)));
    for (int i = 0; i < 3; i++) {
      lv_obj_t *b = ui_alerts_part(btn[i]);
      TEST_ASSERT_FALSE(is_white(lv_obj_get_style_bg_color(b, 0)));
      TEST_ASSERT_FALSE(is_white(lv_obj_get_style_border_color(b, 0)));
      TEST_ASSERT_FALSE(is_white(lv_obj_get_style_text_color(b, 0)));
    }
    lv_obj_t *msg = lv_obj_get_child(ui_alerts_part(UI_ALERTS_BANNER), 0);
    TEST_ASSERT_FALSE(is_white(lv_obj_get_style_text_color(msg, 0)));
  }
}

static void test_unchanged_tick_redraws_nothing(void) {
  raise("propulsion.port.temperature", AL_ALARM, "Port coolant high");
  raise("tanks.fuel.0.currentLevel", AL_WARN, "Fuel low");
  tick(100);
  tick(100);
  int before = flushes;
  tick(200);   // same snapshot, same flash phase
  tick(300);
  TEST_ASSERT_EQUAL(before, flushes);
  // list open: same again
  lv_obj_send_event(ui_alerts_part(UI_ALERTS_MARKER), LV_EVENT_CLICKED, NULL);
  tick(1000);
  tick(1000);
  before = flushes;
  tick(1100);
  tick(1200);
  TEST_ASSERT_EQUAL(before, flushes);
  lv_obj_send_event(ui_alerts_part(UI_ALERTS_CLOSE), LV_EVENT_CLICKED, NULL);
}

static uint8_t buf480[480 * 48 * 2];
static const ui_role_view_t two_pages[] = {{.name = "ENGINE", .page_count = 2}};

static bool areas_meet(lv_obj_t *a, lv_obj_t *b) {
  lv_area_t x, y;
  lv_obj_get_coords(a, &x);
  lv_obj_get_coords(b, &y);
  return x.x1 <= y.x2 && y.x1 <= x.x2 && x.y1 <= y.y2 && y.y1 <= x.y2;
}

static void test_square_offline_clear_of_page_dots(void) {
  lv_display_t *old = lv_display_get_default();
  lv_display_t *d = lv_display_create(480, 480);
  lv_display_set_flush_cb(d, flush);
  lv_display_set_buffers(d, buf480, NULL, sizeof buf480, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_default(d);
  ui_shell_create(d, SIZE_SQUARE, PALETTE_DAY, two_pages, 1, 0);
  ui_alerts_attach(lv_display_get_screen_active(d), ui_shell_strip(), SIZE_SQUARE, role_of, NULL);
  ui_shell_set_status("SK OFFLINE", ui_theme()->alarm);
  alerts_source_up(false);
  raise("a.one", AL_WARN, "One");   // the marker is showing too
  alerts_snapshot_t s;
  alerts_snapshot(&s);
  s.source_up = false;
  ui_alerts_update(&s, 0);
  lv_refr_now(NULL);
  TEST_ASSERT_EQUAL_STRING("SK ALERTS OFFLINE", ui_alerts_offline_text());
  TEST_ASSERT_FALSE(areas_meet(ui_alerts_part(UI_ALERTS_OFFLINE), ui_shell_dots()));
  TEST_ASSERT_FALSE(areas_meet(ui_alerts_part(UI_ALERTS_MARKER), ui_shell_dots()));
  // the app's status comes back when the source does
  s.source_up = true;
  ui_alerts_update(&s, 0);
  TEST_ASSERT_EQUAL_STRING("SK OFFLINE", lv_label_get_text(ui_shell_status()));
  lv_display_set_default(old);
  lv_display_delete(d);
}

static void test_palette_switch_restyles_alerts(void) {
  raise("propulsion.port.temperature", AL_ALARM, "Port coolant high");
  raise("tanks.fuel.0.currentLevel", AL_WARN, "Fuel low");
  tick(0);
  lv_obj_t *marker = ui_alerts_part(UI_ALERTS_MARKER);
  lv_color_t day_banner = lv_obj_get_style_bg_color(ui_alerts_part(UI_ALERTS_BANNER), 0);
  lv_color_t day_marker = lv_obj_get_style_border_color(marker, 0);
  TEST_ASSERT_TRUE(lv_color_eq(day_banner, ui_theme_colors(PALETTE_DAY)->alarm));
  ui_theme_set_current(PALETTE_NIGHT);   // what ui_shell_set_palette does
  tick(0);
  const ui_colors_t *n = ui_theme_colors(PALETTE_NIGHT);
  TEST_ASSERT_TRUE(lv_color_eq(lv_obj_get_style_bg_color(ui_alerts_part(UI_ALERTS_BANNER), 0), n->alarm));
  TEST_ASSERT_FALSE(lv_color_eq(day_marker, n->warn));
  TEST_ASSERT_TRUE(lv_color_eq(lv_obj_get_style_border_color(marker, 0), n->warn));
}


static void test_double_tap_acks_only_the_first_alarm(void) {
  raise("a.one", AL_ALARM, "One");
  raise("a.two", AL_ALARM, "Two");
  tick(0);
  tick(5000);
  TEST_ASSERT_EQUAL_STRING("One", ui_alerts_banner_text());
  click(UI_ALERTS_ACK);
  tick(5100);   // the banner moves on to the next un-acked alarm
  TEST_ASSERT_EQUAL_STRING("Two", ui_alerts_banner_text());
  click(UI_ALERTS_ACK);   // second tap, 100 ms later
  click(UI_ALERTS_SILENCE);
  alert_t a;
  TEST_ASSERT_TRUE(alerts_get("a.one", &a));
  TEST_ASSERT_TRUE(a.acked);
  TEST_ASSERT_TRUE(alerts_get("a.two", &a));
  TEST_ASSERT_FALSE(a.acked);
  TEST_ASSERT_FALSE(a.silenced);
  tick(6100);   // a second after it appeared, ACK works again
  click(UI_ALERTS_ACK);
  TEST_ASSERT_TRUE(alerts_get("a.two", &a));
  TEST_ASSERT_TRUE(a.acked);
}

static void test_state_change_rearms_tap_guard(void) {
  raise("a.one", AL_ALARM, "One");
  tick(0);
  tick(5000);
  raise("a.one", AL_EMERGENCY, "One");   // same id, escalated
  tick(5100);
  click(UI_ALERTS_ACK);
  alert_t a;
  TEST_ASSERT_TRUE(alerts_get("a.one", &a));
  TEST_ASSERT_FALSE(a.acked);
}

static uint8_t fuel_page(const char *id) {
  (void)id;
  return 1;
}
static int8_t tanks_role(const char *id) {
  (void)id;
  return 1;
}

static void test_go_opens_the_alarms_page(void) {
  static const ui_role_view_t roles[] = {
    {.name = "ENGINE", .page_count = 2},
    {.name = "TANKS", .page_count = 2},
  };
  ui_shell_create(lv_display_get_default(), SIZE_WIDE, PALETTE_DAY, roles, 2, 0);
  ui_alerts_attach(lv_screen_active(), ui_shell_strip(), SIZE_WIDE, tanks_role, fuel_page);
  raise("tanks.fuel.0.currentLevel", AL_ALARM, "Fuel low");
  tick(0);
  click(UI_ALERTS_GO);
  const ui_nav_t *n = ui_shell_nav();
  TEST_ASSERT_EQUAL_UINT8(1, n->role);
  TEST_ASSERT_EQUAL_UINT8(1, n->page);
}

static void test_list_row_ack_guarded(void) {
  raise("a.one", AL_ALARM, "One");
  tick(0);
  click(UI_ALERTS_MARKER);   // marker is hidden with no warns, but its handler opens the list
  tick(100);
  TEST_ASSERT_TRUE(ui_alerts_list_visible());
  lv_obj_t *row_ack = ui_alerts_part(UI_ALERTS_ROW0_ACK);
  lv_obj_send_event(row_ack, LV_EVENT_CLICKED, NULL);   // the row appeared 100 ms ago
  alert_t a;
  TEST_ASSERT_TRUE(alerts_get("a.one", &a));
  TEST_ASSERT_FALSE(a.acked);
  tick(1200);
  lv_obj_send_event(row_ack, LV_EVENT_CLICKED, NULL);
  TEST_ASSERT_TRUE(alerts_get("a.one", &a));
  TEST_ASSERT_TRUE(a.acked);
}

int main(void) {
  lv_init();
  lv_display_t *disp = lv_display_create(800, 480);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  scr = lv_screen_active();
  UNITY_BEGIN();
  RUN_TEST(test_ack_not_sent_fits_its_label);
  RUN_TEST(test_banner_is_compact_on_wide);
  RUN_TEST(test_night_banner_never_white);
  RUN_TEST(test_unchanged_tick_redraws_nothing);
  RUN_TEST(test_alarm_shows_banner_with_go);
  RUN_TEST(test_go_hidden_without_role);
  RUN_TEST(test_warn_shows_marker_not_banner);
  RUN_TEST(test_marker_hidden_with_no_alerts);
  RUN_TEST(test_offline_text_when_source_down);
  RUN_TEST(test_ack_button_requests_ack);
  RUN_TEST(test_silence_button_requests_silence);
  RUN_TEST(test_banner_flashes_until_acked);
  RUN_TEST(test_acked_alarm_leaves_banner_for_bell);
  RUN_TEST(test_banner_moves_to_next_unacked_alarm);
  RUN_TEST(test_ack_not_sent_replaces_count);
  RUN_TEST(test_long_message_keeps_buttons_visible);
  RUN_TEST(test_field_alert_tints_background);
  RUN_TEST(test_list_has_a_row_per_alert);
  RUN_TEST(test_square_offline_clear_of_page_dots);
  RUN_TEST(test_palette_switch_restyles_alerts);
  RUN_TEST(test_double_tap_acks_only_the_first_alarm);
  RUN_TEST(test_state_change_rearms_tap_guard);
  RUN_TEST(test_go_opens_the_alarms_page);
  RUN_TEST(test_list_row_ack_guarded);
  return UNITY_END();
}
