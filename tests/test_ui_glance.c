// The Glance page: layout per board size, tile contents, tap to open, the six-tile rule.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "alerts.h"
#include "glance_page.h"
#include "history.h"
#include "platform.h"
#include "ui_shell.h"
#include "unity.h"

static uint8_t buf[800 * 48 * 2];
static int flushes;
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  flushes++;
  lv_display_flush_ready(d);
}
static uint32_t ms;
static uint32_t tick_ms(void) { return ms; }

#define NOW platform_millis()   // the page reads the same clock
static cfg_t cfg;
static cfg_role_t rc[3];
static role_binding_t bind_engine, bind_power, bind_tanks, bind_glance;
static glance_source_t src[3];
static lv_obj_t *scr;

static void num(const char *path, double v) { bd_set_number(bd_intern(path), v, 1, NOW); }

static void fixtures(void) {
  num("propulsion.port.revolutions", 25.0);
  num("propulsion.port.oilPressure", 300000);
  num("propulsion.port.temperature", 358.15);
  num("propulsion.port.alternatorVoltage", 14.0);
  num("electrical.batteries.279.capacity.stateOfCharge", 0.74);
  num("electrical.batteries.279.voltage", 12.6);
  num("electrical.batteries.279.current", -4.8);
  num("tanks.freshWater.0.currentLevel", 0.64);
  num("tanks.fuel.0.currentLevel", 0.82);
}

static void bind_all(void) {
  const char *names[3] = {"engine", "power", "tanks"};
  role_binding_t *bs[3] = {&bind_engine, &bind_power, &bind_tanks};
  for (int i = 0; i < 3; i++) {
    memset(&rc[i], 0, sizeof rc[i]);
    snprintf(rc[i].role, sizeof rc[i].role, "%s", names[i]);
    role_bind(role_find(names[i]), &rc[i], &cfg.units, bs[i]);
    src[i] = (glance_source_t){bs[i], (uint8_t)(i + 1)};   // glance is role 0 in the shell
    if (bs[i]->def->tile) {
      glance_tile_t t = {0};
      bs[i]->def->tile(bs[i], NOW, &t);
      if (t.trend != BD_NONE) hist_track(t.trend);
    }
  }
  role_bind(role_find("glance"), NULL, &cfg.units, &bind_glance);
  glance_set_sources(src, 3);
}

void setUp(void) {
  ui_theme_set_current(PALETTE_DAY);
  bd_init();
  alerts_init();
  hist_init();
  cfg_defaults(&cfg);
  fixtures();
  bind_all();
  lv_obj_clean(scr);
}
void tearDown(void) {}

static lv_obj_t *build(int32_t w, int32_t h, size_class_t sc) {
  lv_obj_t *area = lv_obj_create(scr);
  lv_obj_remove_style_all(area);
  lv_obj_set_size(area, w, h);
  lv_obj_set_pos(area, 0, 0);
  role_find("glance")->build_page(area, 0, sc, &bind_glance);
  lv_obj_update_layout(area);
  return area;
}

static lv_coord_t top_of(uint8_t tile) {
  lv_area_t a = {0};
  if (!glance_part(tile, GP_TILE)) return -1;
  lv_obj_get_coords(glance_part(tile, GP_TILE), &a);
  return a.y1;
}
static lv_coord_t left_of(uint8_t tile) {
  lv_area_t a = {0};
  if (!glance_part(tile, GP_TILE)) return -1;
  lv_obj_get_coords(glance_part(tile, GP_TILE), &a);
  return a.x1;
}
static const char *txt(uint8_t tile, glance_part_t p) {
  lv_obj_t *o = glance_part(tile, p);
  return o ? lv_label_get_text(o) : "(no such part)";
}
static bool edge_is(uint8_t tile, lv_color_t c) {
  lv_obj_t *o = glance_part(tile, GP_EDGE);
  return o && lv_color_eq(lv_obj_get_style_bg_color(o, 0), c);
}
static bool shown(lv_obj_t *o) { return o && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN); }

// ---- registration and defaults ----

static void test_glance_is_a_known_role(void) {
  TEST_ASSERT_NOT_NULL(role_find("glance"));
  bool listed = false;
  for (const char *const *n = role_names(); *n; n++) listed = listed || !strcmp(*n, "glance");
  TEST_ASSERT_TRUE(listed);
  cfg_t c;
  cfg_defaults(&c);
  TEST_ASSERT_EQUAL_STRING("glance", c.roles[0].role);
}

// ---- layout ----

static void test_wide_board_lays_three_tiles_in_a_row_with_both_context_lines(void) {
  build(800, 440, SIZE_WIDE);
  TEST_ASSERT_EQUAL_UINT8(3, glance_tile_count());
  TEST_ASSERT_EQUAL_INT(top_of(0), top_of(1));
  TEST_ASSERT_EQUAL_INT(top_of(1), top_of(2));
  TEST_ASSERT_LESS_THAN_INT(left_of(1), left_of(0));
  TEST_ASSERT_LESS_THAN_INT(left_of(2), left_of(1));
  TEST_ASSERT_NOT_NULL(glance_part(1, GP_CONTEXT1));   // power: voltage line and time left
  TEST_ASSERT_NOT_NULL(glance_part(1, GP_CHART));   // power has a trend
  TEST_ASSERT_NULL(glance_part(0, GP_CHART));       // engine has none
}

static void test_square_board_uses_two_columns_and_drops_the_second_context_line(void) {
  build(480, 444, SIZE_SQUARE);
  TEST_ASSERT_EQUAL_UINT8(3, glance_tile_count());
  TEST_ASSERT_EQUAL_INT(top_of(0), top_of(1));
  TEST_ASSERT_LESS_THAN_INT(top_of(2), top_of(0));   // the third tile starts a new row
  for (uint8_t i = 0; i < 3; i++) TEST_ASSERT_NULL(glance_part(i, GP_CONTEXT1));
}

static void test_square_text_is_never_smaller_than_14(void) {
  build(480, 444, SIZE_SQUARE);
  for (uint8_t i = 0; i < 3; i++)
    for (int p = GP_TITLE; p <= GP_CONTEXT1; p++) {
      lv_obj_t *o = glance_part(i, (glance_part_t)p);
      if (!o || !lv_obj_check_type(o, &lv_label_class)) continue;
      TEST_ASSERT_GREATER_OR_EQUAL_INT(14, lv_obj_get_style_text_font(o, 0)->line_height - 2);
    }
}

// ---- contents ----

static void test_tiles_show_their_titles_headlines_and_states(void) {
  build(800, 440, SIZE_WIDE);
  TEST_ASSERT_EQUAL_STRING("ENGINE", txt(0, GP_TITLE));
  TEST_ASSERT_EQUAL_STRING("1500", txt(0, GP_HEADLINE));
  TEST_ASSERT_EQUAL_STRING("OK", txt(0, GP_WORD));
  TEST_ASSERT_EQUAL_STRING("74", txt(1, GP_HEADLINE));
  TEST_ASSERT_EQUAL_STRING("12.6 V  -4.8 A", txt(1, GP_CONTEXT0));
  TEST_ASSERT_EQUAL_STRING("TANKS", txt(2, GP_TITLE));
}

static void test_tile_edge_takes_the_state_colour(void) {
  build(800, 440, SIZE_WIDE);
  const ui_colors_t *c = ui_theme();
  TEST_ASSERT_TRUE(edge_is(0, c->ok));
  alert_t a = {0};
  snprintf(a.id, sizeof a.id, "propulsion.port.temperature");
  a.state = AL_ALARM;
  alerts_upsert(&a, NOW);
  role_find("glance")->refresh(&bind_glance, NOW);
  TEST_ASSERT_TRUE(edge_is(0, c->alarm));
  TEST_ASSERT_EQUAL_STRING("ALARM", txt(0, GP_WORD));
}

static void test_no_data_tile_shows_the_fault_style_and_no_number(void) {
  bd_set_invalid(bd_find("electrical.batteries.279.capacity.stateOfCharge"));
  build(800, 440, SIZE_WIDE);
  TEST_ASSERT_EQUAL_STRING("NO DATA", txt(1, GP_WORD));
  TEST_ASSERT_TRUE(shown(glance_part(1, GP_FAULT)));
  TEST_ASSERT_TRUE(!shown(glance_part(1, GP_HEADLINE)) || txt(1, GP_HEADLINE)[0] == 0);
  TEST_ASSERT_FALSE(shown(glance_part(0, GP_FAULT)));   // the healthy tile has no cross
}

static void test_refresh_follows_the_data_without_rebuilding(void) {
  build(800, 440, SIZE_WIDE);
  lv_obj_t *head = glance_part(0, GP_HEADLINE);
  TEST_ASSERT_NOT_NULL(head);
  num("propulsion.port.revolutions", 30.0);
  role_find("glance")->refresh(&bind_glance, NOW);
  TEST_ASSERT_EQUAL_PTR(head, glance_part(0, GP_HEADLINE));
  TEST_ASSERT_EQUAL_STRING("1800", txt(0, GP_HEADLINE));
}

static void test_trend_plots_history_after_a_backfill_and_refresh(void) {
  hist_tick(NOW);   // the record is running
  build(800, 440, SIZE_WIDE);
  TEST_ASSERT_LESS_OR_EQUAL_UINT16(2, glance_trend_points(1));   // a minute of record: next to nothing
  float v[300];
  for (int i = 0; i < 300; i++) v[i] = 0.9f - 0.001f * (float)i;
  bd_id_t soc = bd_find("electrical.batteries.279.capacity.stateOfCharge");
  hist_backfill(soc, v, 300);
  role_find("glance")->refresh(&bind_glance, NOW + 10000);
  TEST_ASSERT_GREATER_THAN_UINT16(50, glance_trend_points(1));
  TEST_ASSERT_EQUAL_UINT16(0, glance_trend_points(0));   // the engine tile has no trend
}

static void test_a_tank_bar_with_no_level_says_fail_not_a_number(void) {
  bd_set_invalid(bd_find("tanks.freshWater.0.currentLevel"));
  build(800, 440, SIZE_WIDE);
  lv_obj_t *pct = glance_bar_pct(2, 0);
  TEST_ASSERT_NOT_NULL(pct);
  TEST_ASSERT_EQUAL_STRING("FAIL", lv_label_get_text(pct));
  TEST_ASSERT_EQUAL_STRING("82%", lv_label_get_text(glance_bar_pct(2, 1)));   // the fuel bar is still good
}

static void test_an_idle_refresh_redraws_nothing(void) {   // the panel's bus is the scarce thing: no needless invalidation
  build(800, 440, SIZE_WIDE);
  lv_refr_now(NULL);
  role_find("glance")->refresh(&bind_glance, NOW);
  lv_refr_now(NULL);
  int before = flushes;
  for (int i = 0; i < 5; i++) role_find("glance")->refresh(&bind_glance, NOW);   // nothing changed
  lv_refr_now(NULL);
  TEST_ASSERT_EQUAL_INT(before, flushes);
}

// ---- tap ----

static const ui_role_view_t *views_ptr;
static void build_glance(lv_obj_t *area, uint8_t page, size_class_t sc, void *ctx) {
  (void)ctx;
  role_find("glance")->build_page(area, page, sc, &bind_glance);
}
static void build_none(lv_obj_t *area, uint8_t page, size_class_t sc, void *ctx) { (void)area; (void)page; (void)sc; (void)ctx; }

static void test_tapping_a_tile_opens_that_role(void) {
  static const ui_role_view_t views[] = {
    {.name = "GLANCE", .page_count = 1, .build_page = build_glance},
    {.name = "ENGINE", .page_count = 2, .build_page = build_none},
    {.name = "POWER", .page_count = 3, .build_page = build_none},
    {.name = "TANKS", .page_count = 1, .build_page = build_none}};
  views_ptr = views;
  lv_display_t *d = lv_display_get_default();
  ui_shell_create(d, SIZE_WIDE, PALETTE_DAY, views, 4, 0);
  lv_timer_handler();
  TEST_ASSERT_EQUAL_UINT8(3, glance_tile_count());
  TEST_ASSERT_NOT_NULL(glance_part(1, GP_TILE));
  lv_obj_send_event(glance_part(1, GP_TILE), LV_EVENT_CLICKED, NULL);
  ms += 50;
  lv_timer_handler();   // the move runs after the event, not inside it
  TEST_ASSERT_EQUAL_UINT8(2, ui_shell_nav()->role);   // power
  TEST_ASSERT_EQUAL_UINT8(0, ui_shell_nav()->page);
}

// ---- the six-tile rule ----

static void test_pick_keeps_everything_when_it_fits(void) {
  const char *names[] = {"engine", "power", "tanks"};
  gt_state_t st[] = {GT_OK, GT_OK, GT_OK};
  size_t out[6];
  TEST_ASSERT_EQUAL_UINT(3, glance_pick(names, st, 3, 6, out));
  TEST_ASSERT_EQUAL_UINT(0, out[0]);
  TEST_ASSERT_EQUAL_UINT(1, out[1]);
  TEST_ASSERT_EQUAL_UINT(2, out[2]);
}

static void test_pick_drops_the_lowest_priority_quiet_tile(void) {
  const char *names[] = {"tanks", "engine", "power", "wind", "depth", "anchor", "autopilot"};   // config order
  gt_state_t st[] = {GT_OK, GT_OK, GT_OK, GT_OK, GT_OK, GT_OK, GT_OK};
  size_t out[6];
  TEST_ASSERT_EQUAL_UINT(6, glance_pick(names, st, 7, 6, out));
  for (size_t i = 0; i < 6; i++) TEST_ASSERT_NOT_EQUAL_UINT(6, out[i]);   // an unlisted role ranks last: "autopilot" goes
  TEST_ASSERT_EQUAL_UINT(0, out[0]);                                      // order kept
}

static void test_pick_never_drops_an_alarm_for_a_quiet_tile(void) {
  const char *names[] = {"anchor", "depth", "wind", "power", "engine", "tanks", "autopilot"};
  gt_state_t st[] = {GT_OK, GT_OK, GT_OK, GT_OK, GT_OK, GT_OK, GT_ALARM};
  size_t out[6];
  TEST_ASSERT_EQUAL_UINT(6, glance_pick(names, st, 7, 6, out));
  bool kept_alarm = false, kept_tanks = false;
  for (size_t i = 0; i < 6; i++) {
    kept_alarm = kept_alarm || out[i] == 6;
    kept_tanks = kept_tanks || out[i] == 5;
  }
  TEST_ASSERT_TRUE(kept_alarm);
  TEST_ASSERT_FALSE(kept_tanks);   // the lowest-priority quiet listed role
}

static void test_pick_when_every_tile_alerts_drops_the_lowest_priority_non_alarm(void) {
  const char *names[] = {"anchor", "depth", "wind", "power", "engine", "tanks", "autopilot"};
  gt_state_t st[] = {GT_WARN, GT_WARN, GT_WARN, GT_WARN, GT_WARN, GT_ALARM, GT_ALARM};
  size_t out[6];
  TEST_ASSERT_EQUAL_UINT(6, glance_pick(names, st, 7, 6, out));
  for (size_t i = 0; i < 6; i++) TEST_ASSERT_NOT_EQUAL_UINT(4, out[i]);   // engine: the lowest-priority warn
}

int main(void) {
  lv_init();
  lv_tick_set_cb(tick_ms);
  lv_display_t *disp = lv_display_create(800, 480);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  scr = lv_screen_active();
  UNITY_BEGIN();
  RUN_TEST(test_glance_is_a_known_role);
  RUN_TEST(test_wide_board_lays_three_tiles_in_a_row_with_both_context_lines);
  RUN_TEST(test_square_board_uses_two_columns_and_drops_the_second_context_line);
  RUN_TEST(test_square_text_is_never_smaller_than_14);
  RUN_TEST(test_tiles_show_their_titles_headlines_and_states);
  RUN_TEST(test_tile_edge_takes_the_state_colour);
  RUN_TEST(test_no_data_tile_shows_the_fault_style_and_no_number);
  RUN_TEST(test_refresh_follows_the_data_without_rebuilding);
  RUN_TEST(test_trend_plots_history_after_a_backfill_and_refresh);
  RUN_TEST(test_a_tank_bar_with_no_level_says_fail_not_a_number);
  RUN_TEST(test_an_idle_refresh_redraws_nothing);
  RUN_TEST(test_tapping_a_tile_opens_that_role);
  RUN_TEST(test_pick_keeps_everything_when_it_fits);
  RUN_TEST(test_pick_drops_the_lowest_priority_quiet_tile);
  RUN_TEST(test_pick_never_drops_an_alarm_for_a_quiet_tile);
  RUN_TEST(test_pick_when_every_tile_alerts_drops_the_lowest_priority_non_alarm);
  return UNITY_END();
}
