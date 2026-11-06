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

// ---- multi-bank power: banks and links ----

static const char *BANKS_OK =
  "{ \"roles\": [ { \"role\": \"power\", \"primary\": \"279\", \"x-note\": \"keep\",\n"
  "  \"banks\": [ {\"id\":\"279\",\"name\":\"HOUSE\",\"kind\":\"house\"},\n"
  "               {\"id\":\"280\",\"name\":\"START\",\"kind\":\"start\"},\n"
  "               {\"id\":\"281\",\"name\":\"NAV\",\"kind\":\"nav\"} ],\n"
  "  \"links\": [ {\"src\":\"charger:276\",\"output\":0,\"to\":\"house\"},\n"
  "               {\"src\":\"charger:276\",\"output\":1,\"to\":\"start\"},\n"
  "               {\"src\":\"alternator:0\",\"to\":\"start\"},\n"
  "               {\"src\":\"solar:288\",\"to\":\"house\"},\n"
  "               {\"src\":\"dcdc:1\",\"from\":\"start\",\"to\":\"house\"},\n"
  "               {\"src\":\"dcdc:2\",\"from\":\"house\",\"to\":\"nav\"} ] } ] }\n";

static void test_power_banks_round_trip(void) {
  write_file("config.json", BANKS_OK);
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(0, r.count);
  const cfg_role_t *p = &c.roles[0];
  TEST_ASSERT_EQUAL_UINT8(3, p->bank_count);
  TEST_ASSERT_EQUAL_UINT8(6, p->link_count);
  TEST_ASSERT_EQUAL_STRING("280", p->bank[1].id);
  TEST_ASSERT_EQUAL_STRING("START", p->bank[1].name);
  TEST_ASSERT_EQUAL_STRING("nav", p->bank[2].kind);
  TEST_ASSERT_EQUAL_STRING("charger:276", p->link[1].src);
  TEST_ASSERT_EQUAL_UINT8(1, p->link[1].output);
  TEST_ASSERT_EQUAL_STRING("start", p->link[4].from);
  TEST_ASSERT_EQUAL_STRING("house", p->link[4].to);
  TEST_ASSERT_TRUE(cfg_save(dir, &c));
  cfg_t d;
  cfg_load(dir, &d, &r);
  TEST_ASSERT_EQUAL_UINT8(0, r.count);
  TEST_ASSERT_EQUAL_UINT8(3, d.roles[0].bank_count);
  TEST_ASSERT_EQUAL_UINT8(6, d.roles[0].link_count);
  TEST_ASSERT_EQUAL_STRING("dcdc:2", d.roles[0].link[5].src);
  TEST_ASSERT_EQUAL_STRING("house", d.roles[0].link[5].from);
  TEST_ASSERT_NOT_NULL(strstr(read_file("config.json"), "x-note"));
}

// A bad topology is reported once and dropped; the rest of the role (primary) still loads.
static void expect_banks_rejected(const char *banks_and_links) {
  char text[2048];
  snprintf(text, sizeof text, "{ \"roles\": [ { \"role\": \"power\", \"primary\": \"279\", %s } ] }", banks_and_links);
  write_file("config.json", text);
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(1, r.count);
  TEST_ASSERT_EQUAL_STRING("roles.power.banks", r.issues[0].key_path);
  TEST_ASSERT_EQUAL_UINT8(0, c.roles[0].bank_count);
  TEST_ASSERT_EQUAL_UINT8(0, c.roles[0].link_count);
  TEST_ASSERT_EQUAL_STRING("279", c.roles[0].primary);
}

static void test_power_banks_two_house_rejected(void) {
  expect_banks_rejected("\"banks\": [ {\"id\":\"1\",\"kind\":\"house\"}, {\"id\":\"2\",\"kind\":\"house\"} ]");
}
static void test_power_banks_no_house_rejected(void) {
  expect_banks_rejected("\"banks\": [ {\"id\":\"1\",\"kind\":\"start\"} ]");
}
static void test_power_banks_unknown_kind_rejected(void) {
  expect_banks_rejected("\"banks\": [ {\"id\":\"1\",\"kind\":\"house\"}, {\"id\":\"2\",\"kind\":\"galley\"} ]");
}
static void test_power_banks_link_to_missing_kind_rejected(void) {
  expect_banks_rejected("\"banks\": [ {\"id\":\"1\",\"kind\":\"house\"} ],"
                        " \"links\": [ {\"src\":\"solar:2\",\"to\":\"nav\"} ]");
}
static void test_power_dcdc_without_from_rejected(void) {
  expect_banks_rejected("\"banks\": [ {\"id\":\"1\",\"kind\":\"house\"} ],"
                        " \"links\": [ {\"src\":\"dcdc:1\",\"to\":\"house\"} ]");
}
static void test_power_non_dcdc_with_from_rejected(void) {
  expect_banks_rejected("\"banks\": [ {\"id\":\"1\",\"kind\":\"house\"} ],"
                        " \"links\": [ {\"src\":\"solar:2\",\"from\":\"house\",\"to\":\"house\"} ]");
}
static void test_power_link_bad_src_rejected(void) {
  expect_banks_rejected("\"banks\": [ {\"id\":\"1\",\"kind\":\"house\"} ],"
                        " \"links\": [ {\"src\":\"toaster:2\",\"to\":\"house\"} ]");
}

static void test_power_banks_without_links_is_valid(void) {
  write_file("config.json", "{ \"roles\": [ { \"role\": \"power\", \"banks\": [ {\"id\":\"1\",\"kind\":\"house\"} ] } ] }");
  cfg_t c;
  cfg_report_t r;
  cfg_load(dir, &c, &r);
  TEST_ASSERT_EQUAL_UINT8(0, r.count);
  TEST_ASSERT_EQUAL_UINT8(1, c.roles[0].bank_count);
  TEST_ASSERT_EQUAL_UINT8(0, c.roles[0].link_count);
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
  RUN_TEST(test_cfg_set_rejects_bad_number);
  RUN_TEST(test_cfg_set_rejects_invalid_value);
  RUN_TEST(test_cfg_set_creates_missing_string_key);
  RUN_TEST(test_cfg_set_utc_offset_on_a_file_that_lacks_the_key);
  RUN_TEST(test_cfg_set_rejects_a_utc_offset_that_is_not_a_number);
  RUN_TEST(test_cfg_show_masks_secrets);
  RUN_TEST(test_power_primary_round_trip);
  RUN_TEST(test_power_banks_round_trip);
  RUN_TEST(test_power_banks_two_house_rejected);
  RUN_TEST(test_power_banks_no_house_rejected);
  RUN_TEST(test_power_banks_unknown_kind_rejected);
  RUN_TEST(test_power_banks_link_to_missing_kind_rejected);
  RUN_TEST(test_power_dcdc_without_from_rejected);
  RUN_TEST(test_power_non_dcdc_with_from_rejected);
  RUN_TEST(test_power_link_bad_src_rejected);
  RUN_TEST(test_power_banks_without_links_is_valid);
  RUN_TEST(test_rejected_banks_survive_a_save);
  RUN_TEST(test_no_banks_key_changes_nothing);
  RUN_TEST(test_anchor_plugin_default);
  RUN_TEST(test_anchor_plugin_roundtrip);
  RUN_TEST(test_anchor_plugin_unknown_reports_issue);
  RUN_TEST(test_cfg_set_anchor_plugin_rejects_unknown);
  RUN_TEST(test_navigation_role_known_wind_and_nav_not);
  return UNITY_END();
}
