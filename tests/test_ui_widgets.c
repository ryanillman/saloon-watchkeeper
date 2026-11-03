#include <string.h>
#include "unity.h"
#include "ui_gauge.h"

static uint8_t buf[480 * 48 * 2];
static int flushes;
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  flushes++;
  lv_display_flush_ready(d);
}

static lv_obj_t *scr;
static const ui_band_t bands[] = {{3000, 4000, {0}}};
static const ui_gauge_spec_t rpm = {.label = "RPM", .unit = "RPM", .lo = 0, .hi = 4000,
                                    .sweep = SWEEP_THREE_QUARTER, .bands = bands, .band_count = 1, .decimals = 0};

void setUp(void) { lv_obj_clean(scr); }
void tearDown(void) {}

static void render(void) { lv_refr_now(NULL); }

static void test_dial_starts_faulted(void) {
  lv_obj_t *d = ui_dial_create(scr, &rpm, 1, NULL);
  render();
  TEST_ASSERT_FALSE(ui_dial_needle_visible(d, 0));
  TEST_ASSERT_TRUE(ui_field_faulted(d));
}

static void test_dial_valid_shows_needle(void) {
  lv_obj_t *d = ui_dial_create(scr, &rpm, 1, NULL);
  ui_dial_set(d, 0, 2000, true);
  render();
  TEST_ASSERT_TRUE(ui_dial_needle_visible(d, 0));
  TEST_ASSERT_FALSE(ui_field_faulted(d));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.5, ui_dial_needle_fraction(d, 0));
  TEST_ASSERT_EQUAL_STRING("2000", ui_dial_text(d, 0));
}

static void test_dial_invalid_hides_needle_and_shows_fault(void) {
  lv_obj_t *d = ui_dial_create(scr, &rpm, 1, NULL);
  ui_dial_set(d, 0, 2000, true);
  ui_dial_set(d, 0, 0, false);
  render();
  TEST_ASSERT_FALSE(ui_dial_needle_visible(d, 0));
  TEST_ASSERT_TRUE(ui_field_faulted(d));
}

static void test_dial_one_of_two_failed_flags_that_needle_only(void) {
  const char *labels[] = {"P", "S"};
  lv_obj_t *d = ui_dial_create(scr, &rpm, 2, labels);
  ui_dial_set(d, 0, 1800, true);
  ui_dial_set(d, 1, 0, false);
  render();
  TEST_ASSERT_TRUE(ui_dial_needle_visible(d, 0));
  TEST_ASSERT_FALSE(ui_dial_needle_visible(d, 1));
  TEST_ASSERT_FALSE(ui_field_faulted(d));          // the dial as a whole is still live
  TEST_ASSERT_TRUE(ui_dial_needle_faulted(d, 1));
  TEST_ASSERT_EQUAL_STRING("1800", ui_dial_text(d, 0));
}

static void test_dial_out_of_range_clamps_needle_readout_true(void) {
  lv_obj_t *d = ui_dial_create(scr, &rpm, 1, NULL);
  ui_dial_set(d, 0, 4500, true);
  render();
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, ui_dial_needle_fraction(d, 0));
  TEST_ASSERT_EQUAL_STRING("4500", ui_dial_text(d, 0));
}

static void test_strip_invalid_then_valid(void) {
  ui_gauge_spec_t oil = {.label = "OIL", .unit = "bar", .lo = 0, .hi = 6, .decimals = 1};
  lv_obj_t *s = ui_strip_create(scr, &oil, false);
  render();
  TEST_ASSERT_TRUE(ui_field_faulted(s));
  ui_strip_set(s, 3.0, true);
  render();
  TEST_ASSERT_FALSE(ui_field_faulted(s));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.5, ui_strip_fraction(s));
  TEST_ASSERT_EQUAL_STRING("3.0", ui_strip_text(s));
}

static void test_readout_text_shows_string_and_flags_when_invalid(void) {
  lv_obj_t *r = ui_readout_create(scr, "MODE", "", 0);
  ui_readout_set_text(r, "BULK", true);
  TEST_ASSERT_FALSE(ui_field_faulted(r));
  TEST_ASSERT_EQUAL_STRING("BULK", ui_readout_text(r));
  ui_readout_set_text(r, "", false);
  TEST_ASSERT_TRUE(ui_field_faulted(r));
  TEST_ASSERT_EQUAL_STRING("MODE FAIL", ui_field_fault_text(r));
}

static void test_readout_invalid_has_no_digits(void) {
  lv_obj_t *r = ui_readout_create(scr, "HOURS", "h", 1);
  ui_readout_set(r, 0, false);
  render();
  TEST_ASSERT_TRUE(ui_field_faulted(r));
  TEST_ASSERT_EQUAL_STRING("", ui_readout_text(r));   // no stale or zero digits under the cross
}

static void test_valid_after_invalid_clears_fault(void) {
  lv_obj_t *r = ui_readout_create(scr, "HOURS", "h", 1);
  ui_readout_set(r, 0, false);
  ui_readout_set(r, 1225.4, true);
  render();
  TEST_ASSERT_FALSE(ui_field_faulted(r));
  TEST_ASSERT_EQUAL_STRING("1225.4", ui_readout_text(r));
}

static void test_fault_label_text(void) {
  lv_obj_t *r = ui_readout_create(scr, "COOLANT", "°C", 0);
  render();
  TEST_ASSERT_EQUAL_STRING("COOLANT FAIL", ui_field_fault_text(r));
}

static void test_fault_text_override(void) {   // the Power Summary's DC LOAD flags as LOAD FAIL
  lv_obj_t *r = ui_readout_create(scr, "DC LOAD", "W", 0);
  ui_field_set_fault_text(r, "LOAD FAIL");
  ui_readout_set(r, 0, false);
  render();
  TEST_ASSERT_TRUE(ui_field_faulted(r));
  TEST_ASSERT_EQUAL_STRING("LOAD FAIL", ui_field_fault_text(r));
}

static void test_dial_off_is_not_a_fault(void) {
  lv_obj_t *d = ui_dial_create(scr, &rpm, 1, NULL);
  ui_dial_set_off(d, 0);
  render();
  TEST_ASSERT_FALSE(ui_dial_needle_visible(d, 0));
  TEST_ASSERT_FALSE(ui_dial_needle_faulted(d, 0));
  TEST_ASSERT_FALSE(ui_field_faulted(d));
  TEST_ASSERT_EQUAL_STRING("OFF", ui_dial_text(d, 0));
}

static void test_dial_one_off_one_running(void) {
  const char *labels[] = {"P", "S"};
  lv_obj_t *d = ui_dial_create(scr, &rpm, 2, labels);
  ui_dial_set_off(d, 0);
  ui_dial_set(d, 1, 1500, true);
  render();
  TEST_ASSERT_FALSE(ui_field_faulted(d));
  TEST_ASSERT_TRUE(ui_dial_needle_visible(d, 1));
  TEST_ASSERT_EQUAL_STRING("OFF", ui_dial_text(d, 0));
}

static void test_strip_off_is_not_a_fault(void) {
  ui_gauge_spec_t oil = {.label = "OIL", .unit = "bar", .lo = 0, .hi = 6, .decimals = 1};
  lv_obj_t *s = ui_strip_create(scr, &oil, false);
  ui_strip_set_off(s);
  render();
  TEST_ASSERT_FALSE(ui_field_faulted(s));
  TEST_ASSERT_EQUAL_STRING("OFF", ui_strip_text(s));
}

static void test_unchanged_value_does_not_redraw(void) {
  ui_gauge_spec_t oil = {.label = "OIL", .unit = "bar", .lo = 0, .hi = 6, .decimals = 1};
  lv_obj_t *s = ui_strip_create(scr, &oil, false);
  lv_obj_t *d = ui_dial_create(scr, &rpm, 1, NULL);
  lv_obj_t *r = ui_readout_create(scr, "HOURS", "h", 1);
  ui_strip_set(s, 3.0, true);
  ui_dial_set(d, 0, 1800, true);
  ui_readout_set(r, 12.5, true);
  render();
  flushes = 0;
  ui_strip_set(s, 3.0, true);      // the 10 Hz refresh, nothing changed
  ui_dial_set(d, 0, 1800, true);
  ui_readout_set(r, 12.5, true);
  render();
  TEST_ASSERT_EQUAL_INT(0, flushes);
  ui_strip_set(s, 3.4, true);      // a real change still draws
  render();
  TEST_ASSERT_GREATER_THAN_INT(0, flushes);
}

int main(void) {
  lv_init();
  lv_display_t *disp = lv_display_create(480, 480);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  scr = lv_screen_active();
  UNITY_BEGIN();
  RUN_TEST(test_dial_starts_faulted);
  RUN_TEST(test_dial_valid_shows_needle);
  RUN_TEST(test_dial_invalid_hides_needle_and_shows_fault);
  RUN_TEST(test_dial_one_of_two_failed_flags_that_needle_only);
  RUN_TEST(test_dial_out_of_range_clamps_needle_readout_true);
  RUN_TEST(test_strip_invalid_then_valid);
  RUN_TEST(test_readout_invalid_has_no_digits);
  RUN_TEST(test_valid_after_invalid_clears_fault);
  RUN_TEST(test_fault_label_text);
  RUN_TEST(test_dial_off_is_not_a_fault);
  RUN_TEST(test_dial_one_off_one_running);
  RUN_TEST(test_strip_off_is_not_a_fault);
  RUN_TEST(test_readout_text_shows_string_and_flags_when_invalid);
  RUN_TEST(test_fault_text_override);
  RUN_TEST(test_unchanged_value_does_not_redraw);
  return UNITY_END();
}
