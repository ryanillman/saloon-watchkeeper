#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "alerts.h"
#include "boatdata.h"
#include "sk_parse.h"

void setUp(void) {
  bd_init();
  alerts_init();
  alerts_source_up(true);
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

static void test_discovery_multi_segment_prefix(void) {
  const char *prefixes[] = {"electrical.batteries", NULL};
  sk_parse_set_discovery_prefixes(prefixes);
  const char *body = "{\"electrical\":{\"batteries\":{\"279\":{\"voltage\":{\"value\":12.8}}},"
                     "\"solar\":{\"288\":{\"panelPower\":{\"value\":50}}}}}";
  sk_parse_full(body, strlen(body));
  TEST_ASSERT_NOT_EQUAL(BD_NONE, bd_find("electrical.batteries.279.voltage"));
  TEST_ASSERT_EQUAL(BD_NONE, bd_find("electrical.solar.288.panelPower"));
  sk_parse_set_discovery_prefixes(NULL);
}

static void test_discovery_prefix_segment_boundary(void) {
  const char *prefixes[] = {"electrical.solar", NULL};
  sk_parse_set_discovery_prefixes(prefixes);
  const char *body = "{\"electrical\":{\"solarX\":{\"1\":{\"voltage\":{\"value\":1}}}}}";
  sk_parse_full(body, strlen(body));
  TEST_ASSERT_EQUAL(BD_NONE, bd_find("electrical.solarX.1.voltage"));
  sk_parse_set_discovery_prefixes(NULL);
}

static void test_discovery_single_segment_still_works(void) {
  const char *prefixes[] = {"tanks", NULL};
  sk_parse_set_discovery_prefixes(prefixes);
  const char *body = "{\"tanks\":{\"freshWater\":{\"0\":{\"currentLevel\":{\"value\":0.5}}}}}";
  sk_parse_full(body, strlen(body));
  TEST_ASSERT_NOT_EQUAL(BD_NONE, bd_find("tanks.freshWater.0.currentLevel"));
  sk_parse_set_discovery_prefixes(NULL);
}

static void test_subscribe_json_shape(void) {
  sk_sub_t ids[2] = {{bd_intern("propulsion.port.revolutions"), 0}, {bd_intern("tanks.fuel.0.currentLevel"), 0}};
  char *s = sk_build_subscribe(ids, 2);
  TEST_ASSERT_NOT_NULL(strstr(s, "\"context\":\"vessels.self\""));
  TEST_ASSERT_NOT_NULL(strstr(s, "{\"path\":\"propulsion.port.revolutions\",\"policy\":\"instant\"}"));
  TEST_ASSERT_NOT_NULL(strstr(s, "{\"path\":\"tanks.fuel.0.currentLevel\",\"policy\":\"instant\"}"));
  free(s);
}

#define NOTE(state_and_more) "{\"updates\":[{\"values\":[{\"path\":\"notifications.propulsion.port.temperature\",\"value\":" state_and_more "}]}]}"

static void test_notification_raises_alert(void) {
  feed(NOTE("{\"state\":\"alarm\",\"method\":[\"visual\",\"sound\"],\"message\":\"Hot\",\"id\":\"u-1\",\"status\":{\"silenced\":false,\"acknowledged\":true}}"));
  alerts_snapshot_t s;
  alerts_snapshot(&s);
  TEST_ASSERT_EQUAL_UINT8(1, s.count);
  TEST_ASSERT_EQUAL_STRING("propulsion.port.temperature", s.a[0].id);
  TEST_ASSERT_EQUAL(AL_ALARM, s.a[0].state);
  TEST_ASSERT_TRUE(s.a[0].audible);
  TEST_ASSERT_TRUE(s.a[0].acked);
  TEST_ASSERT_FALSE(s.a[0].silenced);
  TEST_ASSERT_EQUAL_STRING("Hot", s.a[0].message);
  TEST_ASSERT_EQUAL_STRING("u-1", s.a[0].ref);
}

static void test_notification_null_clears(void) {
  feed(NOTE("{\"state\":\"alarm\",\"method\":[\"sound\"],\"message\":\"Hot\"}"));
  feed(NOTE("null"));
  alerts_snapshot_t s;
  alerts_snapshot(&s);
  TEST_ASSERT_EQUAL_UINT8(0, s.count);
}

static void test_notification_missing_state_clears(void) {
  feed(NOTE("{\"state\":\"warn\",\"message\":\"Hot\"}"));
  feed(NOTE("{\"message\":\"no state\"}"));
  alerts_snapshot_t s;
  alerts_snapshot(&s);
  TEST_ASSERT_EQUAL_UINT8(0, s.count);
}

static void test_notification_normal_clears(void) {
  feed(NOTE("{\"state\":\"warn\",\"message\":\"Hot\"}"));
  feed(NOTE("{\"state\":\"normal\",\"id\":\"u-1\"}"));
  alerts_snapshot_t s;
  alerts_snapshot(&s);
  TEST_ASSERT_EQUAL_UINT8(0, s.count);
}

static char em_id[64], em_ref[48];
static bool em_active, em_pos;
static double em_lat, em_lon;
static int em_calls;
static void on_em(const char *id, bool active, bool has_pos, double lat, double lon, const char *ref) {
  snprintf(em_id, sizeof em_id, "%s", id);
  snprintf(em_ref, sizeof em_ref, "%s", ref ? ref : "(null)");
  em_active = active;
  em_pos = has_pos;
  em_lat = lat;
  em_lon = lon;
  em_calls++;
}
#define MOB_NOTE(value) "{\"updates\":[{\"values\":[{\"path\":\"notifications.mob\",\"value\":" value "}]}]}"

static void test_parse_full_reads_name_and_mmsi(void) {
  const char *j = "{\"name\":\"Saloon\",\"mmsi\":\"316001234\",\"uuid\":\"urn:mrn:signalk:uuid:abc\"}";
  TEST_ASSERT_TRUE(sk_parse_full(j, strlen(j)));
  TEST_ASSERT_EQUAL_STRING("Saloon", sk_vessel_name());
  TEST_ASSERT_EQUAL_STRING("316001234", sk_vessel_mmsi());
  const char *none = "{\"navigation\":{}}";
  sk_parse_full(none, strlen(none));   // a snapshot without a name does not erase a known one
  TEST_ASSERT_EQUAL_STRING("Saloon", sk_vessel_name());
}

static void test_notification_calls_emergency_cb_with_position(void) {
  em_calls = 0;
  sk_set_emergency_cb(on_em);
  feed(MOB_NOTE("{\"state\":\"emergency\",\"message\":\"MAN OVERBOARD\",\"data\":{\"position\":{\"latitude\":41.5,\"longitude\":-70.25}}}"));
  TEST_ASSERT_EQUAL(1, em_calls);
  TEST_ASSERT_EQUAL_STRING("mob", em_id);
  TEST_ASSERT_TRUE(em_active);
  TEST_ASSERT_TRUE(em_pos);
  TEST_ASSERT_EQUAL_DOUBLE(41.5, em_lat);
  TEST_ASSERT_EQUAL_DOUBLE(-70.25, em_lon);
  alerts_snapshot_t s;   // the generic alert handling still happens
  alerts_snapshot(&s);
  TEST_ASSERT_EQUAL_UINT8(1, s.count);
  sk_set_emergency_cb(NULL);
}

#define SERVER_MOB(value) "{\"updates\":[{\"values\":[{\"path\":\"notifications.mob.7c0e98fc-2e6a-453c-a7d9-7cf715e2c47b\",\"value\":" value "}]}]}"

static void test_servers_own_mob_carries_its_uuid_in_the_path_and_value_position(void) {
  // What POST /signalk/v2/api/notifications/mob produces: notifications.mob.<uuid>, the server's position in value.position.
  em_calls = 0;
  sk_set_emergency_cb(on_em);
  feed(SERVER_MOB("{\"state\":\"emergency\",\"message\":\"Person Overboard!\",\"position\":{\"latitude\":41.5,\"longitude\":-70.25,\"altitude\":0},"
                  "\"createdAt\":\"2026-11-06T18:00:00Z\",\"id\":\"7c0e98fc-2e6a-453c-a7d9-7cf715e2c47b\"}"));
  TEST_ASSERT_EQUAL(1, em_calls);
  TEST_ASSERT_EQUAL_STRING("mob.7c0e98fc-2e6a-453c-a7d9-7cf715e2c47b", em_id);
  TEST_ASSERT_EQUAL_STRING("7c0e98fc-2e6a-453c-a7d9-7cf715e2c47b", em_ref);
  TEST_ASSERT_TRUE(em_active);
  TEST_ASSERT_TRUE(em_pos);
  TEST_ASSERT_EQUAL_DOUBLE(41.5, em_lat);
  TEST_ASSERT_EQUAL_DOUBLE(-70.25, em_lon);
  sk_set_emergency_cb(NULL);
}

static void test_data_position_wins_over_the_servers_value_position(void) {
  em_calls = 0;
  sk_set_emergency_cb(on_em);   // the press-time capture beats the position the server had when it handled the request
  feed(SERVER_MOB("{\"state\":\"emergency\",\"position\":{\"latitude\":1.0,\"longitude\":2.0},"
                  "\"data\":{\"position\":{\"latitude\":41.5,\"longitude\":-70.25}},\"id\":\"u\"}"));
  TEST_ASSERT_EQUAL_DOUBLE(41.5, em_lat);
  TEST_ASSERT_EQUAL_DOUBLE(-70.25, em_lon);
  feed(SERVER_MOB("{\"state\":\"emergency\",\"position\":null,\"id\":\"u\"}"));   // the server had no fix: null
  TEST_ASSERT_FALSE(em_pos);
  sk_set_emergency_cb(NULL);
}

static void test_notification_without_id_calls_cb_with_null_ref(void) {
  em_calls = 0;
  sk_set_emergency_cb(on_em);
  feed(MOB_NOTE("{\"state\":\"emergency\"}"));
  TEST_ASSERT_EQUAL_STRING("(null)", em_ref);
  sk_set_emergency_cb(NULL);
}

static void test_notification_without_position_calls_cb_with_no_position(void) {
  em_calls = 0;
  sk_set_emergency_cb(on_em);
  feed(MOB_NOTE("{\"state\":\"alarm\",\"message\":\"MAN OVERBOARD\"}"));
  TEST_ASSERT_EQUAL(1, em_calls);
  TEST_ASSERT_TRUE(em_active);
  TEST_ASSERT_FALSE(em_pos);
  feed(MOB_NOTE("{\"state\":\"emergency\",\"data\":{\"position\":{\"latitude\":\"x\",\"longitude\":1}}}"));
  TEST_ASSERT_FALSE(em_pos);   // a malformed position is no position
  sk_set_emergency_cb(NULL);
}

static void test_notification_normal_and_null_call_cb_inactive(void) {
  em_calls = 0;
  sk_set_emergency_cb(on_em);
  feed(MOB_NOTE("{\"state\":\"normal\"}"));
  TEST_ASSERT_EQUAL(1, em_calls);
  TEST_ASSERT_FALSE(em_active);
  feed(MOB_NOTE("null"));
  TEST_ASSERT_EQUAL(2, em_calls);
  TEST_ASSERT_FALSE(em_active);
  feed(MOB_NOTE("{\"state\":\"warn\"}"));   // a warn is not an emergency
  TEST_ASSERT_FALSE(em_active);
  sk_set_emergency_cb(NULL);
}

static void test_other_vessel_notification_ignored_by_cb(void) {
  em_calls = 0;
  sk_set_emergency_cb(on_em);
  feed("{\"context\":\"vessels.urn:mrn:imo:mmsi:999\",\"updates\":[{\"values\":[{\"path\":\"notifications.mob\",\"value\":{\"state\":\"emergency\"}}]}]}");
  TEST_ASSERT_EQUAL(0, em_calls);
  sk_set_emergency_cb(NULL);
}

static void test_notification_not_in_boatdata(void) {
  feed(NOTE("{\"state\":\"alarm\",\"message\":\"Hot\"}"));
  TEST_ASSERT_EQUAL(BD_NONE, bd_find("notifications.propulsion.port.temperature"));
}

static void test_subscribe_has_notifications_and_fixed_policy(void) {
  sk_sub_t subs[2] = {{bd_intern("a.b"), 5000}, {bd_intern("c.d"), 0}};
  char *s = sk_build_subscribe(subs, 2);
  TEST_ASSERT_NOT_NULL(strstr(s, "{\"path\":\"notifications.*\",\"policy\":\"instant\"}"));
  TEST_ASSERT_NOT_NULL(strstr(s, "\"path\":\"a.b\",\"policy\":\"fixed\",\"period\":5000"));
  TEST_ASSERT_NOT_NULL(strstr(s, "{\"path\":\"c.d\",\"policy\":\"instant\"}"));
  free(s);
}

static void test_alert_request_path(void) {
  alert_t a = {.state = AL_ALARM};
  char p[128];
  TEST_ASSERT_FALSE(sk_build_alert_request(&a, AL_REQ_ACK, p, sizeof p));
  strcpy(a.ref, "u-1");
  TEST_ASSERT_TRUE(sk_build_alert_request(&a, AL_REQ_ACK, p, sizeof p));
  TEST_ASSERT_EQUAL_STRING("/signalk/v2/api/notifications/u-1/acknowledge", p);
  TEST_ASSERT_TRUE(sk_build_alert_request(&a, AL_REQ_SILENCE, p, sizeof p));
  TEST_ASSERT_EQUAL_STRING("/signalk/v2/api/notifications/u-1/silence", p);
  TEST_ASSERT_FALSE(sk_build_alert_request(&a, AL_REQ_NONE, p, sizeof p));
}

static void test_delta_timestamp_kept(void) {
  feed("{\"context\":\"vessels.self\",\"updates\":[{\"timestamp\":\"2026-11-05T02:13:37.578Z\",\"values\":[{\"path\":\"navigation.anchor.currentRadius\",\"value\":12}]}]}");
  TEST_ASSERT_EQUAL_INT64(1793844817, get("navigation.anchor.currentRadius").src_s);
  feed("{\"context\":\"vessels.self\",\"updates\":[{\"values\":[{\"path\":\"navigation.anchor.currentRadius\",\"value\":13}]}]}");
  TEST_ASSERT_EQUAL_INT64(0, get("navigation.anchor.currentRadius").src_s);
}

static void test_anchor_watchzone_type_stored_as_text(void) {
  feed("{\"updates\":[{\"values\":[{\"path\":\"navigation.anchor.watchZone\",\"value\":{\"type\":\"circle\",\"radius\":50}}]}]}");
  bd_sample_t s = get("navigation.anchor.watchZone");
  TEST_ASSERT_TRUE(s.valid);
  TEST_ASSERT_EQUAL(BD_TEXT, s.kind);
  TEST_ASSERT_EQUAL_STRING("circle", s.text);
  feed("{\"updates\":[{\"values\":[{\"path\":\"navigation.anchor.watchZone\",\"value\":null}]}]}");
  TEST_ASSERT_FALSE(get("navigation.anchor.watchZone").valid);
}

static void test_other_objects_still_invalid(void) {
  feed("{\"updates\":[{\"values\":[{\"path\":\"a.b\",\"value\":{\"type\":\"circle\"}}]}]}");
  TEST_ASSERT_FALSE(get("a.b").valid);
}

static void test_environment_current_is_flattened(void) {
  feed("{\"updates\":[{\"values\":[{\"path\":\"environment.current\",\"value\":{\"setTrue\":2.4,\"drift\":0.62}}]}]}");
  bd_sample_t set = get("environment.current.setTrue"), drift = get("environment.current.drift");
  TEST_ASSERT_TRUE(set.valid);
  TEST_ASSERT_EQUAL_DOUBLE(2.4, set.num);
  TEST_ASSERT_EQUAL_UINT8(SK_SOURCE_ID, set.source);
  TEST_ASSERT_TRUE(drift.valid);
  TEST_ASSERT_EQUAL_DOUBLE(0.62, drift.num);
}

static void test_environment_current_null_invalidates_both(void) {
  feed("{\"updates\":[{\"values\":[{\"path\":\"environment.current\",\"value\":{\"setTrue\":2.4,\"drift\":0.62}}]}]}");
  feed("{\"updates\":[{\"values\":[{\"path\":\"environment.current\",\"value\":null}]}]}");
  TEST_ASSERT_FALSE(get("environment.current.setTrue").valid);
  TEST_ASSERT_FALSE(get("environment.current.drift").valid);
}

static void test_environment_current_partial(void) {
  feed("{\"updates\":[{\"values\":[{\"path\":\"environment.current\",\"value\":{\"drift\":0.5}}]}]}");
  TEST_ASSERT_TRUE(get("environment.current.drift").valid);
  TEST_ASSERT_FALSE(get("environment.current.setTrue").valid);
}

// Signal K preferred over a bus source: a value it replays on reconnect (old timestamp) must not displace a fresher
// bus value, and a null must not hide a working bus sensor.
static void test_replay_does_not_displace_a_fresher_bus_value(void) {
  bd_source_register(BD_SRC_SK, "signalk", 2);
  bd_source_register(BD_SRC_N2K, "n2k", 1);
  feed("{\"updates\":[{\"timestamp\":\"2026-11-05T12:00:00.000Z\",\"values\":[{\"path\":\"navigation.datetime\",\"value\":\"x\"}]}]}");
  bd_id_t id = bd_intern("propulsion.port.revolutions");
  bd_set_number(id, 40, BD_SRC_N2K, 1000);   // from the bus, current
  feed("{\"updates\":[{\"timestamp\":\"2026-11-05T09:00:00.000Z\",\"values\":[{\"path\":\"propulsion.port.revolutions\",\"value\":5}]}]}");
  bd_sample_t s = get("propulsion.port.revolutions");
  TEST_ASSERT_EQUAL_DOUBLE(40, s.num);
  TEST_ASSERT_EQUAL_UINT8(BD_SRC_N2K, s.source);
  TEST_ASSERT_EQUAL_INT64(0, s.src_s);   // the refused value left no timestamp on the bus sample
  feed("{\"updates\":[{\"timestamp\":\"2026-11-05T12:00:00.000Z\",\"values\":[{\"path\":\"propulsion.port.revolutions\",\"value\":41}]}]}");
  s = get("propulsion.port.revolutions");   // a current reading from the server wins
  TEST_ASSERT_EQUAL_DOUBLE(41, s.num);
  TEST_ASSERT_EQUAL_UINT8(BD_SRC_SK, s.source);
  TEST_ASSERT_NOT_EQUAL(0, s.src_s);
}

static void test_signalk_null_does_not_hide_a_bus_value(void) {
  bd_source_register(BD_SRC_SK, "signalk", 2);
  bd_source_register(BD_SRC_N2K, "n2k", 1);
  bd_id_t id = bd_intern("tanks.fuel.0.currentLevel");
  bd_set_number(id, 0.5, BD_SRC_N2K, 1000);
  feed("{\"updates\":[{\"values\":[{\"path\":\"tanks.fuel.0.currentLevel\",\"value\":null}]}]}");
  bd_sample_t s = get("tanks.fuel.0.currentLevel");
  TEST_ASSERT_TRUE(s.valid);
  TEST_ASSERT_EQUAL_DOUBLE(0.5, s.num);
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
  RUN_TEST(test_discovery_multi_segment_prefix);
  RUN_TEST(test_discovery_prefix_segment_boundary);
  RUN_TEST(test_discovery_single_segment_still_works);
  RUN_TEST(test_subscribe_json_shape);
  RUN_TEST(test_notification_raises_alert);
  RUN_TEST(test_notification_null_clears);
  RUN_TEST(test_notification_missing_state_clears);
  RUN_TEST(test_notification_normal_clears);
  RUN_TEST(test_parse_full_reads_name_and_mmsi);
  RUN_TEST(test_notification_calls_emergency_cb_with_position);
  RUN_TEST(test_servers_own_mob_carries_its_uuid_in_the_path_and_value_position);
  RUN_TEST(test_data_position_wins_over_the_servers_value_position);
  RUN_TEST(test_notification_without_id_calls_cb_with_null_ref);
  RUN_TEST(test_notification_without_position_calls_cb_with_no_position);
  RUN_TEST(test_notification_normal_and_null_call_cb_inactive);
  RUN_TEST(test_other_vessel_notification_ignored_by_cb);
  RUN_TEST(test_notification_not_in_boatdata);
  RUN_TEST(test_subscribe_has_notifications_and_fixed_policy);
  RUN_TEST(test_alert_request_path);
  RUN_TEST(test_delta_timestamp_kept);
  RUN_TEST(test_anchor_watchzone_type_stored_as_text);
  RUN_TEST(test_other_objects_still_invalid);
  RUN_TEST(test_environment_current_is_flattened);
  RUN_TEST(test_environment_current_null_invalidates_both);
  RUN_TEST(test_environment_current_partial);
  RUN_TEST(test_replay_does_not_displace_a_fresher_bus_value);
  RUN_TEST(test_signalk_null_does_not_hide_a_bus_value);
  return UNITY_END();
}
