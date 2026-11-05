// The top strip: role name, page dots, clock, source dot and status text, with the alert markers.
#include <stdio.h>
#include <string.h>
#include "alerts.h"
#include "ui_alerts.h"
#include "ui_shell.h"
#include "unity.h"

static uint8_t buf[1024 * 48 * 2];
static int flushes;
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  flushes++;
  lv_display_flush_ready(d);
}
static int8_t role_of(const char *id) { (void)id; return 0; }
static const ui_role_view_t views[] = {{.name = "GLANCE", .page_count = 1}, {.name = "ENGINE", .page_count = 4}};

static lv_display_t *disp, *old_disp;

static void open_shell(int32_t w, int32_t h, size_class_t sc, palette_t pal) {
  old_disp = lv_display_get_default();
  disp = lv_display_create(w, h);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_default(disp);
  ui_shell_create(disp, sc, pal, views, 2, 0);
  ui_alerts_attach(lv_display_get_screen_active(disp), ui_shell_strip(), sc, role_of, NULL);
}
static void close_shell(void) {
  lv_display_set_default(old_disp);
  lv_display_delete(disp);
}

void setUp(void) {
  alerts_init();
  alerts_source_up(true);
}
void tearDown(void) {}

static void raise(const char *id, alert_state_t st, bool acked) {
  alert_t a = {0};
  snprintf(a.id, sizeof a.id, "%s", id);
  a.state = st;
  snprintf(a.message, sizeof a.message, "msg");
  a.audible = true;
  snprintf(a.ref, sizeof a.ref, "ref-%s", id);
  alerts_upsert(&a, 1000);
  if (acked) alerts_request(id, AL_REQ_ACK);
}

static void update(bool source_up) {
  alerts_snapshot_t s;
  alerts_snapshot(&s);
  s.source_up = source_up;
  ui_alerts_update(&s, 0);
  lv_obj_update_layout(lv_display_get_screen_active(disp));
  lv_refr_now(NULL);
}

static bool shown(lv_obj_t *o) {
  if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
  if (lv_obj_check_type(o, &lv_label_class) && !lv_label_get_text(o)[0]) return false;
  return true;
}
static bool meet(lv_obj_t *a, lv_obj_t *b) {
  lv_area_t x, y;
  lv_obj_get_coords(a, &x);
  lv_obj_get_coords(b, &y);
  return x.x1 <= y.x2 && y.x1 <= x.x2 && x.y1 <= y.y2 && y.y1 <= x.y2;
}

// Every part of the strip that is showing, none overlapping another.
static void check_no_overlap(void) {
  lv_obj_t *p[] = {ui_shell_home_button(), ui_shell_name(), ui_shell_dots(), ui_shell_clock(), ui_shell_source_dot(), ui_shell_status(),
                   ui_alerts_part(UI_ALERTS_MARKER), ui_alerts_part(UI_ALERTS_ACKED_MARKER), ui_alerts_part(UI_ALERTS_OFFLINE)};
  const char *n[] = {"home", "name", "dots", "clock", "source dot", "status", "marker", "acked marker", "offline"};
  for (size_t i = 0; i < sizeof p / sizeof p[0]; i++)
    for (size_t j = i + 1; j < sizeof p / sizeof p[0]; j++) {
      if (p[i] == p[j] || !shown(p[i]) || !shown(p[j])) continue;   // square: the offline text is the status label
      char msg[64];
      snprintf(msg, sizeof msg, "%s overlaps %s", n[i], n[j]);
      TEST_ASSERT_FALSE_MESSAGE(meet(p[i], p[j]), msg);
    }
}

// Signal K down: the status text and "SK ALERTS OFFLINE" show, the alert list is empty.
static void offline_strip(int32_t w, int32_t h, size_class_t sc) {
  open_shell(w, h, sc, PALETTE_DAY);
  ui_shell_set_clock("02:14");
  ui_shell_set_source(UI_SRC_OFFLINE);
  update(false);
  TEST_ASSERT_TRUE(shown(ui_alerts_part(UI_ALERTS_OFFLINE)) || sc == SIZE_SQUARE);   // square: in the status label
  check_no_overlap();
  close_shell();
}

// Signal K up: both alert markers show beside the bare dot.
static void marked_strip(int32_t w, int32_t h, size_class_t sc) {
  open_shell(w, h, sc, PALETTE_DAY);
  ui_shell_set_clock("02:14");
  ui_shell_set_source(UI_SRC_LIVE);
  raise("a.warn", AL_WARN, false);
  raise("a.alarm", AL_ALARM, true);
  update(true);
  TEST_ASSERT_TRUE(shown(ui_alerts_part(UI_ALERTS_MARKER)));
  TEST_ASSERT_TRUE(shown(ui_alerts_part(UI_ALERTS_ACKED_MARKER)));
  check_no_overlap();
  close_shell();
}

static void test_offline_strip_clear_on_square(void) { offline_strip(480, 480, SIZE_SQUARE); }
static void test_offline_strip_clear_on_800(void) { offline_strip(800, 480, SIZE_WIDE); }
static void test_offline_strip_clear_on_1024(void) { offline_strip(1024, 600, SIZE_WIDE); }
static void test_marked_strip_clear_on_square(void) { marked_strip(480, 480, SIZE_SQUARE); }
static void test_marked_strip_clear_on_800(void) { marked_strip(800, 480, SIZE_WIDE); }
static void test_marked_strip_clear_on_1024(void) { marked_strip(1024, 600, SIZE_WIDE); }

static void test_clock_shows_text_and_hides_on_null(void) {
  open_shell(800, 480, SIZE_WIDE, PALETTE_DAY);
  ui_shell_set_clock("02:14");
  update(true);
  TEST_ASSERT_TRUE(shown(ui_shell_clock()));
  TEST_ASSERT_EQUAL_STRING("02:14", lv_label_get_text(ui_shell_clock()));
  ui_shell_set_clock(NULL);
  update(true);
  TEST_ASSERT_FALSE(shown(ui_shell_clock()));
  close_shell();
}

static void test_setting_the_same_clock_and_source_again_redraws_nothing(void) {   // the app does both every tick
  open_shell(800, 480, SIZE_WIDE, PALETTE_DAY);
  ui_shell_set_clock("02:14");
  ui_shell_set_source(UI_SRC_OFFLINE);
  update(true);
  lv_refr_now(NULL);
  int before = flushes;
  for (int i = 0; i < 5; i++) {
    ui_shell_set_clock("02:14");
    ui_shell_set_source(UI_SRC_OFFLINE);
  }
  lv_refr_now(NULL);
  TEST_ASSERT_EQUAL_INT(before, flushes);
  close_shell();
}

static void test_live_is_a_bare_dot_and_trouble_adds_text(void) {
  open_shell(800, 480, SIZE_WIDE, PALETTE_DAY);
  ui_shell_set_source(UI_SRC_LIVE);
  update(true);
  TEST_ASSERT_EQUAL_STRING("", lv_label_get_text(ui_shell_status()));
  ui_shell_set_source(UI_SRC_CONNECTING);
  TEST_ASSERT_EQUAL_STRING("SK CONNECTING", lv_label_get_text(ui_shell_status()));
  ui_shell_set_source(UI_SRC_OFFLINE);
  TEST_ASSERT_EQUAL_STRING("SK OFFLINE", lv_label_get_text(ui_shell_status()));
  ui_shell_set_source(UI_SRC_LIVE);
  TEST_ASSERT_EQUAL_STRING("", lv_label_get_text(ui_shell_status()));
  close_shell();
}

static void test_dot_shape_differs_by_state_not_only_colour(void) {
  open_shell(800, 480, SIZE_WIDE, PALETTE_NIGHT);   // night: warn and alarm are the same red
  lv_obj_t *d = ui_shell_source_dot();
  ui_shell_set_source(UI_SRC_LIVE);
  TEST_ASSERT_EQUAL_UINT8(LV_OPA_COVER, lv_obj_get_style_bg_opa(d, 0));   // filled
  TEST_ASSERT_EQUAL_UINT32(0, lv_obj_get_child_count(d));
  ui_shell_set_source(UI_SRC_CONNECTING);
  TEST_ASSERT_EQUAL_UINT8(LV_OPA_TRANSP, lv_obj_get_style_bg_opa(d, 0));  // hollow ring
  TEST_ASSERT_GREATER_THAN_INT(0, lv_obj_get_style_border_width(d, 0));
  TEST_ASSERT_EQUAL_UINT32(0, lv_obj_get_child_count(d));
  ui_shell_set_source(UI_SRC_OFFLINE);
  TEST_ASSERT_EQUAL_UINT8(LV_OPA_TRANSP, lv_obj_get_style_bg_opa(d, 0));  // ring with a cross
  TEST_ASSERT_GREATER_THAN_UINT32(0, lv_obj_get_child_count(d));
  close_shell();
}

static void test_override_text_replaces_status_text_and_keeps_the_dot(void) {
  open_shell(480, 480, SIZE_SQUARE, PALETTE_DAY);
  ui_shell_set_source(UI_SRC_OFFLINE);
  ui_shell_set_status_override("SK ALERTS OFFLINE", ui_theme()->alarm);
  TEST_ASSERT_EQUAL_STRING("SK ALERTS OFFLINE", lv_label_get_text(ui_shell_status()));
  TEST_ASSERT_TRUE(shown(ui_shell_source_dot()));
  ui_shell_set_status_override(NULL, ui_theme()->alarm);
  TEST_ASSERT_EQUAL_STRING("SK OFFLINE", lv_label_get_text(ui_shell_status()));
  close_shell();
}

static void test_palette_change_restyles_the_dot(void) {
  open_shell(800, 480, SIZE_WIDE, PALETTE_DAY);
  ui_shell_set_source(UI_SRC_LIVE);
  lv_obj_t *d = ui_shell_source_dot();
  TEST_ASSERT_TRUE(lv_color_eq(lv_obj_get_style_bg_color(d, 0), ui_theme_colors(PALETTE_DAY)->fg));
  ui_shell_set_palette(PALETTE_NIGHT);
  TEST_ASSERT_TRUE(lv_color_eq(lv_obj_get_style_bg_color(d, 0), ui_theme_colors(PALETTE_NIGHT)->fg));
  close_shell();
}

static void test_home_button_returns_to_the_start_roles_first_page(void) {
  open_shell(800, 480, SIZE_WIDE, PALETTE_DAY);   // starts on role 0
  ui_shell_move(NAV_LEFT);
  ui_shell_move(NAV_UP);
  TEST_ASSERT_EQUAL_UINT8(1, ui_shell_nav()->role);
  TEST_ASSERT_EQUAL_UINT8(1, ui_shell_nav()->page);
  lv_obj_send_event(ui_shell_home_button(), LV_EVENT_CLICKED, NULL);
  TEST_ASSERT_EQUAL_UINT8(0, ui_shell_nav()->role);
  TEST_ASSERT_EQUAL_UINT8(0, ui_shell_nav()->page);
  close_shell();
}

int main(void) {
  lv_init();
  lv_display_t *d0 = lv_display_create(800, 480);
  lv_display_set_flush_cb(d0, flush);
  lv_display_set_buffers(d0, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  UNITY_BEGIN();
  RUN_TEST(test_offline_strip_clear_on_square);
  RUN_TEST(test_offline_strip_clear_on_800);
  RUN_TEST(test_offline_strip_clear_on_1024);
  RUN_TEST(test_marked_strip_clear_on_square);
  RUN_TEST(test_marked_strip_clear_on_800);
  RUN_TEST(test_marked_strip_clear_on_1024);
  RUN_TEST(test_clock_shows_text_and_hides_on_null);
  RUN_TEST(test_setting_the_same_clock_and_source_again_redraws_nothing);
  RUN_TEST(test_live_is_a_bare_dot_and_trouble_adds_text);
  RUN_TEST(test_dot_shape_differs_by_state_not_only_colour);
  RUN_TEST(test_override_text_replaces_status_text_and_keeps_the_dot);
  RUN_TEST(test_palette_change_restyles_the_dot);
  RUN_TEST(test_home_button_returns_to_the_start_roles_first_page);
  return UNITY_END();
}
