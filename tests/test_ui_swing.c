#include <math.h>
#include <string.h>
#include "unity.h"
#include "ui_swing.h"

static uint8_t buf[480 * 48 * 2];
static int flushes;
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  flushes++;
  lv_display_flush_ready(d);
}
static lv_obj_t *scr;

void setUp(void) { lv_obj_clean(scr); }
void tearDown(void) {}

static void test_scale_ring_fills_80_percent(void) {
  double s = ui_swing_scale(50, 20, 400);   // plot 400 px: 200 px from the centre to the edge
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.8 * 200, 50 * s);
}

static void test_scale_grows_when_boat_outside_ring(void) {
  double s = ui_swing_scale(50, 70, 400);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 200 / 77.0, s);   // the farthest point, plus 10%, just fits
  TEST_ASSERT_LESS_THAN_DOUBLE(ui_swing_scale(50, 20, 400), s);
}

static void test_scale_no_limit_uses_distance(void) {
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 200 / 22.0, ui_swing_scale(NAN, 20, 400));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 200 / 22.0, ui_swing_scale(0, 20, 400));
}

static void test_scale_nothing_known_is_finite(void) {
  double s = ui_swing_scale(NAN, NAN, 400);
  TEST_ASSERT_TRUE(isfinite(s));
  TEST_ASSERT_GREATER_THAN_DOUBLE(0, s);
  TEST_ASSERT_TRUE(isfinite(ui_swing_scale(50, 0, 0)));
}

static void test_create_and_set_does_not_crash(void) {
  lv_obj_t *sw = ui_swing_create(scr);
  lv_obj_set_size(sw, 400, 400);
  static const ui_swing_pt_t track[] = {{0, 10}, {2, 12}, {NAN, NAN}, {5, 14}, {8, 20}, {-3, 25}};
  for (int st = UI_SWING_GREEN; st <= UI_SWING_GREY; st++) {
    ui_swing_data_t d = {.track = track, .track_n = 6, .limit_m = 40, .warn_m = 30, .state = (ui_swing_state_t)st, .show_n = true};
    ui_swing_set(sw, &d);
    lv_refr_now(NULL);
  }
  ui_swing_data_t none = {.track = NULL, .track_n = 0, .limit_m = NAN, .warn_m = NAN, .state = UI_SWING_GREY};
  ui_swing_set(sw, &none);   // not set / no data: nothing to plot
  lv_refr_now(NULL);
  TEST_ASSERT_GREATER_THAN_INT(0, flushes);
}

int main(void) {
  lv_init();
  lv_display_t *disp = lv_display_create(480, 480);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  scr = lv_screen_active();
  UNITY_BEGIN();
  RUN_TEST(test_scale_ring_fills_80_percent);
  RUN_TEST(test_scale_grows_when_boat_outside_ring);
  RUN_TEST(test_scale_no_limit_uses_distance);
  RUN_TEST(test_scale_nothing_known_is_finite);
  RUN_TEST(test_create_and_set_does_not_crash);
  return UNITY_END();
}
