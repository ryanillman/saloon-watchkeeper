#include "unity.h"
#include "ui_nav.h"

void setUp(void) {}
void tearDown(void) {}

// ENGINE 2 pages, POWER 2, TANKS 1, ANCHOR 1
static const uint8_t pages[] = {2, 2, 1, 1};
static ui_nav_t n;

static void start(uint8_t role) { ui_nav_init(&n, 4, pages, role); }

static void test_left_advances_role(void) {
  start(0);
  TEST_ASSERT_TRUE(ui_nav_move(&n, NAV_LEFT));
  TEST_ASSERT_EQUAL_UINT8(1, n.role);
}

static void test_right_from_first_wraps_to_last(void) {
  start(0);
  TEST_ASSERT_TRUE(ui_nav_move(&n, NAV_RIGHT));
  TEST_ASSERT_EQUAL_UINT8(3, n.role);
}

static void test_left_from_last_wraps_to_first(void) {
  start(3);
  TEST_ASSERT_TRUE(ui_nav_move(&n, NAV_LEFT));
  TEST_ASSERT_EQUAL_UINT8(0, n.role);
}

static void test_up_advances_page_and_wraps(void) {
  start(0);
  TEST_ASSERT_TRUE(ui_nav_move(&n, NAV_UP));
  TEST_ASSERT_EQUAL_UINT8(1, n.page);
  TEST_ASSERT_TRUE(ui_nav_move(&n, NAV_UP));
  TEST_ASSERT_EQUAL_UINT8(0, n.page);
  TEST_ASSERT_TRUE(ui_nav_move(&n, NAV_DOWN));
  TEST_ASSERT_EQUAL_UINT8(1, n.page);
}

static void test_role_change_resets_page(void) {
  start(0);
  ui_nav_move(&n, NAV_UP);
  ui_nav_move(&n, NAV_LEFT);
  TEST_ASSERT_EQUAL_UINT8(1, n.role);
  TEST_ASSERT_EQUAL_UINT8(0, n.page);
}

static void test_single_page_ignores_vertical(void) {
  start(2);
  TEST_ASSERT_FALSE(ui_nav_move(&n, NAV_UP));
  TEST_ASSERT_FALSE(ui_nav_move(&n, NAV_DOWN));
  TEST_ASSERT_EQUAL_UINT8(0, n.page);
}

static void test_zero_roles_noop(void) {
  ui_nav_init(&n, 0, NULL, 0);
  TEST_ASSERT_FALSE(ui_nav_move(&n, NAV_LEFT));
  TEST_ASSERT_FALSE(ui_nav_move(&n, NAV_RIGHT));
  TEST_ASSERT_FALSE(ui_nav_move(&n, NAV_UP));
  TEST_ASSERT_FALSE(ui_nav_move(&n, NAV_DOWN));
}

static void test_start_role_out_of_range_is_zero(void) {
  start(9);
  TEST_ASSERT_EQUAL_UINT8(0, n.role);
}

static void test_set_pages_clamps_page(void) {
  start(0);   // pages: see top of file
  n.page = 1;
  uint8_t fewer[4] = {1, 1, 1, 1};
  ui_nav_set_pages(&n, fewer);
  TEST_ASSERT_EQUAL_UINT8(0, n.page);
  uint8_t more[4] = {3, 1, 1, 1};
  ui_nav_set_pages(&n, more);
  TEST_ASSERT_EQUAL_UINT8(3, n.page_count[0]);
  TEST_ASSERT_TRUE(ui_nav_move(&n, NAV_UP));
  TEST_ASSERT_EQUAL_UINT8(1, n.page);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_set_pages_clamps_page);
  RUN_TEST(test_left_advances_role);
  RUN_TEST(test_right_from_first_wraps_to_last);
  RUN_TEST(test_left_from_last_wraps_to_first);
  RUN_TEST(test_up_advances_page_and_wraps);
  RUN_TEST(test_role_change_resets_page);
  RUN_TEST(test_single_page_ignores_vertical);
  RUN_TEST(test_zero_roles_noop);
  RUN_TEST(test_start_role_out_of_range_is_zero);
  return UNITY_END();
}
