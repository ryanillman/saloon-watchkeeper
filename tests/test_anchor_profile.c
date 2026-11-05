#include <string.h>
#include "unity.h"
#include "anchor_profile.h"

void setUp(void) {}
void tearDown(void) {}

static anchor_cmd_t build(anchor_plugin_t p, anchor_cmd_kind_t k, double r) {
  anchor_cmd_t c;
  anchor_build_cmd(p, k, r, &c);
  return c;
}

static void test_hoeken_drop(void) {
  anchor_cmd_t c = build(ANCHOR_PLUGIN_HOEKEN, ANCHOR_CMD_DROP, 30);
  TEST_ASSERT_FALSE(c.put_number);
  TEST_ASSERT_EQUAL_STRING("/plugins/hoekens-anchor-alarm/dropAnchor", c.path);
  TEST_ASSERT_EQUAL_STRING("{\"zone\":{\"type\":\"circle\",\"radius\":30}}", c.body);
}

static void test_hoeken_radius(void) {
  anchor_cmd_t c = build(ANCHOR_PLUGIN_HOEKEN, ANCHOR_CMD_RADIUS, 45);
  TEST_ASSERT_FALSE(c.put_number);
  TEST_ASSERT_EQUAL_STRING("/plugins/hoekens-anchor-alarm/setZone", c.path);
  TEST_ASSERT_EQUAL_STRING("{\"zone\":{\"type\":\"circle\",\"radius\":45}}", c.body);
}

static void test_hoeken_raise(void) {
  anchor_cmd_t c = build(ANCHOR_PLUGIN_HOEKEN, ANCHOR_CMD_RAISE, 30);
  TEST_ASSERT_FALSE(c.put_number);
  TEST_ASSERT_EQUAL_STRING("/plugins/hoekens-anchor-alarm/raiseAnchor", c.path);
  TEST_ASSERT_EQUAL_STRING("", c.body);
}

static void test_anchoralarm_drop(void) {
  anchor_cmd_t c = build(ANCHOR_PLUGIN_ANCHORALARM, ANCHOR_CMD_DROP, 30);
  TEST_ASSERT_FALSE(c.put_number);
  TEST_ASSERT_EQUAL_STRING("/plugins/anchoralarm/dropAnchor", c.path);
  TEST_ASSERT_EQUAL_STRING("{\"radius\":30}", c.body);
}

static void test_anchoralarm_radius_is_put(void) {
  anchor_cmd_t c = build(ANCHOR_PLUGIN_ANCHORALARM, ANCHOR_CMD_RADIUS, 30);
  TEST_ASSERT_TRUE(c.put_number);
  TEST_ASSERT_EQUAL_STRING("navigation.anchor.maxRadius", c.path);
  TEST_ASSERT_EQUAL_DOUBLE(30, c.value);
}

static void test_anchoralarm_raise(void) {
  anchor_cmd_t c = build(ANCHOR_PLUGIN_ANCHORALARM, ANCHOR_CMD_RAISE, 30);
  TEST_ASSERT_FALSE(c.put_number);
  TEST_ASSERT_EQUAL_STRING("/plugins/anchoralarm/raiseAnchor", c.path);
  TEST_ASSERT_EQUAL_STRING("", c.body);
}

static void test_radius_prints_without_noise(void) {
  TEST_ASSERT_EQUAL_STRING("{\"radius\":32.5}", build(ANCHOR_PLUGIN_ANCHORALARM, ANCHOR_CMD_DROP, 32.5).body);
  TEST_ASSERT_EQUAL_STRING("{\"radius\":5}", build(ANCHOR_PLUGIN_ANCHORALARM, ANCHOR_CMD_DROP, 5).body);
  TEST_ASSERT_EQUAL_STRING("{\"zone\":{\"type\":\"circle\",\"radius\":300}}", build(ANCHOR_PLUGIN_HOEKEN, ANCHOR_CMD_DROP, 300).body);
}

static void test_plugin_names(void) {
  anchor_plugin_t p;
  TEST_ASSERT_TRUE(anchor_plugin_from_name("hoekens-anchor-alarm", &p));
  TEST_ASSERT_EQUAL(ANCHOR_PLUGIN_HOEKEN, p);
  TEST_ASSERT_TRUE(anchor_plugin_from_name("anchoralarm", &p));
  TEST_ASSERT_EQUAL(ANCHOR_PLUGIN_ANCHORALARM, p);
}

static void test_unknown_name_rejected(void) {
  anchor_plugin_t p = ANCHOR_PLUGIN_HOEKEN;
  TEST_ASSERT_FALSE(anchor_plugin_from_name("nope", &p));
  TEST_ASSERT_FALSE(anchor_plugin_from_name("", &p));
  TEST_ASSERT_FALSE(anchor_plugin_from_name(NULL, &p));
  TEST_ASSERT_EQUAL(ANCHOR_PLUGIN_HOEKEN, p);   // untouched
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_hoeken_drop);
  RUN_TEST(test_hoeken_radius);
  RUN_TEST(test_hoeken_raise);
  RUN_TEST(test_anchoralarm_drop);
  RUN_TEST(test_anchoralarm_radius_is_put);
  RUN_TEST(test_anchoralarm_raise);
  RUN_TEST(test_radius_prints_without_noise);
  RUN_TEST(test_plugin_names);
  RUN_TEST(test_unknown_name_rejected);
  return UNITY_END();
}
