#include <math.h>
#include "unity.h"
#include "ui_rose.h"
#include "ui_theme.h"

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

static void test_xte_shift_zero_and_nan(void) {
  TEST_ASSERT_EQUAL_DOUBLE(0, ui_rose_xte_shift_px(0, 100));
  TEST_ASSERT_EQUAL_DOUBLE(0, ui_rose_xte_shift_px(NAN, 100));
}

static void test_xte_shift_one_dot_is_0p1nm(void) {
  double dot = ui_rose_dot_px(100);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 21.0, dot);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, -dot, ui_rose_xte_shift_px(185.2, 100));   // boat right of track: line moves left
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, dot, ui_rose_xte_shift_px(-185.2, 100));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, -dot / 2, ui_rose_xte_shift_px(92.6, 100));
}

static void test_xte_shift_clamps_at_two_dots(void) {
  double dot = ui_rose_dot_px(150);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, -2 * dot, ui_rose_xte_shift_px(5 * 1852.0, 150));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 2 * dot, ui_rose_xte_shift_px(-5 * 1852.0, 150));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, -2 * dot, ui_rose_xte_shift_px(INFINITY, 150));
}

static void test_dot_px_scales_with_radius(void) {
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 2 * ui_rose_dot_px(80), ui_rose_dot_px(160));
}

static void test_radius_leaves_a_margin_for_the_markers(void) {
  TEST_ASSERT_EQUAL(172, ui_rose_radius_px(420));
  TEST_ASSERT_EQUAL(150, ui_rose_radius_px(376));
  TEST_ASSERT_EQUAL(0, ui_rose_radius_px(10));   // never negative
}

static void test_rose_draws_all_states(void) {
  for (int p = PALETTE_DAY; p <= PALETTE_NIGHT; p++) {
    ui_theme_set_current((palette_t)p);
    lv_obj_t *r = ui_rose_create(scr);
    lv_obj_set_size(r, 376, 376);
    ui_rose_data_t all = {.have_heading = true, .heading = 0.82, .course = true, .dtk = 1.22, .xte_m = 222,
                          .have_cog = true, .cog = 1.22, .ap_bug = true, .ap_target = 0.91, .have_awa = true,
                          .awa = 0.66, .have_twa = true, .twa = 1.07,
                          .have_mob = true, .mob = 2.5};
    ui_rose_set(r, &all);
    lv_refr_now(NULL);
    ui_rose_data_t none = {.xte_m = NAN};
    ui_rose_set(r, &none);   // no heading, nothing else
    lv_refr_now(NULL);
    ui_rose_data_t wind_only = {.xte_m = NAN, .have_awa = true, .awa = -1.0, .have_twa = true, .twa = 7.0};
    ui_rose_set(r, &wind_only);   // wind is off the bow: it needs no heading
    lv_refr_now(NULL);
    ui_rose_data_t odd = {.have_heading = true, .heading = -3.0, .course = true, .dtk = 9.0, .xte_m = 1e9,
                          .have_cog = true, .cog = -1.0, .ap_bug = true, .ap_target = 9.0, .have_awa = true, .awa = 7.0,
                          .have_mob = true, .mob = -9.0};
    ui_rose_set(r, &odd);   // angles outside one turn, a cross-track error off the scale
    lv_refr_now(NULL);
    ui_rose_data_t nan_heading = {.have_heading = true, .heading = NAN, .course = true, .dtk = NAN, .xte_m = NAN,
                                  .ap_bug = true, .ap_target = NAN, .have_awa = true, .awa = NAN,
                                  .have_mob = true, .mob = NAN};
    ui_rose_set(r, &nan_heading);   // flags say yes, numbers say no: nothing may be drawn from a NAN
    lv_refr_now(NULL);
    lv_obj_delete(r);
  }
  ui_theme_set_current(PALETTE_DAY);
  TEST_ASSERT_GREATER_THAN_INT(0, flushes);
}

int main(void) {
  lv_init();
  lv_display_t *disp = lv_display_create(480, 480);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  scr = lv_screen_active();
  UNITY_BEGIN();
  RUN_TEST(test_xte_shift_zero_and_nan);
  RUN_TEST(test_xte_shift_one_dot_is_0p1nm);
  RUN_TEST(test_xte_shift_clamps_at_two_dots);
  RUN_TEST(test_dot_px_scales_with_radius);
  RUN_TEST(test_radius_leaves_a_margin_for_the_markers);
  RUN_TEST(test_rose_draws_all_states);
  return UNITY_END();
}
