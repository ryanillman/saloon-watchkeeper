// Drives a simulated pointer drag through LVGL and checks the shell changes role.
#include "unity.h"
#include "ui_shell.h"

static uint32_t now_ms;
static uint32_t fake_tick(void) { return now_ms; }

static lv_point_t ptr;
static bool pressed;
static void read_ptr(lv_indev_t *i, lv_indev_data_t *d) {
  (void)i;
  d->point = ptr;
  d->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  lv_display_flush_ready(d);
}

static void page(lv_obj_t *area, uint8_t p, size_class_t sc, void *ctx) {
  (void)p; (void)sc; (void)ctx;
  lv_obj_t *l = lv_label_create(area);   // a child under the finger, like a real page
  lv_label_set_text(l, "PAGE");
  lv_obj_set_size(l, LV_PCT(100), LV_PCT(100));
}

// A container whose child overflows it, like a gauge whose fault cross spills over.
static void overflow_page(lv_obj_t *area, uint8_t p, size_class_t sc, void *ctx) {
  (void)p; (void)sc; (void)ctx;
  lv_obj_t *box = lv_obj_create(area);
  lv_obj_set_size(box, 700, 400);
  lv_obj_t *big = lv_obj_create(box);
  lv_obj_set_size(big, 900, 500);
}

static ui_role_view_t overflow_roles[] = {{"A", 2, overflow_page, NULL, NULL}, {"B", 1, overflow_page, NULL, NULL}};

static ui_role_view_t roles[] = {{"A", 2, page, NULL, NULL}, {"B", 1, page, NULL, NULL}, {"C", 1, page, NULL, NULL}};
static lv_indev_t *indev;
static uint8_t buf[800 * 48 * 2];

void setUp(void) {}
void tearDown(void) {}

static void step(int32_t x, int32_t y, bool down) {
  ptr.x = x; ptr.y = y; pressed = down;
  now_ms += 20;
  lv_indev_read(indev);
  lv_timer_handler();
}

static void drag(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
  for (int i = 0; i <= 10; i++) step(x0 + (x1 - x0) * i / 10, y0 + (y1 - y0) * i / 10, true);
  step(x1, y1, false);
  for (int i = 0; i < 3; i++) step(x1, y1, false);   // let the deferred move run
}

static void test_drag_left_changes_role(void) {
  ui_shell_create(lv_display_get_default(), SIZE_WIDE, PALETTE_DAY, roles, 3, 0);
  drag(600, 240, 200, 240);
  TEST_ASSERT_EQUAL_UINT8(1, ui_shell_nav()->role);
}

static void test_drag_up_changes_page(void) {
  ui_shell_create(lv_display_get_default(), SIZE_WIDE, PALETTE_DAY, roles, 3, 0);
  drag(400, 420, 400, 120);
  TEST_ASSERT_EQUAL_UINT8(0, ui_shell_nav()->role);
  TEST_ASSERT_EQUAL_UINT8(1, ui_shell_nav()->page);
}

static void test_drag_over_overflowing_child_changes_role(void) {
  ui_shell_create(lv_display_get_default(), SIZE_WIDE, PALETTE_DAY, overflow_roles, 2, 0);
  drag(600, 240, 200, 240);
  TEST_ASSERT_EQUAL_UINT8(1, ui_shell_nav()->role);
}

int main(void) {
  lv_init();
  lv_tick_set_cb(fake_tick);
  lv_display_t *d = lv_display_create(800, 480);
  lv_display_set_flush_cb(d, flush);
  lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, read_ptr);
  lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);   // read only when step() asks

  UNITY_BEGIN();
  RUN_TEST(test_drag_left_changes_role);
  RUN_TEST(test_drag_up_changes_page);
  RUN_TEST(test_drag_over_overflowing_child_changes_role);
  return UNITY_END();
}
