#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "boatdata.h"
#include "sk_parse.h"

void setUp(void) {
  bd_init();
  sk_parse_reset();
}
void tearDown(void) {}

static void feed(const char *json) { sk_parse_message(json, strlen(json), 1000); }

static bd_sample_t get(const char *path) {
  bd_sample_t s = {0};
  bd_get(bd_find(path), &s);
  return s;
}

static void test_delta_number(void) {
  feed("{\"context\":\"vessels.self\",\"updates\":[{\"values\":[{\"path\":\"propulsion.port.revolutions\",\"value\":30}]}]}");
  bd_sample_t s = get("propulsion.port.revolutions");
  TEST_ASSERT_TRUE(s.valid);
  TEST_ASSERT_EQUAL_DOUBLE(30, s.num);
  TEST_ASSERT_EQUAL_UINT8(SK_SOURCE_ID, s.source);
}

static void test_delta_without_context_is_self(void) {
  feed("{\"updates\":[{\"values\":[{\"path\":\"a.b\",\"value\":1.5}]}]}");
  TEST_ASSERT_TRUE(get("a.b").valid);
}

static void test_hello_sets_self_context(void) {
  feed("{\"name\":\"signalk-server\",\"version\":\"2.35.1\",\"self\":\"vessels.urn:mrn:signalk:uuid:abc\",\"roles\":[\"master\"]}");
  feed("{\"context\":\"vessels.urn:mrn:signalk:uuid:abc\",\"updates\":[{\"values\":[{\"path\":\"a.b\",\"value\":2}]}]}");
  TEST_ASSERT_TRUE(get("a.b").valid);
}

static void test_delta_other_context_ignored(void) {
  feed("{\"name\":\"s\",\"self\":\"vessels.urn:mrn:signalk:uuid:abc\"}");
  feed("{\"context\":\"vessels.urn:mrn:imo:mmsi:316000000\",\"updates\":[{\"values\":[{\"path\":\"a.b\",\"value\":2}]}]}");
  TEST_ASSERT_EQUAL(BD_NONE, bd_find("a.b"));
}

static void test_delta_null_invalidates(void) {
  feed("{\"updates\":[{\"values\":[{\"path\":\"propulsion.port.temperature\",\"value\":350}]}]}");
  feed("{\"updates\":[{\"values\":[{\"path\":\"propulsion.port.temperature\",\"value\":null}]}]}");
  TEST_ASSERT_FALSE(get("propulsion.port.temperature").valid);
}

static void test_delta_object_value_invalidates(void) {
  feed("{\"updates\":[{\"values\":[{\"path\":\"x\",\"value\":5}]}]}");
  feed("{\"updates\":[{\"values\":[{\"path\":\"x\",\"value\":{\"weird\":1}}]}]}");
  TEST_ASSERT_FALSE(get("x").valid);
}

static void test_delta_position(void) {
  feed("{\"updates\":[{\"values\":[{\"path\":\"navigation.position\",\"value\":{\"latitude\":49.25,\"longitude\":-123.5}}]}]}");
  bd_sample_t s = get("navigation.position");
  TEST_ASSERT_TRUE(s.valid);
  TEST_ASSERT_EQUAL(BD_POSITION, s.kind);
  TEST_ASSERT_EQUAL_DOUBLE(-123.5, s.pos.lon);
}

static void test_delta_text(void) {
  feed("{\"updates\":[{\"values\":[{\"path\":\"propulsion.port.state\",\"value\":\"started\"}]}]}");
  bd_sample_t s = get("propulsion.port.state");
  TEST_ASSERT_EQUAL(BD_TEXT, s.kind);
  TEST_ASSERT_EQUAL_STRING("started", s.text);
}

static void test_delta_meta(void) {
  feed("{\"updates\":[{\"meta\":[{\"path\":\"propulsion.port.temperature\",\"value\":{\"units\":\"K\","
       "\"displayScale\":{\"lower\":273.15,\"upper\":393.15},"
       "\"zones\":[{\"lower\":368.15,\"upper\":393.15,\"state\":\"alarm\"}]}}]}]}");
  bd_meta_t m;
  TEST_ASSERT_TRUE(bd_get_meta(bd_find("propulsion.port.temperature"), &m));
  TEST_ASSERT_TRUE(m.has_scale);
  TEST_ASSERT_EQUAL_DOUBLE(393.15, m.scale_hi);
  TEST_ASSERT_EQUAL_UINT8(1, m.zone_count);
  TEST_ASSERT_EQUAL_UINT8(SK_STATE_ALARM, m.zones[0].state);
}

static void test_garbage_is_ignored(void) {
  feed("not json at all");
  feed("{\"updates\":\"nope\"}");
  feed("{\"updates\":[{\"values\":[{\"value\":3}]}]}");   // no path
  TEST_PASS();
}

static const char *FULL =
  "{\"name\":\"Golden Dragon\",\"uuid\":\"urn:mrn:signalk:uuid:abc\","
  "\"propulsion\":{"
  "  \"port\":{\"revolutions\":{\"value\":30,\"$source\":\"n2k.1\",\"timestamp\":\"t\"},"
  "          \"temperature\":{\"value\":350,\"meta\":{\"units\":\"K\",\"zones\":[{\"lower\":368,\"upper\":400,\"state\":\"warn\"}]}}},"
  "  \"starboard\":{\"revolutions\":{\"value\":31}}},"
  "\"navigation\":{\"position\":{\"value\":{\"latitude\":49,\"longitude\":-123}}}}";

static void test_full_model_interns_leaves(void) {
  sk_parse_full(FULL, strlen(FULL));
  bd_id_t ids[8];
  TEST_ASSERT_EQUAL(2, bd_match("propulsion.*.revolutions", ids, 8));
  TEST_ASSERT_NOT_EQUAL(BD_NONE, bd_find("navigation.position"));
  TEST_ASSERT_EQUAL(BD_NONE, bd_find("navigation.position.latitude"));   // leaf, not inside it
  TEST_ASSERT_EQUAL(BD_NONE, bd_find("name"));                           // vessel identity, not data
  TEST_ASSERT_FALSE(get("propulsion.port.revolutions").valid);           // values come from the stream
}

static void test_full_model_reads_meta_zones(void) {
  sk_parse_full(FULL, strlen(FULL));
  bd_meta_t m;
  TEST_ASSERT_TRUE(bd_get_meta(bd_find("propulsion.port.temperature"), &m));
  TEST_ASSERT_EQUAL_STRING("K", m.units);
  TEST_ASSERT_EQUAL_UINT8(SK_STATE_WARN, m.zones[0].state);
}

static void test_full_model_sets_self(void) {
  sk_parse_full(FULL, strlen(FULL));
  feed("{\"context\":\"vessels.urn:mrn:signalk:uuid:abc\",\"updates\":[{\"values\":[{\"path\":\"q\",\"value\":1}]}]}");
  TEST_ASSERT_TRUE(get("q").valid);
}

static void test_full_model_interns_only_discovery_prefixes(void) {
  const char *prefixes[] = {"propulsion", NULL};
  sk_parse_set_discovery_prefixes(prefixes);
  const char *body = "{\"electrical\":{\"batteries\":{\"house\":{\"voltage\":{\"value\":12.8}}}},"
                     "\"propulsion\":{\"port\":{\"revolutions\":{\"value\":30}}}}";
  sk_parse_full(body, strlen(body));
  TEST_ASSERT_EQUAL(BD_NONE, bd_find("electrical.batteries.house.voltage"));
  TEST_ASSERT_NOT_EQUAL(BD_NONE, bd_find("propulsion.port.revolutions"));
  sk_parse_set_discovery_prefixes(NULL);   // back to everything
}

static void test_full_model_prefix_is_a_whole_segment(void) {
  const char *prefixes[] = {"propulsion", NULL};
  sk_parse_set_discovery_prefixes(prefixes);
  const char *body = "{\"propulsionExtra\":{\"x\":{\"value\":1}}}";
  sk_parse_full(body, strlen(body));
  TEST_ASSERT_EQUAL(BD_NONE, bd_find("propulsionExtra.x"));
  sk_parse_set_discovery_prefixes(NULL);
}

static void test_subscribe_json_shape(void) {
  bd_id_t ids[2] = {bd_intern("propulsion.port.revolutions"), bd_intern("tanks.fuel.0.currentLevel")};
  char *s = sk_build_subscribe(ids, 2);
  TEST_ASSERT_NOT_NULL(strstr(s, "\"context\":\"vessels.self\""));
  TEST_ASSERT_NOT_NULL(strstr(s, "{\"path\":\"propulsion.port.revolutions\",\"policy\":\"instant\"}"));
  TEST_ASSERT_NOT_NULL(strstr(s, "{\"path\":\"tanks.fuel.0.currentLevel\",\"policy\":\"instant\"}"));
  free(s);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_delta_number);
  RUN_TEST(test_delta_without_context_is_self);
  RUN_TEST(test_hello_sets_self_context);
  RUN_TEST(test_delta_other_context_ignored);
  RUN_TEST(test_delta_null_invalidates);
  RUN_TEST(test_delta_object_value_invalidates);
  RUN_TEST(test_delta_position);
  RUN_TEST(test_delta_text);
  RUN_TEST(test_delta_meta);
  RUN_TEST(test_garbage_is_ignored);
  RUN_TEST(test_full_model_interns_leaves);
  RUN_TEST(test_full_model_reads_meta_zones);
  RUN_TEST(test_full_model_sets_self);
  RUN_TEST(test_full_model_interns_only_discovery_prefixes);
  RUN_TEST(test_full_model_prefix_is_a_whole_segment);
  RUN_TEST(test_subscribe_json_shape);
  return UNITY_END();
}
