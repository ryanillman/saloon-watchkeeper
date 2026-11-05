#include "unity.h"
#include "ui_theme.h"

#define C(hex) lv_color_hex(hex)

void setUp(void) {}
void tearDown(void) {}

static void test_nav_tokens_per_palette(void) {
  const ui_colors_t *d = ui_theme_colors(PALETTE_DAY), *u = ui_theme_colors(PALETTE_DUSK), *n = ui_theme_colors(PALETTE_NIGHT);
  TEST_ASSERT_TRUE(lv_color_eq(d->nav_course, C(0xFF2BD6)));
  TEST_ASSERT_TRUE(lv_color_eq(d->nav_app, C(0xFFE62E)));
  TEST_ASSERT_TRUE(lv_color_eq(d->nav_true, C(0xFF7A1A)));
  TEST_ASSERT_TRUE(lv_color_eq(d->nav_lubber, d->warn));
  TEST_ASSERT_TRUE(lv_color_eq(u->nav_course, C(0xB01E96)));
  TEST_ASSERT_TRUE(lv_color_eq(u->nav_app, C(0xB3A11F)));
  TEST_ASSERT_TRUE(lv_color_eq(u->nav_true, C(0xB3540F)));
  TEST_ASSERT_TRUE(lv_color_eq(u->nav_lubber, u->warn));
  TEST_ASSERT_TRUE(lv_color_eq(n->nav_course, C(0xC01818)));
  TEST_ASSERT_TRUE(lv_color_eq(n->nav_app, C(0xC01818)));
  TEST_ASSERT_TRUE(lv_color_eq(n->nav_true, C(0xC01818)));
  TEST_ASSERT_TRUE(lv_color_eq(n->nav_lubber, C(0xC01818)));
}

static void test_night_nav_tokens_are_one_colour(void) {
  const ui_colors_t *n = ui_theme_colors(PALETTE_NIGHT);
  TEST_ASSERT_TRUE(lv_color_eq(n->nav_course, n->nav_app));
  TEST_ASSERT_TRUE(lv_color_eq(n->nav_app, n->nav_true));
  TEST_ASSERT_TRUE(lv_color_eq(n->nav_true, n->nav_lubber));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_nav_tokens_per_palette);
  RUN_TEST(test_night_nav_tokens_are_one_colour);
  return UNITY_END();
}
