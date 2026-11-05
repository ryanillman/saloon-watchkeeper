// The Glance tiles of Engine, Power and Tanks, from bound roles and live samples.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "alerts.h"
#include "anchor_ctl.h"
#include "anchor_watch.h"
#include "boatdata.h"
#include "history.h"
#include "role.h"
#include "unity.h"

#define NOW 1000000LL
static cfg_t cfg;
static cfg_role_t rc;
static role_binding_t b;
static glance_tile_t t;

void setUp(void) {
  bd_init();
  alerts_init();
  hist_init();
  cfg_defaults(&cfg);
}
void tearDown(void) {}

static bd_id_t num(const char *path, double v) {
  bd_id_t id = bd_intern(path);
  bd_set_number(id, v, 1, NOW);
  return id;
}
static void bind(const char *role) {
  memset(&rc, 0, sizeof rc);
  snprintf(rc.role, sizeof rc.role, "%s", role);
  role_bind(role_find(role), &rc, &cfg.units, &b);
}
static void tile(void) {
  memset(&t, 0, sizeof t);
  b.def->tile(&b, NOW, &t);
}
static void zone(bd_id_t id, double lo, double hi, alert_state_t st) {
  bd_meta_t m = {0};
  m.zone_count = 1;
  m.zones[0] = (bd_zone_t){lo, hi, (uint8_t)st};
  bd_set_meta(id, &m);
}
static void alarm_on(const char *path) {
  alert_t a = {0};
  snprintf(a.id, sizeof a.id, "%s", path);
  a.state = AL_ALARM;
  alerts_upsert(&a, NOW);
}

// ---- engine ----

static void run_engine(const char *name, double hz, double coolant_k, double oil_pa) {
  char p[64];
  snprintf(p, sizeof p, "propulsion.%s.revolutions", name); num(p, hz);
  snprintf(p, sizeof p, "propulsion.%s.temperature", name); num(p, coolant_k);
  snprintf(p, sizeof p, "propulsion.%s.oilPressure", name); num(p, oil_pa);
  snprintf(p, sizeof p, "propulsion.%s.alternatorVoltage", name); num(p, 14.0);
}

static void test_engine_twin_running_shows_both_rpm_and_ok(void) {
  run_engine("port", 25.0, 358.15, 300000);
  run_engine("starboard", 24.6667, 357.15, 310000);
  bind("engine");
  tile();
  TEST_ASSERT_EQUAL_STRING("ENGINE", t.title);
  TEST_ASSERT_EQUAL(GT_OK, t.state);
  TEST_ASSERT_EQUAL_STRING("OK", t.word);
  TEST_ASSERT_EQUAL_STRING("1500/1480", t.headline);
  TEST_ASSERT_EQUAL_STRING("RPM", t.unit);
  TEST_ASSERT_EQUAL_STRING("COOLANT 85 \xC2\xB0" "C", t.context[0]);
  TEST_ASSERT_EQUAL_STRING("OIL 3.0 bar", t.context[1]);
}

static void test_engine_one_in_alarm_shows_the_alarm(void) {
  run_engine("port", 25.0, 358.15, 300000);
  run_engine("starboard", 25.0, 358.15, 300000);
  alarm_on("propulsion.port.temperature");
  bind("engine");
  tile();
  TEST_ASSERT_EQUAL(GT_ALARM, t.state);
  TEST_ASSERT_EQUAL_STRING("ALARM", t.word);
}

static void test_engine_stopped_is_off_with_stopped_headline(void) {
  num("propulsion.port.revolutions", 0);
  num("propulsion.port.oilPressure", 0);
  num("propulsion.port.temperature", 314.15);   // 41 C, cooling
  num("propulsion.port.runTime", 4410000);       // 1225 h
  bind("engine");
  tile();
  TEST_ASSERT_EQUAL(GT_OFF, t.state);
  TEST_ASSERT_EQUAL_STRING("OFF", t.word);
  TEST_ASSERT_EQUAL_STRING("STOPPED", t.headline);
  TEST_ASSERT_EQUAL_STRING("HOURS 1225 h", t.context[0]);
  TEST_ASSERT_EQUAL_STRING("COOLANT 41 \xC2\xB0" "C", t.context[1]);
}

static void test_engine_one_running_one_stopped_is_ok_and_shows_the_running_one(void) {
  run_engine("port", 25.0, 358.15, 300000);
  num("propulsion.starboard.revolutions", 0);
  num("propulsion.starboard.oilPressure", 0);
  bind("engine");
  tile();
  TEST_ASSERT_EQUAL(GT_OK, t.state);
  TEST_ASSERT_EQUAL_STRING("1500", t.headline);
}

static void test_engine_stopped_with_an_alarm_shows_the_alarm_not_off(void) {   // ignition on, engine off, oil alarm raised
  num("propulsion.port.revolutions", 0);
  num("propulsion.port.oilPressure", 0);
  alarm_on("propulsion.port.oilPressure");
  bind("engine");
  tile();
  TEST_ASSERT_EQUAL(GT_ALARM, t.state);
  TEST_ASSERT_EQUAL_STRING("ALARM", t.word);
  TEST_ASSERT_EQUAL_STRING("STOPPED", t.headline);   // it is still not running
}

static void test_engine_of_unknown_state_with_an_alarm_shows_the_alarm_not_no_data(void) {
  bd_set_number(bd_intern("propulsion.port.revolutions"), 25.0, 1, NOW - 60000);
  bd_set_number(bd_intern("propulsion.port.oilPressure"), 300000, 1, NOW - 60000);
  alarm_on("propulsion.port.temperature");
  bind("engine");
  tile();
  TEST_ASSERT_EQUAL(GT_ALARM, t.state);
}

static void test_engine_with_stale_rpm_has_no_data_and_no_number(void) {
  bd_set_number(bd_intern("propulsion.port.revolutions"), 25.0, 1, NOW - 60000);
  bd_set_number(bd_intern("propulsion.port.oilPressure"), 300000, 1, NOW - 60000);
  bind("engine");
  tile();
  TEST_ASSERT_EQUAL(GT_NO_DATA, t.state);
  TEST_ASSERT_EQUAL_STRING("NO DATA", t.word);
  TEST_ASSERT_EQUAL_STRING("", t.headline);
}

static void test_engine_role_with_no_instance_is_no_data_not_off(void) {
  bind("engine");
  tile();
  TEST_ASSERT_EQUAL(GT_NO_DATA, t.state);
  TEST_ASSERT_EQUAL_STRING("", t.headline);
}

// ---- power ----

static void house(double soc, double volts, double amps, double secs) {
  num("electrical.batteries.279.capacity.stateOfCharge", soc);
  num("electrical.batteries.279.voltage", volts);
  num("electrical.batteries.279.current", amps);
  num("electrical.batteries.279.capacity.timeRemaining", secs);
}

static void test_power_tile_shows_house_soc_with_trend_and_context(void) {
  house(0.74, 12.6, -4.8, 100800);
  bind("power");
  tile();
  TEST_ASSERT_EQUAL_STRING("POWER", t.title);
  TEST_ASSERT_EQUAL(GT_OK, t.state);
  TEST_ASSERT_EQUAL_STRING("74", t.headline);
  TEST_ASSERT_EQUAL_STRING("%", t.unit);
  TEST_ASSERT_EQUAL_STRING("12.6 V  -4.8 A", t.context[0]);
  TEST_ASSERT_EQUAL_STRING("28 h left", t.context[1]);
  TEST_ASSERT_EQUAL_STRING("electrical.batteries.279.capacity.stateOfCharge", bd_path(t.trend));
}

static void test_power_invalid_soc_is_no_data_with_no_number(void) {
  house(0.74, 12.6, -4.8, 100800);
  bd_set_invalid(bd_find("electrical.batteries.279.capacity.stateOfCharge"));
  bind("power");
  tile();
  TEST_ASSERT_EQUAL(GT_NO_DATA, t.state);
  TEST_ASSERT_EQUAL_STRING("", t.headline);
}

static void test_power_start_bank_alarm_shows_on_the_tile(void) {
  house(0.74, 12.6, -4.8, 100800);
  bd_id_t sv = num("electrical.batteries.280.voltage", 10.9);
  num("electrical.batteries.280.capacity.stateOfCharge", 0.5);
  zone(sv, -1e9, 11.5, AL_ALARM);
  bind("power");
  tile();
  TEST_ASSERT_EQUAL(GT_ALARM, t.state);
}

static void test_power_with_no_battery_is_no_data(void) {
  bind("power");
  tile();
  TEST_ASSERT_EQUAL(GT_NO_DATA, t.state);
  TEST_ASSERT_EQUAL_STRING("", t.headline);
}

// ---- tanks ----

static bd_id_t three_tanks(void) {
  bd_id_t w = num("tanks.freshWater.0.currentLevel", 0.64);
  num("tanks.fuel.0.currentLevel", 0.82);
  num("tanks.blackWater.0.currentLevel", 0.41);
  return w;
}

static void test_tanks_tile_shows_water_fuel_waste_bars(void) {
  three_tanks();
  bind("tanks");
  tile();
  TEST_ASSERT_EQUAL_STRING("TANKS", t.title);
  TEST_ASSERT_EQUAL(GT_OK, t.state);
  TEST_ASSERT_EQUAL_UINT8(3, t.bar_count);
  TEST_ASSERT_EQUAL_CHAR('W', t.bar_label[0]);
  TEST_ASSERT_EQUAL_CHAR('F', t.bar_label[1]);
  TEST_ASSERT_EQUAL_CHAR('S', t.bar_label[2]);
  TEST_ASSERT_EQUAL_UINT8(64, t.bar_pct[0]);
  TEST_ASSERT_EQUAL_UINT8(82, t.bar_pct[1]);
  TEST_ASSERT_EQUAL_UINT8(41, t.bar_pct[2]);
  TEST_ASSERT_EQUAL_STRING("", t.headline);
}

static void test_tanks_one_in_a_warn_zone_makes_the_tile_warn(void) {
  three_tanks();
  zone(bd_find("tanks.fuel.0.currentLevel"), 0, 0.15, AL_WARN);
  num("tanks.fuel.0.currentLevel", 0.10);
  bind("tanks");
  tile();
  TEST_ASSERT_EQUAL(GT_WARN, t.state);
  TEST_ASSERT_EQUAL_UINT8(10, t.bar_pct[1]);
}

static void test_tanks_invalid_level_is_no_data_and_an_unknown_bar(void) {
  three_tanks();
  bd_set_invalid(bd_find("tanks.freshWater.0.currentLevel"));
  bind("tanks");
  tile();
  TEST_ASSERT_EQUAL(GT_NO_DATA, t.state);
  TEST_ASSERT_EQUAL_UINT8(GT_PCT_UNKNOWN, t.bar_pct[0]);   // never 0%: that reads as an empty tank
  TEST_ASSERT_EQUAL_UINT8(82, t.bar_pct[1]);               // the other bars keep their good levels
}

static void test_tanks_one_of_two_water_tanks_missing_makes_the_bar_unknown_not_the_other_level(void) {
  num("tanks.freshWater.0.currentLevel", 0.80);
  bd_id_t second = num("tanks.freshWater.1.currentLevel", 0.30);
  bd_set_invalid(second);
  bind("tanks");
  tile();
  TEST_ASSERT_EQUAL_UINT8(GT_PCT_UNKNOWN, t.bar_pct[0]);   // the lowest tank is not known
}

static void test_tanks_when_every_level_is_missing_every_bar_is_unknown(void) {
  three_tanks();
  bd_invalidate_source(1);   // Signal K dropped
  bind("tanks");
  tile();
  TEST_ASSERT_EQUAL(GT_NO_DATA, t.state);
  TEST_ASSERT_EQUAL_UINT8(3, t.bar_count);
  for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_UINT8(GT_PCT_UNKNOWN, t.bar_pct[i]);
}

static void test_tanks_two_water_tanks_show_the_lowest_and_missing_kinds_have_no_bar(void) {
  num("tanks.freshWater.0.currentLevel", 0.90);
  num("tanks.freshWater.1.currentLevel", 0.30);
  bind("tanks");
  tile();
  TEST_ASSERT_EQUAL_UINT8(1, t.bar_count);
  TEST_ASSERT_EQUAL_CHAR('W', t.bar_label[0]);
  TEST_ASSERT_EQUAL_UINT8(30, t.bar_pct[0]);
}

static void test_tanks_with_none_is_no_data(void) {
  bind("tanks");
  tile();
  TEST_ASSERT_EQUAL(GT_NO_DATA, t.state);
  TEST_ASSERT_EQUAL_UINT8(0, t.bar_count);
}

static void test_tanks_water_change_over_six_hours_from_history(void) {
  bd_id_t w = three_tanks();
  hist_track(w);
  for (int m = 0; m <= 360; m++) {
    int64_t ts = 5000000LL + m * 60000LL;
    bd_set_number(w, 0.73 - 0.09 * m / 360.0, 1, ts);
    hist_tick(ts);
  }
  num("tanks.freshWater.0.currentLevel", 0.64);
  bind("tanks");
  tile();
  TEST_ASSERT_EQUAL_STRING("WATER -9% in 6 h", t.context[0]);
  TEST_ASSERT_EQUAL(w, t.trend);   // tracked for the history; bars are drawn, not a trend line
}

// ---- Anchor ----

static anchor_watch_t awatch;
static void anchor_start(bool restoring) {
  anchor_watch_init(&awatch, NULL, 0, 60000);
  awatch.restoring = restoring;
  anchor_ctl_setup(NULL, ANCHOR_PLUGIN_HOEKEN, &awatch, NULL, &cfg.units);
  alerts_source_up(true);
  bind("anchor");
}
static void anchored(double dist, double limit) {
  bd_set_position(bd_intern("navigation.anchor.position"), 48.5, -123.0, 1, NOW);
  num("navigation.anchor.maxRadius", limit);
  num("navigation.anchor.currentRadius", dist);
  num("navigation.anchor.bearingTrue", 1.0);
}
static void anchor_alert(alert_state_t st) {
  alert_t a = {0};
  strcpy(a.id, "navigation.anchor");
  a.state = st;
  alerts_upsert(&a, NOW);
}

static void test_anchor_tile_states(void) {
  anchor_start(false);
  bd_set_number(bd_intern("navigation.anchor.position"), NAN, 1, NOW);   // the plugin says no anchor
  tile();
  TEST_ASSERT_EQUAL_STRING("ANCHOR", t.title);
  TEST_ASSERT_EQUAL(GT_OFF, t.state);
  TEST_ASSERT_EQUAL_STRING("NOT SET", t.word);
  TEST_ASSERT_NULL(t.swing);
  TEST_ASSERT_TRUE(t.has_swing);   // the page keeps room for the plan view whatever the data

  anchored(30, 50);
  tile();
  TEST_ASSERT_EQUAL(GT_OK, t.state);
  TEST_ASSERT_EQUAL_STRING("HOLDING", t.word);
  TEST_ASSERT_NOT_NULL(t.swing);
  anchor_alert(AL_WARN);
  tile();
  TEST_ASSERT_EQUAL(GT_WARN, t.state);
  anchor_alert(AL_ALARM);
  tile();
  TEST_ASSERT_EQUAL(GT_ALARM, t.state);
}

static void test_anchor_tile_never_ok_when_data_missing(void) {
  anchor_start(false);
  anchored(30, 50);
  bd_set_number(bd_intern("navigation.anchor.maxRadius"), NAN, 1, NOW);   // no limit
  tile();
  TEST_ASSERT_EQUAL(GT_NO_DATA, t.state);
  TEST_ASSERT_EQUAL_STRING("NO LIMIT", t.word);
  TEST_ASSERT_EQUAL_STRING("30", t.headline);   // the distance is still worth showing
  anchored(30, 50);
  alerts_source_up(false);   // the connection is gone
  tile();
  TEST_ASSERT_EQUAL(GT_NO_DATA, t.state);
  TEST_ASSERT_EQUAL_STRING("NO DATA", t.word);
  anchor_start(true);   // restoring, nothing shown back yet
  bd_init();
  anchor_start(true);
  tile();
  TEST_ASSERT_EQUAL(GT_NO_DATA, t.state);
  TEST_ASSERT_EQUAL_STRING("RESTORING", t.word);
}

static void test_anchor_tile_local_watch_alarm_outranks_no_data(void) {
  anchor_start(false);
  alerts_source_up(false);
  alert_t a = {0};
  strcpy(a.id, ANCHOR_WATCH_ALERT_ID);
  a.state = AL_ALARM;
  alerts_upsert_local(&a, NOW);
  tile();
  TEST_ASSERT_EQUAL(GT_ALARM, t.state);
}

static void test_anchor_tile_headline_and_limit_text(void) {
  anchor_start(false);
  anchored(30, 50);
  tile();
  TEST_ASSERT_EQUAL_STRING("30", t.headline);
  TEST_ASSERT_EQUAL_STRING("m", t.unit);
  TEST_ASSERT_NOT_NULL(strstr(t.context[0], "limit 50 m"));
  TEST_ASSERT_EQUAL_STRING("max tonight 30 m", t.context[1]);   // short enough for the narrowest tile
  num("navigation.anchor.rodeLength", 12);
  tile();
  TEST_ASSERT_NOT_NULL(strstr(t.context[0], "chain 12 m"));
  strcpy(cfg.units.distance, "ft");
  tile();
  TEST_ASSERT_EQUAL_STRING("98", t.headline);
  TEST_ASSERT_EQUAL_STRING("ft", t.unit);
  TEST_ASSERT_NOT_NULL(strstr(t.context[0], "limit 164 ft"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_engine_twin_running_shows_both_rpm_and_ok);
  RUN_TEST(test_engine_one_in_alarm_shows_the_alarm);
  RUN_TEST(test_engine_stopped_is_off_with_stopped_headline);
  RUN_TEST(test_engine_one_running_one_stopped_is_ok_and_shows_the_running_one);
  RUN_TEST(test_engine_stopped_with_an_alarm_shows_the_alarm_not_off);
  RUN_TEST(test_engine_of_unknown_state_with_an_alarm_shows_the_alarm_not_no_data);
  RUN_TEST(test_engine_with_stale_rpm_has_no_data_and_no_number);
  RUN_TEST(test_engine_role_with_no_instance_is_no_data_not_off);
  RUN_TEST(test_power_tile_shows_house_soc_with_trend_and_context);
  RUN_TEST(test_power_invalid_soc_is_no_data_with_no_number);
  RUN_TEST(test_power_start_bank_alarm_shows_on_the_tile);
  RUN_TEST(test_power_with_no_battery_is_no_data);
  RUN_TEST(test_tanks_tile_shows_water_fuel_waste_bars);
  RUN_TEST(test_tanks_one_in_a_warn_zone_makes_the_tile_warn);
  RUN_TEST(test_tanks_invalid_level_is_no_data_and_an_unknown_bar);
  RUN_TEST(test_tanks_one_of_two_water_tanks_missing_makes_the_bar_unknown_not_the_other_level);
  RUN_TEST(test_tanks_when_every_level_is_missing_every_bar_is_unknown);
  RUN_TEST(test_tanks_two_water_tanks_show_the_lowest_and_missing_kinds_have_no_bar);
  RUN_TEST(test_tanks_with_none_is_no_data);
  RUN_TEST(test_tanks_water_change_over_six_hours_from_history);
  RUN_TEST(test_anchor_tile_states);
  RUN_TEST(test_anchor_tile_never_ok_when_data_missing);
  RUN_TEST(test_anchor_tile_local_watch_alarm_outranks_no_data);
  RUN_TEST(test_anchor_tile_headline_and_limit_text);
  return UNITY_END();
}
