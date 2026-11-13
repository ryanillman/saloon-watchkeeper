#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "unity.h"
#include "config.h"
#include "cJSON.h"

static char dir[64];

static void write_file(const char *name, const char *text) {
  char p[128];
  snprintf(p, sizeof p, "%s/%s", dir, name);
  FILE *f = fopen(p, "w");
  fputs(text, f);
  fclose(f);
}

static char *read_file(const char *name) {
  char p[128];
  snprintf(p, sizeof p, "%s/%s", dir, name);
  FILE *f = fopen(p, "r");
  if (!f) return NULL;
  static char buf[8192];
  size_t n = fread(buf, 1, sizeof buf - 1, f);
  buf[n] = 0;
  fclose(f);
  return buf;
}

void setUp(void) {
  strcpy(dir, "/tmp/bi_cfg_XXXXXX");
  TEST_ASSERT_NOT_NULL(mkdtemp(dir));
}

void tearDown(void) {
  char cmd[96];
  snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
  system(cmd);
}

static const char *MINIMAL =
  "{\n"
  "  \"wifi\": { \"ssid\": \"Boatnet\", \"password\": \"pw\" },\n"
  "  \"roles\": [ { \"role\": \"engine\" } ]\n"
  "}\n";

static void test_minimal_file_loads(void) {
  write_file("config.json", MINIMAL);
  cfg_t c;
  cfg_report_t r;
  TEST_ASSERT_EQUAL(CFG_FROM_MAIN, cfg_load(dir, &c, &r));
  TEST_ASSERT_EQUAL_UINT8(0, r.count);
  TEST_ASSERT_EQUAL_STRING("Boatnet", c.wifi.ssid);
  TEST_ASSERT_EQUAL_UINT8(1, c.role_count);
  TEST_ASSERT_EQUAL_STRING("engine", c.roles[0].role);
  // unspecified sections take defaults
  TEST_ASSERT_EQUAL_STRING("auto", c.sk.host);
  TEST_ASSERT_EQUAL_UINT16(3000, c.sk.port);
  TEST_ASSERT_EQUAL_STRING("C", c.units.temperature);
  TEST_ASSERT_EQUAL_UINT8(80, c.brightness);
}

static void test_full_role_fields(void) {
  write_file("config.json",
    "{ \"roles\": [ { \"role\": \"engine\",\n"
    "  \"instances\": [ {\"id\": \"port\", \"label\": \"PORT\"}, {\"id\": \"starboard\", \"label\": \"STBD\"} ],\n"
    "  \"overrides\": { \"rpm\": { \"max\": 3600 } },\n"
    "  \"bind\": { \"port.coolant\": \"propulsion.port.exhaustTemperature\" },\n"
    "  \"exclude\": [ \"center\" ] } ],\n"
    "  \"sources\": { \"signalk\": { \"host\": \"10.0.0.5\", \"port\": 3443, \"token\": \"abc\" } } }\n");
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(0, r.count);
  cfg_role_t *e = &c.roles[0];
  TEST_ASSERT_EQUAL_UINT8(2, e->inst_count);
  TEST_ASSERT_EQUAL_STRING("STBD", e->inst[1].label);
  TEST_ASSERT_EQUAL_UINT8(1, e->ovr_count);
  TEST_ASSERT_EQUAL_STRING("rpm", e->ovr[0].slot);
  TEST_ASSERT_FALSE(e->ovr[0].has_min);
  TEST_ASSERT_TRUE(e->ovr[0].has_max);
  TEST_ASSERT_EQUAL_DOUBLE(3600, e->ovr[0].max);
  TEST_ASSERT_EQUAL_STRING("propulsion.port.exhaustTemperature", e->bind[0].path);
  TEST_ASSERT_EQUAL_STRING("center", e->exclude[0]);
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", c.sk.host);
  TEST_ASSERT_EQUAL_UINT16(3443, c.sk.port);
  TEST_ASSERT_EQUAL_STRING("abc", c.sk.token);
}

static void test_missing_main_uses_backup(void) {
  write_file("config.bak.json", MINIMAL);
  cfg_t c;
  cfg_report_t r;
  TEST_ASSERT_EQUAL(CFG_FROM_BACKUP, cfg_load(dir, &c, &r));
  TEST_ASSERT_EQUAL_STRING("Boatnet", c.wifi.ssid);
}

static void test_both_missing_uses_defaults(void) {
  cfg_t c;
  cfg_report_t r;
  TEST_ASSERT_EQUAL(CFG_FROM_DEFAULTS, cfg_load(dir, &c, &r));
  TEST_ASSERT_EQUAL_UINT8(5, c.role_count);
  TEST_ASSERT_EQUAL_STRING("glance", c.roles[0].role);   // the glance page comes first
  TEST_ASSERT_EQUAL_STRING("engine", c.roles[1].role);
  TEST_ASSERT_EQUAL_STRING("anchor", c.roles[4].role);
}

static void test_parse_error_reports_line_and_falls_back(void) {
  write_file("config.json",
    "{\n"                               // 1
    "  \"wifi\": {\n"                   // 2
    "    \"ssid\": \"Boatnet\"\n"       // 3
    "  },\n"                            // 4
    "  \"display\": {\n"                // 5
    "    \"palette\": \"day\",\n"       // 6
    "    \"brightness\": 80 80\n"       // 7
    "  }\n"
    "}\n");
  write_file("config.bak.json", MINIMAL);
  cfg_t c;
  cfg_report_t r;
  TEST_ASSERT_EQUAL(CFG_FROM_BACKUP, cfg_load(dir, &c, &r));
  TEST_ASSERT_EQUAL_UINT8(1, r.count);
  TEST_ASSERT_EQUAL_INT(7, r.issues[0].line);
}

static void test_unknown_role_skipped_others_load(void) {
  write_file("config.json", "{ \"roles\": [ {\"role\": \"engnie\"}, {\"role\": \"tanks\"} ] }");
  cfg_t c;
  cfg_report_t r;
  TEST_ASSERT_EQUAL(CFG_FROM_MAIN, cfg_load(dir, &c, &r));
  TEST_ASSERT_EQUAL_UINT8(1, r.count);
  TEST_ASSERT_EQUAL_STRING("roles[0].role", r.issues[0].key_path);
  TEST_ASSERT_EQUAL_UINT8(1, c.role_count);
  TEST_ASSERT_EQUAL_STRING("tanks", c.roles[0].role);
}

static void test_no_valid_roles_uses_default_roles(void) {
  write_file("config.json", "{ \"roles\": [ {\"role\": \"engnie\"} ] }");
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(5, c.role_count);   // never a blank display
}

static void test_unknown_keys_survive_save(void) {
  write_file("config.json",
    "{ \"note\": \"keep me\", \"wifi\": {\"ssid\": \"A\", \"channel\": 6},\n"
    "  \"roles\": [ {\"role\": \"engine\", \"comment\": \"twin\"} ] }");
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  strcpy(c.wifi.ssid, "B");
  TEST_ASSERT_TRUE(cfg_save(dir, &c));
  const char *out = read_file("config.json");
  TEST_ASSERT_NOT_NULL(strstr(out, "keep me"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"channel\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "twin"));
  cfg_t again;
  cfg_load(dir, &again, &r);
  TEST_ASSERT_EQUAL_STRING("B", again.wifi.ssid);
}

static void test_save_rotates_backup(void) {
  write_file("config.json", MINIMAL);
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  strcpy(c.wifi.ssid, "First");
  cfg_save(dir, &c);
  strcpy(c.wifi.ssid, "Second");
  cfg_save(dir, &c);
  TEST_ASSERT_NOT_NULL(strstr(read_file("config.bak.json"), "First"));
  TEST_ASSERT_NOT_NULL(strstr(read_file("config.json"), "Second"));
  TEST_ASSERT_NULL(read_file("config.tmp"));
}

static void test_version_written_and_missing_treated_as_1(void) {
  write_file("config.json", MINIMAL);   // no "version"
  cfg_t c;
  cfg_report_t r;
  TEST_ASSERT_EQUAL(CFG_FROM_MAIN, cfg_load(dir, &c, &r));
  TEST_ASSERT_EQUAL_UINT8(0, r.count);
  cfg_save(dir, &c);
  TEST_ASSERT_NOT_NULL(strstr(read_file("config.json"), "\"version\":\t1"));
}

static void test_save_from_defaults_creates_file(void) {
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_TRUE(cfg_save(dir, &c));
  cfg_t again;
  TEST_ASSERT_EQUAL(CFG_FROM_MAIN, cfg_load(dir, &again, &r));
  TEST_ASSERT_EQUAL_UINT8(5, again.role_count);
}

static void test_unknown_unit_reported_and_defaulted(void) {
  write_file("config.json", "{ \"units\": { \"temperature\": \"farenheit\", \"pressure\": \"psi\", \"speed\": \"mph\" } }");
  cfg_t c;
  cfg_report_t r;
  TEST_ASSERT_EQUAL(CFG_FROM_MAIN, cfg_load(dir, &c, &r));
  TEST_ASSERT_EQUAL_UINT8(2, r.count);
  TEST_ASSERT_EQUAL_STRING("units.temperature", r.issues[0].key_path);
  TEST_ASSERT_EQUAL_STRING("units.speed", r.issues[1].key_path);
  TEST_ASSERT_EQUAL_STRING("C", c.units.temperature);   // default kept
  TEST_ASSERT_EQUAL_STRING("psi", c.units.pressure);    // valid one applied
  TEST_ASSERT_EQUAL_STRING("kn", c.units.speed);
}

static void test_long_unit_value_not_truncated_into_a_valid_one(void) {
  write_file("config.json", "{ \"units\": { \"pressure\": \"barometric\" } }");   // must not become "bar"
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(1, r.count);
  TEST_ASSERT_EQUAL_STRING("units.pressure", r.issues[0].key_path);
}

static void test_distance_and_depth_units(void) {
  write_file("config.json", "{ \"units\": { \"distance\": \"km\", \"depth\": \"ft\" } }");
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(0, r.count);
  TEST_ASSERT_EQUAL_STRING("km", c.units.distance);
  TEST_ASSERT_EQUAL_STRING("ft", c.units.depth);
  cfg_defaults(&c);
  TEST_ASSERT_EQUAL_STRING("nm", c.units.distance);
  TEST_ASSERT_EQUAL_STRING("m", c.units.depth);
}

// A file from before the depth unit: distance "ft" or "m" meant the depth unit. Honoured, not reported.
static void test_legacy_distance_ft_sets_depth(void) {
  write_file("config.json", "{ \"units\": { \"distance\": \"ft\" } }");
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(0, r.count);
  TEST_ASSERT_EQUAL_STRING("nm", c.units.distance);
  TEST_ASSERT_EQUAL_STRING("ft", c.units.depth);
}

static void test_power_primary_round_trip(void) {
  write_file("config.json",
    "{ \"roles\": [ { \"role\": \"power\", \"primary\": \"279\", \"x-note\": \"keep\" } ] }\n");
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_STRING("279", c.roles[0].primary);
  TEST_ASSERT_TRUE(cfg_save(dir, &c));
  cfg_t d;
  cfg_load(dir, &d, &r);
  TEST_ASSERT_EQUAL_STRING("279", d.roles[0].primary);
  TEST_ASSERT_NOT_NULL(strstr(read_file("config.json"), "x-note"));
}

// ---- power configuration: banks with ids, flows with NAME refs ----

#define N1 "c046aa002cc001f9"
#define N2 "c046a0002cc001f5"
#define N3 "c046a0002cc001f8"

static const char *BANKS_OK =
  "{ \"roles\": [ { \"role\": \"power\", \"primary\": \"279\", \"x-note\": \"keep\",\n"
  "  \"banks\": [ {\"id\":\"house\",\"name\":\"HOUSE\",\"kind\":\"house\",\"battery\":\"" N1 ".0\"},\n"
  "               {\"id\":\"start\",\"name\":\"START\",\"kind\":\"start\",\"battery\":\"" N1 ".1\"},\n"
  "               {\"id\":\"bow\",\"name\":\"THRUSTER\",\"kind\":\"thruster\",\"battery\":\"" N2 ".0\"},\n"
  "               {\"id\":\"odd\",\"name\":\"ODD JOB\",\"kind\":\"other\",\"battery\":\"" N3 ".0\"} ],\n"
  "  \"flows\": [ {\"device\":\"" N2 "\",\"kind\":\"solar\",\"to\":\"house\"},\n"
  "               {\"device\":\"" N3 "\",\"kind\":\"shore\",\"to\":\"house\",\"output\":1},\n"
  "               {\"device\":\"aa00000000000001\",\"kind\":\"engine\",\"to\":\"start\"},\n"
  "               {\"device\":\"aa00000000000002\",\"kind\":\"converter\",\"from\":\"start\",\"to\":\"house\"} ] } ] }\n";

static void test_power_roundtrip_all_kinds(void) {
  write_file("config.json", BANKS_OK);
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(0, r.count);
  const cfg_role_t *p = &c.roles[0];
  TEST_ASSERT_EQUAL_UINT8(4, p->bank_count);
  TEST_ASSERT_EQUAL_UINT8(4, p->flow_count);
  TEST_ASSERT_EQUAL_STRING("start", p->bank[1].id);
  TEST_ASSERT_EQUAL_STRING("thruster", p->bank[2].kind);
  TEST_ASSERT_EQUAL_STRING("ODD JOB", p->bank[3].name);
  TEST_ASSERT_EQUAL_STRING(N1 ".1", p->bank[1].battery);
  TEST_ASSERT_EQUAL_STRING("shore", p->flow[1].kind);
  TEST_ASSERT_EQUAL_UINT8(1, p->flow[1].output);
  TEST_ASSERT_EQUAL_STRING("start", p->flow[3].from);
  TEST_ASSERT_TRUE(cfg_save(dir, &c));
  cfg_t d;
  cfg_load(dir, &d, &r);
  TEST_ASSERT_EQUAL_UINT8(0, r.count);
  TEST_ASSERT_EQUAL_UINT8(4, d.roles[0].bank_count);
  TEST_ASSERT_EQUAL_UINT8(4, d.roles[0].flow_count);
  TEST_ASSERT_EQUAL_STRING("aa00000000000002", d.roles[0].flow[3].device);
  TEST_ASSERT_EQUAL_STRING("house", d.roles[0].flow[3].to);
  TEST_ASSERT_NOT_NULL(strstr(read_file("config.json"), "x-note"));
}

// A bad topology is reported once and dropped; the rest of the role (primary) still loads.
static void expect_rejected(const char *banks_and_flows, const char *message_part) {
  char text[2048];
  snprintf(text, sizeof text, "{ \"roles\": [ { \"role\": \"power\", \"primary\": \"279\", %s } ] }", banks_and_flows);
  write_file("config.json", text);
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(1, r.count);
  TEST_ASSERT_EQUAL_STRING("roles.power.banks", r.issues[0].key_path);
  if (message_part) TEST_ASSERT_NOT_NULL(strstr(r.issues[0].message, message_part));
  TEST_ASSERT_EQUAL_UINT8(0, c.roles[0].bank_count);
  TEST_ASSERT_EQUAL_UINT8(0, c.roles[0].flow_count);
  TEST_ASSERT_EQUAL_STRING("279", c.roles[0].primary);
}
#define HB "{\"id\":\"house\",\"kind\":\"house\",\"battery\":\"" N1 ".0\"}"

static void test_power_two_banks_same_kind_ok(void) {
  write_file("config.json", "{ \"roles\": [ { \"role\": \"power\", \"banks\": [" HB ","
    "{\"id\":\"s1\",\"kind\":\"start\",\"battery\":\"" N1 ".1\"}, {\"id\":\"s2\",\"kind\":\"start\",\"battery\":\"" N2 ".0\"} ] } ] }");
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(0, r.count);
  TEST_ASSERT_EQUAL_UINT8(3, c.roles[0].bank_count);
}
static void test_power_two_house_rejected(void) {
  expect_rejected("\"banks\": [" HB ", {\"id\":\"h2\",\"kind\":\"house\",\"battery\":\"" N2 ".0\"} ]", "house");
}
static void test_power_no_house_rejected(void) {
  expect_rejected("\"banks\": [ {\"id\":\"s\",\"kind\":\"start\",\"battery\":\"" N1 ".0\"} ]", "house");
}
static void test_power_unknown_kind_rejected(void) {
  expect_rejected("\"banks\": [" HB ", {\"id\":\"g\",\"kind\":\"galley\",\"battery\":\"" N2 ".0\"} ]", "kind");
}
static void test_power_dangling_flow_to_rejected(void) {
  expect_rejected("\"banks\": [" HB "], \"flows\": [ {\"device\":\"" N2 "\",\"kind\":\"solar\",\"to\":\"nav\"} ]", "to");
}
static void test_power_converter_needs_from_and_others_refuse_it(void) {
  expect_rejected("\"banks\": [" HB "], \"flows\": [ {\"device\":\"" N2 "\",\"kind\":\"converter\",\"to\":\"house\"} ]", "from");
  expect_rejected("\"banks\": [" HB "], \"flows\": [ {\"device\":\"" N2 "\",\"kind\":\"solar\",\"from\":\"house\",\"to\":\"house\"} ]", "from");
}
static void test_power_other_needs_name(void) {
  expect_rejected("\"banks\": [" HB ", {\"id\":\"o\",\"kind\":\"other\",\"battery\":\"" N2 ".0\"} ]", "name");
}
static void test_power_duplicate_battery_ref_rejected(void) {
  expect_rejected("\"banks\": [" HB ", {\"id\":\"s\",\"kind\":\"start\",\"battery\":\"" N1 ".0\"} ]", "battery");
}
static void test_power_bad_ref_rejected(void) {
  expect_rejected("\"banks\": [ {\"id\":\"house\",\"kind\":\"house\",\"battery\":\"279\"} ]", "battery");
}
static void test_power_too_many_banks_rejected(void) {
  char b[1024] = "\"banks\": [";
  for (int i = 0; i < CFG_MAX_BANKS + 1; i++) {
    char one[128];
    snprintf(one, sizeof one, "%s{\"id\":\"b%d\",\"kind\":\"%s\",\"battery\":\"" N1 ".%d\"}", i ? "," : "", i, i ? "start" : "house", i);
    strcat(b, one);
  }
  strcat(b, "]");
  expect_rejected(b, "too many");
}

static void test_power_two_flows_on_one_output_rejected(void) {
  expect_rejected("\"banks\": [" HB "], \"flows\": [ {\"device\":\"" N2 "\",\"kind\":\"solar\",\"to\":\"house\"},"
                  " {\"device\":\"" N2 "\",\"kind\":\"other\",\"to\":\"house\"} ]", "output");
}

// Old-format files (instance-id banks, links with group:instance) are not read; the message says so.
static void test_power_old_keys_message_and_flag(void) {
  write_file("config.json", "{ \"roles\": [ { \"role\": \"power\", \"banks\": [ {\"id\":\"279\",\"kind\":\"house\"} ],"
                            " \"links\": [ {\"src\":\"charger:276\",\"to\":\"house\"} ] } ] }");
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(1, r.count);
  TEST_ASSERT_NOT_NULL(strstr(r.issues[0].message, "old-format"));
  TEST_ASSERT_TRUE(cfg_power_has_old_keys(&c.roles[0]));
  TEST_ASSERT_EQUAL_UINT8(0, c.roles[0].bank_count);
}

static void test_power_banks_without_flows_is_valid(void) {
  write_file("config.json", "{ \"roles\": [ { \"role\": \"power\", \"banks\": [" HB "] } ] }");
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(0, r.count);
  TEST_ASSERT_EQUAL_UINT8(1, c.roles[0].bank_count);
  TEST_ASSERT_EQUAL_UINT8(0, c.roles[0].flow_count);
}

// A rejected topology is not loaded, but a save from the setup screen must not delete the user's text.
static void test_rejected_banks_survive_a_save(void) {
  write_file("config.json", "{ \"roles\": [ { \"role\": \"power\", \"banks\": [ {\"id\":\"1\",\"kind\":\"galley\"} ] } ] }");
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(1, r.count);
  TEST_ASSERT_TRUE(cfg_save(dir, &c));
  TEST_ASSERT_NOT_NULL(strstr(read_file("config.json"), "galley"));
}

// Banks removed on the display are removed from the file (only a rejected topology is kept as written).
static void test_cleared_banks_are_removed_on_save(void) {
  write_file("config.json", BANKS_OK);
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  c.roles[0].bank_count = 0;
  c.roles[0].flow_count = 0;
  TEST_ASSERT_TRUE(cfg_save(dir, &c));
  TEST_ASSERT_NULL(strstr(read_file("config.json"), "banks"));
  TEST_ASSERT_NULL(strstr(read_file("config.json"), "flows"));
}

static void test_no_banks_key_changes_nothing(void) {
  write_file("config.json", "{ \"roles\": [ { \"role\": \"power\", \"primary\": \"279\" } ] }");
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(0, r.count);
  TEST_ASSERT_EQUAL_UINT8(0, c.roles[0].bank_count);
  TEST_ASSERT_TRUE(cfg_save(dir, &c));
  TEST_ASSERT_NULL(strstr(read_file("config.json"), "banks"));
}

// ---- milestone 5a: unknown roles survive, key-path edits ----

static const char *WITH_UNKNOWN =
  "{\n"
  "  \"wifi\": { \"ssid\": \"Boatnet\", \"password\": \"pw\" },\n"
  "  \"roles\": [\n"
  "    { \"role\": \"engine\" },\n"
  "    { \"role\": \"windlass\", \"mine\": 1 },\n"
  "    { \"role\": \"tanks\", \"exclude\": [\"x.0\"] },\n"
  "    { \"role\": \"anchor\" },\n"
  "    { \"role\": \"foo\", \"bar\": [1, 2] }\n"
  "  ]\n"
  "}\n";

static cJSON *roles_of(const char *text) {
  cJSON *root = cJSON_Parse(text);
  TEST_ASSERT_NOT_NULL(root);
  return root;
}

static void test_unknown_roles_survive_save(void) {
  write_file("config.json", WITH_UNKNOWN);
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(3, c.role_count);
  strcpy(c.palette, "night");
  TEST_ASSERT_TRUE(cfg_save(dir, &c));
  cJSON *root = roles_of(read_file("config.json"));
  cJSON *roles = cJSON_GetObjectItem(root, "roles");
  TEST_ASSERT_EQUAL_INT(5, cJSON_GetArraySize(roles));
  const char *want[] = {"engine", "tanks", "anchor", "windlass", "foo"};   // known first, unknown kept after
  for (int i = 0; i < 5; i++)
    TEST_ASSERT_EQUAL_STRING(want[i], cJSON_GetObjectItem(cJSON_GetArrayItem(roles, i), "role")->valuestring);
  TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(cJSON_GetArrayItem(roles, 1), "exclude"));   // tanks kept its exclude
  TEST_ASSERT_EQUAL_INT(1, cJSON_GetObjectItem(cJSON_GetArrayItem(roles, 3), "mine")->valueint);
  TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(cJSON_GetObjectItem(cJSON_GetArrayItem(roles, 4), "bar")));
  cJSON_Delete(root);
}

static void test_reordered_roles_keep_their_settings(void) {
  write_file("config.json", WITH_UNKNOWN);
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  cfg_role_t t = c.roles[0];   // swap engine and tanks
  c.roles[0] = c.roles[1];
  c.roles[1] = t;
  TEST_ASSERT_TRUE(cfg_save(dir, &c));
  cfg_t back;
  cfg_load(dir, &back, &r);
  TEST_ASSERT_EQUAL_STRING("tanks", back.roles[0].role);
  TEST_ASSERT_EQUAL_UINT8(1, back.roles[0].exclude_count);
  TEST_ASSERT_EQUAL_STRING("engine", back.roles[1].role);
  TEST_ASSERT_EQUAL_UINT8(0, back.roles[1].exclude_count);
}

static void test_cfg_set_string(void) {
  write_file("config.json", MINIMAL);
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  cfg_t out;
  TEST_ASSERT_TRUE(cfg_set(dir, "sources.signalk.token", "abc123", &out, &r));
  TEST_ASSERT_EQUAL_STRING("abc123", out.sk.token);
  cfg_t back;
  cfg_load(dir, &back, &r);
  TEST_ASSERT_EQUAL_STRING("abc123", back.sk.token);
  TEST_ASSERT_EQUAL_STRING("Boatnet", back.wifi.ssid);
}

static void test_cfg_set_number_and_bool(void) {
  write_file("config.json",
             "{ \"wifi\": {\"ssid\": \"B\"}, \"display\": {\"brightness\": 80}, \"buzzer\": {\"enabled\": false},"
             "  \"roles\": [ {\"role\": \"engine\"} ] }");
  cfg_t c, out;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_TRUE(cfg_set(dir, "display.brightness", "40", &out, &r));
  TEST_ASSERT_EQUAL_UINT8(40, out.brightness);
  TEST_ASSERT_TRUE(cfg_set(dir, "buzzer.enabled", "true", &out, &r));
  TEST_ASSERT_TRUE(out.buzzer);
  cJSON *root = roles_of(read_file("config.json"));
  TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "display"), "brightness")));
  TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "buzzer"), "enabled")));
  cJSON_Delete(root);
}

static void test_cfg_reset_after_swipe_defaults_false_and_parses(void) {
  write_file("config.json", "{ \"wifi\": {\"ssid\": \"B\"}, \"roles\": [ {\"role\": \"engine\"} ] }");
  cfg_t c, out;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_FALSE(c.reset_after_swipe);
  TEST_ASSERT_TRUE(cfg_set(dir, "display.reset_after_swipe", "true", &out, &r));   // the key was absent: set as text
  TEST_ASSERT_TRUE(out.reset_after_swipe);
  cfg_t back;
  cfg_load(dir, &back, &r);
  TEST_ASSERT_TRUE(back.reset_after_swipe);
  cJSON *root = roles_of(read_file("config.json"));
  TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "display"), "reset_after_swipe")));
  cJSON_Delete(root);
  TEST_ASSERT_TRUE(cfg_set(dir, "display.reset_after_swipe", "false", &out, &r));
  TEST_ASSERT_FALSE(out.reset_after_swipe);
}

static void test_cfg_vessel_name_roundtrips(void) {
  write_file("config.json", "{ \"wifi\": {\"ssid\": \"B\"}, \"roles\": [ {\"role\": \"engine\"} ] }");
  cfg_t c, out;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_STRING("", c.vessel_name);
  TEST_ASSERT_TRUE(cfg_set(dir, "vessel.name", "Saloon", &out, &r));
  TEST_ASSERT_EQUAL_STRING("Saloon", out.vessel_name);
  cfg_t back;
  cfg_load(dir, &back, &r);
  TEST_ASSERT_EQUAL_STRING("Saloon", back.vessel_name);
}

static void test_cfg_set_rejects_bad_number(void) {
  write_file("config.json",
             "{ \"wifi\": {\"ssid\": \"B\"}, \"display\": {\"brightness\": 80}, \"roles\": [ {\"role\": \"engine\"} ] }");
  cfg_t c, out;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  char before[1024];
  snprintf(before, sizeof before, "%s", read_file("config.json"));
  TEST_ASSERT_FALSE(cfg_set(dir, "display.brightness", "abc", &out, &r));
  TEST_ASSERT_TRUE(r.count >= 1);
  TEST_ASSERT_EQUAL_STRING("display.brightness", r.issues[0].key_path);
  TEST_ASSERT_EQUAL_STRING(before, read_file("config.json"));
}

static void test_cfg_set_refuses_a_path_through_a_non_object(void) {
  write_file("config.json",
             "{ \"units\": {\"temperature\": \"F\"}, \"roles\": [ {\"role\": \"engine\"} ] }");
  cfg_t c, out;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  char before[1024];
  snprintf(before, sizeof before, "%s", read_file("config.json"));
  TEST_ASSERT_FALSE(cfg_set(dir, "roles.foo", "x", &out, &r));
  TEST_ASSERT_FALSE(cfg_set(dir, "units.temperature.foo", "x", &out, &r));
  TEST_ASSERT_EQUAL_STRING(before, read_file("config.json"));
  cfg_t back;
  cfg_load(dir, &back, &r);
  TEST_ASSERT_EQUAL_STRING("engine", back.roles[0].role);
  TEST_ASSERT_EQUAL_STRING("F", back.units.temperature);
}

static void test_cfg_set_reports_out_of_range_numbers(void) {
  write_file("config.json",
             "{ \"sources\": {\"signalk\": {\"port\": 3000}}, \"display\": {\"brightness\": 80}, "
             "\"roles\": [ {\"role\": \"engine\"} ] }");
  cfg_t c, out;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_FALSE(cfg_set(dir, "sources.signalk.port", "70000", &out, &r));
  TEST_ASSERT_FALSE(cfg_set(dir, "display.brightness", "500", &out, &r));
  TEST_ASSERT_TRUE(cfg_set(dir, "display.brightness", "50", &out, &r));
}

static void test_cfg_set_rejects_invalid_value(void) {
  write_file("config.json", MINIMAL);
  cfg_t c, out;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  char before[1024];
  snprintf(before, sizeof before, "%s", read_file("config.json"));
  TEST_ASSERT_FALSE(cfg_set(dir, "units.temperature", "K", &out, &r));
  TEST_ASSERT_EQUAL_STRING("units.temperature", r.issues[0].key_path);
  TEST_ASSERT_NOT_NULL(strstr(r.issues[0].message, "unknown unit"));
  TEST_ASSERT_EQUAL_STRING(before, read_file("config.json"));
  // the cached tree is untouched too: a later save does not carry the rejected value
  TEST_ASSERT_TRUE(cfg_save(dir, &c));
  TEST_ASSERT_NULL(strstr(read_file("config.json"), "\"K\""));
}

static void test_cfg_set_creates_missing_string_key(void) {
  write_file("config.json", MINIMAL);
  cfg_t c, out;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_TRUE(cfg_set(dir, "sources.signalk.host", "192.168.0.108", &out, &r));
  TEST_ASSERT_EQUAL_STRING("192.168.0.108", out.sk.host);
}

static void test_cfg_set_utc_offset_on_a_file_that_lacks_the_key(void) {
  // The key is new: `config set` creates a missing key as a string. It must still take effect, and be saved as a number.
  write_file("config.json", "{ \"display\": {\"brightness\": 80}, \"roles\": [ {\"role\": \"engine\"} ] }");
  cfg_t c, out;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_TRUE(cfg_set(dir, "display.utc_offset_min", "-300", &out, &r));
  TEST_ASSERT_EQUAL_INT16(-300, out.utc_offset_min);
  cJSON *root = roles_of(read_file("config.json"));
  cJSON *tz = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "display"), "utc_offset_min");
  TEST_ASSERT_TRUE(cJSON_IsNumber(tz));
  TEST_ASSERT_EQUAL_INT(-300, tz->valueint);
  cJSON_Delete(root);
}

static void test_cfg_set_rejects_a_utc_offset_that_is_not_a_number(void) {
  write_file("config.json", "{ \"display\": {\"utc_offset_min\": 60}, \"roles\": [ {\"role\": \"engine\"} ] }");
  cfg_t c, out;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_FALSE(cfg_set(dir, "display.utc_offset_min", "abc", &out, &r));
  TEST_ASSERT_EQUAL_UINT8(1, r.count);
  TEST_ASSERT_EQUAL_STRING("display.utc_offset_min", r.issues[0].key_path);
}

static void test_cfg_show_masks_secrets(void) {
  write_file("config.json",
             "{ \"wifi\": {\"ssid\": \"B\", \"password\": \"hunter22\"},"
             "  \"sources\": {\"signalk\": {\"token\": \"tok-secret\"}}, \"roles\": [ {\"role\": \"engine\"} ] }");
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  char *s = cfg_show(dir);
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_NULL(strstr(s, "hunter22"));
  TEST_ASSERT_NULL(strstr(s, "tok-secret"));
  TEST_ASSERT_NOT_NULL(strstr(s, "set (8 chars)"));
  TEST_ASSERT_NOT_NULL(strstr(s, "set (10 chars)"));
  free(s);
}

static void test_utc_offset_default_is_zero(void) {
  cfg_t c;
  cfg_defaults(&c);
  TEST_ASSERT_EQUAL_INT16(0, c.utc_offset_min);
}

static void test_utc_offset_loads_and_clamps(void) {
  cfg_t c;
  cfg_report_t r;
  write_file("config.json", "{ \"display\": {\"utc_offset_min\": -480} }");
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_INT16(-480, c.utc_offset_min);
  write_file("config.json", "{ \"display\": {\"utc_offset_min\": 2000} }");
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_INT16(840, c.utc_offset_min);
  write_file("config.json", "{ \"display\": {\"utc_offset_min\": -2000} }");
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_INT16(-720, c.utc_offset_min);
}

static void test_utc_offset_saves_and_keeps_unknown_display_keys(void) {
  cfg_t c;
  cfg_report_t r;
  write_file("config.json", "{ \"display\": {\"utc_offset_min\": 60, \"theme_note\": \"keep\"} }");
  cfg_load(dir, &c, &r);
  c.utc_offset_min = 330;
  TEST_ASSERT_TRUE(cfg_save(dir, &c));
  TEST_ASSERT_NOT_NULL(strstr(read_file("config.json"), "theme_note"));
  cfg_t again;
  cfg_load(dir, &again, &r);
  TEST_ASSERT_EQUAL_INT16(330, again.utc_offset_min);
}

static void test_anchor_plugin_default(void) {
  write_file("config.json", MINIMAL);
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_STRING("hoekens-anchor-alarm", c.anchor_plugin);
  cfg_t d;
  cfg_defaults(&d);
  TEST_ASSERT_EQUAL_STRING("hoekens-anchor-alarm", d.anchor_plugin);
}

static void test_anchor_plugin_roundtrip(void) {
  write_file("config.json", "{ \"anchor\": { \"plugin\": \"anchoralarm\" }, \"roles\": [ { \"role\": \"anchor\" } ] }");
  cfg_t c, back;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(0, r.count);
  TEST_ASSERT_EQUAL_STRING("anchoralarm", c.anchor_plugin);
  TEST_ASSERT_TRUE(cfg_save(dir, &c));
  cfg_load(dir, &back, &r);
  TEST_ASSERT_EQUAL_STRING("anchoralarm", back.anchor_plugin);
}

static void test_anchor_plugin_unknown_reports_issue(void) {
  write_file("config.json", "{ \"anchor\": { \"plugin\": \"nope\" }, \"roles\": [ { \"role\": \"anchor\" } ] }");
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_STRING("hoekens-anchor-alarm", c.anchor_plugin);
  TEST_ASSERT_EQUAL_UINT8(1, r.count);
  TEST_ASSERT_EQUAL_STRING("anchor.plugin", r.issues[0].key_path);
}

static void test_cfg_set_anchor_plugin_rejects_unknown(void) {
  write_file("config.json", "{ \"anchor\": { \"plugin\": \"anchoralarm\" }, \"roles\": [ { \"role\": \"anchor\" } ] }");
  cfg_t c, out;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  char before[1024];
  snprintf(before, sizeof before, "%s", read_file("config.json"));
  TEST_ASSERT_FALSE(cfg_set(dir, "anchor.plugin", "bogus", &out, &r));
  TEST_ASSERT_EQUAL_STRING(before, read_file("config.json"));
  TEST_ASSERT_TRUE(cfg_set(dir, "anchor.plugin", "hoekens-anchor-alarm", &out, &r));
  TEST_ASSERT_EQUAL_STRING("hoekens-anchor-alarm", out.anchor_plugin);
}

static void test_navigation_role_known_wind_and_nav_not(void) {
  write_file("config.json", "{\"roles\":[{\"role\":\"engine\"},{\"role\":\"navigation\"}]}");
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(0, r.count);
  TEST_ASSERT_EQUAL_UINT8(2, c.role_count);
  TEST_ASSERT_EQUAL_STRING("navigation", c.roles[1].role);
  write_file("config.json", "{\"roles\":[{\"role\":\"engine\"},{\"role\":\"wind\"},{\"role\":\"nav\"}]}");
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(2, r.count);   // both are reported as unknown roles now
  TEST_ASSERT_EQUAL_UINT8(1, c.role_count);
}


// ---- sources.nmea2000 ----
static void test_nmea2000_defaults_off(void) {
  write_file("config.json", MINIMAL);
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_FALSE(c.n2k.enabled);
  TEST_ASSERT_FALSE(c.n2k.only);
  TEST_ASSERT_EQUAL_UINT8(0, c.n2k.id_count);
  TEST_ASSERT_EQUAL_UINT8(0, r.count);
}

static void test_nmea2000_parsed(void) {
  write_file("config.json",
    "{ \"sources\": { \"nmea2000\": { \"enabled\": true, \"mode\": \"only\",\n"
    "    \"ids\": { \"engine.0\": \"port\", \"battery.1\": \"279\" } } },\n"
    "  \"roles\": [ {\"role\": \"engine\"} ] }");
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(0, r.count);
  TEST_ASSERT_TRUE(c.n2k.enabled);
  TEST_ASSERT_TRUE(c.n2k.only);
  TEST_ASSERT_EQUAL_UINT8(2, c.n2k.id_count);
  TEST_ASSERT_EQUAL_STRING("engine.0", c.n2k.ids[0].key);
  TEST_ASSERT_EQUAL_STRING("port", c.n2k.ids[0].id);
  TEST_ASSERT_EQUAL_STRING("battery.1", c.n2k.ids[1].key);
  TEST_ASSERT_EQUAL_STRING("279", c.n2k.ids[1].id);
}

static void test_nmea2000_bad_mode_reports_and_stays_fallback(void) {
  write_file("config.json", "{ \"sources\": { \"nmea2000\": { \"enabled\": true, \"mode\": \"both\" } } }");
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_TRUE(c.n2k.enabled);
  TEST_ASSERT_FALSE(c.n2k.only);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT8(1, r.count);
  TEST_ASSERT_NOT_NULL(strstr(r.issues[0].key_path, "nmea2000.mode"));
  TEST_ASSERT_NOT_NULL(strstr(r.issues[0].message, "fallback"));
}

static void test_nmea2000_too_many_ids_reports(void) {
  char text[2048] = "{ \"sources\": { \"nmea2000\": { \"enabled\": true, \"ids\": {";
  for (int i = 0; i < 18; i++) {
    char one[48];
    snprintf(one, sizeof one, "%s\"engine.%d\": \"e%d\"", i ? "," : "", i, i);
    strcat(text, one);
  }
  strcat(text, "} } } }");
  write_file("config.json", text);
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(16, c.n2k.id_count);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT8(1, r.count);
}

static void test_nmea2000_survives_a_save(void) {
  write_file("config.json",
    "{ \"sources\": { \"nmea2000\": { \"enabled\": true, \"mode\": \"only\", \"ids\": { \"engine.0\": \"port\" } } },\n"
    "  \"roles\": [ {\"role\": \"engine\"} ] }");
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  strcpy(c.wifi.ssid, "X");
  TEST_ASSERT_TRUE(cfg_save(dir, &c));
  cfg_t again;
  cfg_load(dir, &again, &r);
  TEST_ASSERT_TRUE(again.n2k.enabled);
  TEST_ASSERT_TRUE(again.n2k.only);
  TEST_ASSERT_EQUAL_UINT8(1, again.n2k.id_count);
  TEST_ASSERT_EQUAL_STRING("port", again.n2k.ids[0].id);
}

static void test_nmea2000_not_added_to_a_file_without_it(void) {
  write_file("config.json", MINIMAL);
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_TRUE(cfg_save(dir, &c));
  TEST_ASSERT_NULL(strstr(read_file("config.json"), "nmea2000"));
}

static void test_cfg_set_enables_nmea2000(void) {
  write_file("config.json", MINIMAL);
  cfg_t c;
  cfg_report_t r;
  TEST_ASSERT_TRUE(cfg_set(dir, "sources.nmea2000.enabled", "true", &c, &r));
  TEST_ASSERT_TRUE(c.n2k.enabled);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_utc_offset_default_is_zero);
  RUN_TEST(test_utc_offset_loads_and_clamps);
  RUN_TEST(test_utc_offset_saves_and_keeps_unknown_display_keys);
  RUN_TEST(test_minimal_file_loads);
  RUN_TEST(test_full_role_fields);
  RUN_TEST(test_missing_main_uses_backup);
  RUN_TEST(test_both_missing_uses_defaults);
  RUN_TEST(test_parse_error_reports_line_and_falls_back);
  RUN_TEST(test_unknown_role_skipped_others_load);
  RUN_TEST(test_no_valid_roles_uses_default_roles);
  RUN_TEST(test_unknown_keys_survive_save);
  RUN_TEST(test_save_rotates_backup);
  RUN_TEST(test_version_written_and_missing_treated_as_1);
  RUN_TEST(test_save_from_defaults_creates_file);
  RUN_TEST(test_unknown_unit_reported_and_defaulted);
  RUN_TEST(test_long_unit_value_not_truncated_into_a_valid_one);
  RUN_TEST(test_distance_and_depth_units);
  RUN_TEST(test_legacy_distance_ft_sets_depth);
  RUN_TEST(test_unknown_roles_survive_save);
  RUN_TEST(test_reordered_roles_keep_their_settings);
  RUN_TEST(test_cfg_set_string);
  RUN_TEST(test_cfg_set_number_and_bool);
  RUN_TEST(test_cfg_reset_after_swipe_defaults_false_and_parses);
  RUN_TEST(test_cfg_vessel_name_roundtrips);
  RUN_TEST(test_cfg_set_rejects_bad_number);
  RUN_TEST(test_cfg_set_rejects_invalid_value);
  RUN_TEST(test_cfg_set_refuses_a_path_through_a_non_object);
  RUN_TEST(test_cfg_set_reports_out_of_range_numbers);
  RUN_TEST(test_cfg_set_creates_missing_string_key);
  RUN_TEST(test_cfg_set_utc_offset_on_a_file_that_lacks_the_key);
  RUN_TEST(test_cfg_set_rejects_a_utc_offset_that_is_not_a_number);
  RUN_TEST(test_cfg_show_masks_secrets);
  RUN_TEST(test_power_primary_round_trip);
  RUN_TEST(test_power_roundtrip_all_kinds);
  RUN_TEST(test_power_two_banks_same_kind_ok);
  RUN_TEST(test_power_two_house_rejected);
  RUN_TEST(test_power_no_house_rejected);
  RUN_TEST(test_power_unknown_kind_rejected);
  RUN_TEST(test_power_dangling_flow_to_rejected);
  RUN_TEST(test_power_converter_needs_from_and_others_refuse_it);
  RUN_TEST(test_power_other_needs_name);
  RUN_TEST(test_power_duplicate_battery_ref_rejected);
  RUN_TEST(test_power_bad_ref_rejected);
  RUN_TEST(test_power_too_many_banks_rejected);
  RUN_TEST(test_power_two_flows_on_one_output_rejected);
  RUN_TEST(test_power_old_keys_message_and_flag);
  RUN_TEST(test_power_banks_without_flows_is_valid);
  RUN_TEST(test_rejected_banks_survive_a_save);
  RUN_TEST(test_cleared_banks_are_removed_on_save);
  RUN_TEST(test_no_banks_key_changes_nothing);
  RUN_TEST(test_anchor_plugin_default);
  RUN_TEST(test_anchor_plugin_roundtrip);
  RUN_TEST(test_anchor_plugin_unknown_reports_issue);
  RUN_TEST(test_cfg_set_anchor_plugin_rejects_unknown);
  RUN_TEST(test_navigation_role_known_wind_and_nav_not);
  RUN_TEST(test_nmea2000_defaults_off);
  RUN_TEST(test_nmea2000_parsed);
  RUN_TEST(test_nmea2000_bad_mode_reports_and_stays_fallback);
  RUN_TEST(test_nmea2000_too_many_ids_reports);
  RUN_TEST(test_nmea2000_survives_a_save);
  RUN_TEST(test_nmea2000_not_added_to_a_file_without_it);
  RUN_TEST(test_cfg_set_enables_nmea2000);
  return UNITY_END();
}
