#include "unity.h"
#include "sim_boards.h"

void setUp(void) {}
void tearDown(void) {}

static void check(const char *id, uint16_t w, uint16_t h, size_class_t sc) {
  const sim_board_t *b = sim_board_find(id);
  TEST_ASSERT_NOT_NULL_MESSAGE(b, id);
  TEST_ASSERT_EQUAL_UINT16(w, b->width);
  TEST_ASSERT_EQUAL_UINT16(h, b->height);
  TEST_ASSERT_EQUAL_INT(sc, b->size_class);
}

static void test_sim480(void) { check("sim480", 480, 480, SIZE_SQUARE); }
static void test_sim800(void) { check("sim800", 800, 480, SIZE_WIDE); }
static void test_sim1024(void) { check("sim1024", 1024, 600, SIZE_WIDE); }
static void test_unknown_is_null(void) { TEST_ASSERT_NULL(sim_board_find("nope")); }

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_sim480);
  RUN_TEST(test_sim800);
  RUN_TEST(test_sim1024);
  RUN_TEST(test_unknown_is_null);
  return UNITY_END();
}
