// The hold-to-confirm button: acts only after being held for UI_HOLD_MS.
#include "ui_holdbtn.h"
#include "ui_theme.h"
#include "unity.h"

static uint32_t now_ms;
static uint32_t fake_tick(void) { return now_ms; }
static uint8_t buf[480 * 48 * 2];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  lv_display_flush_ready(d);
}
static lv_obj_t *scr;

static int fired[3];
static void held0(void *ctx) { (void)ctx; fired[0]++; }
static void held1(void *ctx) { (void)ctx; fired[1]++; }
static void held_ctx(void *ctx) { fired[2] += *(int *)ctx; }

void setUp(void) {
  lv_obj_clean(scr);
  fired[0] = fired[1] = fired[2] = 0;
  ui_theme_set_current(PALETTE_DAY);
}
void tearDown(void) {}

static void run(uint32_t ms) {   // time passes in 50 ms steps so the hold timer runs repeatedly
  for (uint32_t t = 0; t < ms; t += 50) {
    now_ms += 50;
    lv_timer_handler();
  }
}
static void press(lv_obj_t *b) { lv_obj_send_event(b, LV_EVENT_PRESSED, NULL); }
static void release(lv_obj_t *b) { lv_obj_send_event(b, LV_EVENT_RELEASED, NULL); }
static lv_obj_t *make(ui_holdbtn_fn fn, void *ctx) { return ui_holdbtn_create(scr, "MOB", &lv_font_montserrat_20, 120, 64, fn, ctx); }

static void test_fires_after_the_hold_and_only_once(void) {
  lv_obj_t *b = make(held0, NULL);
  press(b);
  run(900);
  TEST_ASSERT_EQUAL(0, fired[0]);
  run(150);
  TEST_ASSERT_EQUAL(1, fired[0]);
  run(3000);   // still held
  TEST_ASSERT_EQUAL(1, fired[0]);
  release(b);
  run(500);
  TEST_ASSERT_EQUAL(1, fired[0]);
}

static void test_early_release_and_a_lost_press_send_nothing(void) {
  lv_obj_t *b = make(held0, NULL);
  press(b);
  run(500);
  release(b);
  run(2000);
  press(b);
  run(500);
  lv_obj_send_event(b, LV_EVENT_PRESS_LOST, NULL);   // the finger slid off
  run(2000);
  TEST_ASSERT_EQUAL(0, fired[0]);
}

static void test_a_tap_is_not_a_hold(void) {
  lv_obj_t *b = make(held0, NULL);
  press(b);
  run(100);
  release(b);
  lv_obj_send_event(b, LV_EVENT_CLICKED, NULL);   // the click a tap produces
  run(2000);
  TEST_ASSERT_EQUAL(0, fired[0]);
}

static void test_the_context_is_passed_and_buttons_are_independent(void) {
  int three = 3;
  lv_obj_t *a = make(held1, NULL), *b = make(held_ctx, &three);
  press(b);
  run(1100);
  release(b);
  TEST_ASSERT_EQUAL(3, fired[2]);
  TEST_ASSERT_EQUAL(0, fired[1]);
  press(a);
  run(1100);
  release(a);
  TEST_ASSERT_EQUAL(1, fired[1]);
  TEST_ASSERT_EQUAL(3, fired[2]);
}

static void test_the_fill_grows_while_held_and_resets(void) {
  lv_obj_t *b = make(held0, NULL);
  lv_obj_update_layout(scr);
  int32_t full = lv_obj_get_width(b);
  TEST_ASSERT_EQUAL(0, lv_obj_get_style_width(ui_holdbtn_fill(b), LV_PART_MAIN));
  press(b);
  run(500);
  int32_t half = lv_obj_get_style_width(ui_holdbtn_fill(b), LV_PART_MAIN);   // the requested width: layout catches up next frame
  TEST_ASSERT_TRUE_MESSAGE(half > full / 4 && half < full * 3 / 4, "about half filled at half the hold");
  release(b);
  TEST_ASSERT_EQUAL(0, lv_obj_get_style_width(ui_holdbtn_fill(b), LV_PART_MAIN));
}

static void test_deleting_a_button_during_a_hold_is_safe(void) {
  lv_obj_t *b = make(held0, NULL);
  press(b);
  run(300);
  lv_obj_delete(b);   // the panel closed mid-hold
  run(2000);
  TEST_ASSERT_EQUAL(0, fired[0]);
}

// Night is one red: the filled (declared) button's fill and the usual text colour are the same, so the label must flip.
static void test_declared_label_is_readable_in_every_palette(void) {
  for (int pal = PALETTE_DAY; pal <= PALETTE_NIGHT; pal++) {
    ui_theme_set_current((palette_t)pal);
    lv_obj_t *b = make(held0, NULL);
    for (int declared = 0; declared <= 1; declared++) {
      ui_holdbtn_set_declared(b, declared);
      lv_color_t fill = lv_obj_get_style_bg_color(b, LV_PART_MAIN), text = lv_obj_get_style_text_color(ui_holdbtn_label(b), LV_PART_MAIN);
      TEST_ASSERT_FALSE_MESSAGE(lv_color_eq(fill, text), "label differs from fill");
    }
    lv_obj_delete(b);
  }
}

int main(void) {
  lv_init();
  lv_tick_set_cb(fake_tick);
  lv_display_t *d = lv_display_create(480, 480);
  lv_display_set_flush_cb(d, flush);
  lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  scr = lv_screen_active();
  UNITY_BEGIN();
  RUN_TEST(test_fires_after_the_hold_and_only_once);
  RUN_TEST(test_early_release_and_a_lost_press_send_nothing);
  RUN_TEST(test_a_tap_is_not_a_hold);
  RUN_TEST(test_the_context_is_passed_and_buttons_are_independent);
  RUN_TEST(test_the_fill_grows_while_held_and_resets);
  RUN_TEST(test_deleting_a_button_during_a_hold_is_safe);
  RUN_TEST(test_declared_label_is_readable_in_every_palette);
  return UNITY_END();
}
