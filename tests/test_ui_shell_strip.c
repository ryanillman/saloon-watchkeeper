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

static void open_shell_with(const ui_role_view_t *v, uint8_t count, int32_t w, int32_t h, size_class_t sc, palette_t pal) {
  old_disp = lv_display_get_default();
  disp = lv_display_create(w, h);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_default(disp);
  ui_shell_create(disp, sc, pal, v, count, 0);
  ui_alerts_attach(lv_display_get_screen_active(disp), ui_shell_strip(), sc, role_of, NULL);
}
static void open_shell(int32_t w, int32_t h, size_class_t sc, palette_t pal) { open_shell_with(views, 2, w, h, sc, pal); }
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
  lv_obj_t *p[] = {ui_shell_home_button(), ui_shell_name(), ui_shell_dots(), ui_shell_clock(), ui_shell_source_dot(), ui_shell_can_dot(), ui_shell_status(),
                   ui_alerts_part(UI_ALERTS_MARKER), ui_alerts_part(UI_ALERTS_ACKED_MARKER), ui_alerts_part(UI_ALERTS_OFFLINE), ui_shell_sos()};
  const char *n[] = {"home", "name", "dots", "clock", "source dot", "bus dot", "status", "marker", "acked marker", "offline", "sos"};
  for (size_t i = 0; i < sizeof p / sizeof p[0]; i++)
    for (size_t j = i + 1; j < sizeof p / sizeof p[0]; j++) {
      if (p[i] == p[j] || !shown(p[i]) || !shown(p[j])) continue;   // square: the offline text is the status label
      lv_area_t ai, aj;
      lv_obj_get_coords(p[i], &ai);
      lv_obj_get_coords(p[j], &aj);
      char msg[96];
      snprintf(msg, sizeof msg, "%s [%d..%d] overlaps %s [%d..%d]", n[i], (int)ai.x1, (int)ai.x2, n[j], (int)aj.x1, (int)aj.x2);
      TEST_ASSERT_FALSE_MESSAGE(meet(p[i], p[j]), msg);
    }
}

// Signal K down: the status text and "ALARMS DISABLED" show, the alert list is empty.
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

// The SOS button (shown while a MOB is declared) takes the role name's place; nothing else in the strip moves under it.
static void sos_strip(int32_t w, int32_t h, size_class_t sc) {
  open_shell(w, h, sc, PALETTE_DAY);
  ui_shell_set_clock("02:14");
  ui_shell_set_source(UI_SRC_LIVE);
  raise("a.warn", AL_WARN, false);
  raise("a.alarm", AL_ALARM, true);
  ui_shell_set_sos(true);
  update(true);
  TEST_ASSERT_TRUE(shown(ui_shell_sos()));
  TEST_ASSERT_TRUE(shown(ui_alerts_part(UI_ALERTS_MARKER)));
  TEST_ASSERT_TRUE(shown(ui_alerts_part(UI_ALERTS_ACKED_MARKER)));   // the bell is still there
  check_no_overlap();
  lv_area_t a;
  lv_obj_get_coords(ui_shell_sos(), &a);
  int32_t x = (a.x1 + a.x2) / 2, height = a.y2 - a.y1 + 1;
  lv_point_t above = {x, a.y1 - 8}, below = {x, a.y2 + 8}, far_below = {x, a.y2 + 20};
  TEST_ASSERT_TRUE(lv_obj_hit_test(ui_shell_sos(), &above));
  TEST_ASSERT_TRUE(lv_obj_hit_test(ui_shell_sos(), &below));      // 8 px of reach each way
  TEST_ASSERT_FALSE(lv_obj_hit_test(ui_shell_sos(), &far_below));
  TEST_ASSERT_GREATER_OR_EQUAL_INT32_MESSAGE(48, height + 16, "a touch target big enough for a wet thumb");
  close_shell();
}

static int sos_taps;
static void on_sos(void) { sos_taps++; }

static void sos_basics(int32_t w, int32_t h, size_class_t sc) {
  open_shell(w, h, sc, PALETTE_DAY);
  TEST_ASSERT_NOT_NULL(ui_shell_sos());
  TEST_ASSERT_FALSE(shown(ui_shell_sos()));   // hidden until a MOB
  TEST_ASSERT_TRUE(shown(ui_shell_name()));
  sos_taps = 0;
  ui_shell_set_sos_cb(on_sos);
  ui_shell_set_sos(true);
  update(true);
  TEST_ASSERT_TRUE(shown(ui_shell_sos()));
  TEST_ASSERT_FALSE(shown(ui_shell_name()));   // the name makes way
  lv_obj_send_event(ui_shell_sos(), LV_EVENT_CLICKED, NULL);
  TEST_ASSERT_EQUAL(1, sos_taps);
  ui_shell_set_sos(false);
  update(true);
  TEST_ASSERT_FALSE(shown(ui_shell_sos()));
  TEST_ASSERT_TRUE(shown(ui_shell_name()));
  ui_shell_set_sos_cb(NULL);
  close_shell();
}

static void test_sos_basics_on_square(void) { sos_basics(480, 480, SIZE_SQUARE); }
static void test_sos_basics_on_800(void) { sos_basics(800, 480, SIZE_WIDE); }
static void test_sos_clear_of_the_markers_on_square(void) { sos_strip(480, 480, SIZE_SQUARE); }
static void test_sos_clear_of_the_markers_on_800(void) { sos_strip(800, 480, SIZE_WIDE); }
static void test_sos_clear_of_the_markers_on_1024(void) { sos_strip(1024, 600, SIZE_WIDE); }

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
  ui_shell_set_status_override("ALARMS DISABLED", ui_theme()->alarm);
  TEST_ASSERT_EQUAL_STRING("ALARMS DISABLED", lv_label_get_text(ui_shell_status()));
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


// Every role name the app has, with as many pages as a role gets: the clock and the page dots must clear the name on
// every page (the Navigation title ran into the clock on the square board).
static const ui_role_view_t all_roles[] = {
  {.name = "GLANCE", .page_count = 1}, {.name = "ENGINE", .page_count = 4}, {.name = "POWER", .page_count = 6},
  {.name = "TANKS", .page_count = 1},  {.name = "ANCHOR", .page_count = 2}, {.name = "NAVIGATION", .page_count = 3},
  {.name = "DEPTH", .page_count = 2},  {.name = "NAVIGATION", .page_count = 6},
};
#define ALL_ROLES (sizeof all_roles / sizeof all_roles[0])

static void every_role_clears_the_clock(int32_t w, int32_t h, size_class_t sc) {
  open_shell_with(all_roles, ALL_ROLES, w, h, sc, PALETTE_DAY);
  ui_shell_set_clock("23:39");
  ui_shell_set_source(UI_SRC_LIVE);
  for (uint8_t r = 0; r < ALL_ROLES; r++)
    for (uint8_t pg = 0; pg < all_roles[r].page_count; pg++) {
      ui_shell_goto(r, pg);
      update(true);
      char msg[48];
      snprintf(msg, sizeof msg, "%s page %u", all_roles[r].name, (unsigned)pg);
      TEST_ASSERT_TRUE_MESSAGE(shown(ui_shell_clock()), msg);
      TEST_ASSERT_FALSE_MESSAGE(meet(ui_shell_name(), ui_shell_clock()), msg);
      check_no_overlap();
    }
  close_shell();
}
static void test_every_role_clears_the_clock_on_square(void) { every_role_clears_the_clock(480, 480, SIZE_SQUARE); }
static void test_every_role_clears_the_clock_on_800(void) { every_role_clears_the_clock(800, 480, SIZE_WIDE); }
static void test_every_role_clears_the_clock_on_1024(void) { every_role_clears_the_clock(1024, 600, SIZE_WIDE); }


// ---- the bus dot and the status text that names what is not live ----
static void sources(bool sk_shown, ui_src_t sk, bool can_shown, ui_src_t can) {
  ui_shell_set_sources((ui_source_t){sk_shown, sk}, (ui_source_t){can_shown, can});
  update(sk == UI_SRC_LIVE);
}

static void test_bus_dot_is_hidden_until_the_bus_is_in_use(void) {
  open_shell(480, 480, SIZE_SQUARE, PALETTE_DAY);
  TEST_ASSERT_NOT_NULL(ui_shell_can_dot());
  TEST_ASSERT_TRUE(lv_obj_has_flag(ui_shell_can_dot(), LV_OBJ_FLAG_HIDDEN));
  sources(true, UI_SRC_LIVE, true, UI_SRC_LIVE);
  TEST_ASSERT_FALSE(lv_obj_has_flag(ui_shell_can_dot(), LV_OBJ_FLAG_HIDDEN));
  sources(true, UI_SRC_LIVE, false, UI_SRC_LIVE);
  TEST_ASSERT_TRUE(lv_obj_has_flag(ui_shell_can_dot(), LV_OBJ_FLAG_HIDDEN));
  close_shell();
}

static void test_bus_dot_is_a_rounded_square_and_signal_k_a_circle(void) {
  open_shell(480, 480, SIZE_SQUARE, PALETTE_DAY);
  sources(true, UI_SRC_LIVE, true, UI_SRC_LIVE);
  TEST_ASSERT_EQUAL_INT32(LV_RADIUS_CIRCLE, lv_obj_get_style_radius(ui_shell_source_dot(), 0));
  TEST_ASSERT_NOT_EQUAL_INT32(LV_RADIUS_CIRCLE, lv_obj_get_style_radius(ui_shell_can_dot(), 0));
  close_shell();
}

static void test_bus_dot_shape_differs_by_state(void) {
  open_shell(480, 480, SIZE_SQUARE, PALETTE_DAY);
  sources(true, UI_SRC_LIVE, true, UI_SRC_LIVE);
  TEST_ASSERT_EQUAL_UINT32(0, lv_obj_get_child_count(ui_shell_can_dot()));                  // filled
  TEST_ASSERT_EQUAL_INT32(LV_OPA_COVER, lv_obj_get_style_bg_opa(ui_shell_can_dot(), 0));
  sources(true, UI_SRC_LIVE, true, UI_SRC_CONNECTING);
  TEST_ASSERT_EQUAL_UINT32(0, lv_obj_get_child_count(ui_shell_can_dot()));                  // hollow ring
  TEST_ASSERT_EQUAL_INT32(LV_OPA_TRANSP, lv_obj_get_style_bg_opa(ui_shell_can_dot(), 0));
  sources(true, UI_SRC_LIVE, true, UI_SRC_OFFLINE);
  TEST_ASSERT_GREATER_THAN_UINT32(0, lv_obj_get_child_count(ui_shell_can_dot()));           // ring with a cross
  close_shell();
}

static void test_status_text_names_what_is_not_live(void) {
  open_shell(800, 480, SIZE_WIDE, PALETTE_DAY);
  struct { bool sk_shown; ui_src_t sk; bool can_shown; ui_src_t can; const char *want; } t[] = {
    {true, UI_SRC_LIVE, false, UI_SRC_LIVE, ""},
    {true, UI_SRC_OFFLINE, false, UI_SRC_OFFLINE, "SK OFFLINE"},
    {true, UI_SRC_CONNECTING, false, UI_SRC_OFFLINE, "SK CONNECTING"},
    {true, UI_SRC_LIVE, true, UI_SRC_LIVE, ""},
    {true, UI_SRC_LIVE, true, UI_SRC_OFFLINE, "CAN OFFLINE"},
    {true, UI_SRC_LIVE, true, UI_SRC_CONNECTING, "CAN CONNECTING"},
    {true, UI_SRC_OFFLINE, true, UI_SRC_LIVE, "SK OFFLINE"},
    {true, UI_SRC_OFFLINE, true, UI_SRC_OFFLINE, "SK + CAN OFFLINE"},
    {true, UI_SRC_CONNECTING, true, UI_SRC_CONNECTING, "SK + CAN CONNECTING"},
    {true, UI_SRC_OFFLINE, true, UI_SRC_CONNECTING, "SK OFFLINE"},      // the worse state is named; the shapes show the rest
    {true, UI_SRC_CONNECTING, true, UI_SRC_OFFLINE, "CAN OFFLINE"},
    {false, UI_SRC_OFFLINE, true, UI_SRC_OFFLINE, "CAN OFFLINE"},       // bus only: Signal K is not in use and never named
    {false, UI_SRC_OFFLINE, true, UI_SRC_LIVE, ""},
  };
  for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
    sources(t[i].sk_shown, t[i].sk, t[i].can_shown, t[i].can);
    char msg[48];
    snprintf(msg, sizeof msg, "case %u", (unsigned)i);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(t[i].want, lv_label_get_text(ui_shell_status()), msg);
  }
  close_shell();
}

static void test_bus_only_hides_the_signal_k_dot_and_the_bus_dot_takes_its_place(void) {
  open_shell(480, 480, SIZE_SQUARE, PALETTE_DAY);
  sources(true, UI_SRC_LIVE, false, UI_SRC_LIVE);
  lv_area_t where;
  lv_obj_get_coords(ui_shell_source_dot(), &where);
  sources(false, UI_SRC_OFFLINE, true, UI_SRC_LIVE);
  TEST_ASSERT_TRUE(lv_obj_has_flag(ui_shell_source_dot(), LV_OBJ_FLAG_HIDDEN));
  lv_area_t now;
  lv_obj_get_coords(ui_shell_can_dot(), &now);
  TEST_ASSERT_EQUAL_INT32(where.x2, now.x2);   // at the strip's right edge, where the Signal K dot was
  close_shell();
}

// The roles as they really are (Navigation has 2 pages, Power starts at 3 and grows with the batteries; 6 is generous):
// the strip's widest real combination. The stress list above (a six-page Navigation) is for the clock alone.
static const ui_role_view_t real_roles[] = {
  {.name = "GLANCE", .page_count = 1}, {.name = "ENGINE", .page_count = 2}, {.name = "POWER", .page_count = 6},
  {.name = "TANKS", .page_count = 1},  {.name = "ANCHOR", .page_count = 1}, {.name = "NAVIGATION", .page_count = 2},
  {.name = "DEPTH", .page_count = 1},
};
#define REAL_ROLES (sizeof real_roles / sizeof real_roles[0])

// Both dots, every status text and the alert markers together: nothing in the strip overlaps, on every size.
static void both_sources_strip(int32_t w, int32_t h, size_class_t sc) {
  static const struct { ui_src_t sk, can; } combos[] = {
    {UI_SRC_LIVE, UI_SRC_LIVE}, {UI_SRC_OFFLINE, UI_SRC_OFFLINE}, {UI_SRC_CONNECTING, UI_SRC_CONNECTING},
    {UI_SRC_OFFLINE, UI_SRC_LIVE}, {UI_SRC_LIVE, UI_SRC_OFFLINE},
  };
  for (size_t i = 0; i < sizeof combos / sizeof combos[0]; i++) {
    for (int marked = 0; marked < 2; marked++) {
      open_shell_with(real_roles, REAL_ROLES, w, h, sc, PALETTE_DAY);
      ui_shell_set_clock("23:39");
      alerts_init();
      alerts_source_up(combos[i].sk == UI_SRC_LIVE);
      if (marked && combos[i].sk == UI_SRC_LIVE) {
        raise("a.warn", AL_WARN, false);
        raise("a.alarm", AL_ALARM, true);
      }
      for (uint8_t r = 0; r < REAL_ROLES; r++) {
        ui_shell_goto(r, 0);
        ui_shell_set_sources((ui_source_t){true, combos[i].sk}, (ui_source_t){true, combos[i].can});
        update(combos[i].sk == UI_SRC_LIVE);
        char msg[64];
        snprintf(msg, sizeof msg, "%s sk=%d can=%d marked=%d", real_roles[r].name, combos[i].sk, combos[i].can, marked);
        check_no_overlap();
        TEST_ASSERT_FALSE_MESSAGE(meet(ui_shell_name(), ui_shell_clock()), msg);
      }
      close_shell();
    }
  }
}
static void test_both_sources_strip_clear_on_square(void) { both_sources_strip(480, 480, SIZE_SQUARE); }
static void test_both_sources_strip_clear_on_800(void) { both_sources_strip(800, 480, SIZE_WIDE); }
static void test_both_sources_strip_clear_on_1024(void) { both_sources_strip(1024, 600, SIZE_WIDE); }

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
  RUN_TEST(test_every_role_clears_the_clock_on_square);
  RUN_TEST(test_every_role_clears_the_clock_on_800);
  RUN_TEST(test_every_role_clears_the_clock_on_1024);
  RUN_TEST(test_bus_dot_is_hidden_until_the_bus_is_in_use);
  RUN_TEST(test_bus_dot_is_a_rounded_square_and_signal_k_a_circle);
  RUN_TEST(test_bus_dot_shape_differs_by_state);
  RUN_TEST(test_status_text_names_what_is_not_live);
  RUN_TEST(test_bus_only_hides_the_signal_k_dot_and_the_bus_dot_takes_its_place);
  RUN_TEST(test_both_sources_strip_clear_on_square);
  RUN_TEST(test_both_sources_strip_clear_on_800);
  RUN_TEST(test_both_sources_strip_clear_on_1024);
  RUN_TEST(test_clock_shows_text_and_hides_on_null);
  RUN_TEST(test_setting_the_same_clock_and_source_again_redraws_nothing);
  RUN_TEST(test_live_is_a_bare_dot_and_trouble_adds_text);
  RUN_TEST(test_dot_shape_differs_by_state_not_only_colour);
  RUN_TEST(test_override_text_replaces_status_text_and_keeps_the_dot);
  RUN_TEST(test_sos_basics_on_square);
  RUN_TEST(test_sos_basics_on_800);
  RUN_TEST(test_sos_clear_of_the_markers_on_square);
  RUN_TEST(test_sos_clear_of_the_markers_on_800);
  RUN_TEST(test_sos_clear_of_the_markers_on_1024);
  RUN_TEST(test_palette_change_restyles_the_dot);
  RUN_TEST(test_home_button_returns_to_the_start_roles_first_page);
  return UNITY_END();
}
