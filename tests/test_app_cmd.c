// Console commands, run as the console task would: from a thread other than the UI task,
// while a ticker thread plays the UI task (LVGL is only touched there).
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "app.h"
#include "app_can.h"
#include "app_cmd.h"
#include "platform.h"
#include "unity.h"

static uint8_t buf[800 * 48 * 2];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  lv_display_flush_ready(d);
}
static uint32_t tick_ms(void) { return (uint32_t)platform_millis(); }
static lv_display_t *disp;
static char dir[64];

static char out[16384];
static void collect(const char *line) {
  strncat(out, line, sizeof out - strlen(out) - 2);
  strcat(out, "\n");
}

static volatile bool ticking;
static void *ticker(void *arg) {
  (void)arg;
  while (ticking) {
    app_tick();
    usleep(5000);
  }
  return NULL;
}

// Runs one command line ("config set a.b c") with the UI task ticking alongside.
static int run(const char *line) {
  char copy[512], *argv[8];
  int argc = 0;
  snprintf(copy, sizeof copy, "%s", line);
  for (char *t = strtok(copy, " "); t && argc < 8; t = strtok(NULL, " ")) argv[argc++] = t;
  out[0] = 0;
  pthread_t th;
  ticking = true;
  pthread_create(&th, NULL, ticker, NULL);
  int rc = app_cmd_run(argc, argv, collect);
  ticking = false;
  pthread_join(th, NULL);
  return rc;
}

static void write_config(const char *text) {
  char p[128];
  snprintf(p, sizeof p, "%s/config.json", dir);
  FILE *f = fopen(p, "w");
  fputs(text, f);
  fclose(f);
}

void setUp(void) {
  strcpy(dir, "/tmp/bi_cmd_XXXXXX");
  TEST_ASSERT_NOT_NULL(mkdtemp(dir));
  write_config("{ \"wifi\": {\"ssid\": \"Boatnet\", \"password\": \"hunter22\"},"
               "  \"sources\": {\"signalk\": {\"host\": \"10.0.0.5\", \"token\": \"\"}},"
               "  \"display\": {\"brightness\": 80},"
               "  \"roles\": [ {\"role\": \"engine\"}, {\"role\": \"tanks\"} ] }");
  cfg_report_t rep;
  app_init(dir, &rep);
  app_set_info("sim800", "test");
  app_bind();
  app_start_ui(disp, SIZE_WIDE);
}

void tearDown(void) {
  char cmd[96];
  snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
  system(cmd);
}

static void test_help_lists_commands(void) {
  TEST_ASSERT_EQUAL_INT(0, run("help"));
  const char *want[] = {"status", "data <prefix>", "config show", "config set", "config reload", "sk request-access", "reboot"};
  for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, want[i]), want[i]);
}

static void test_status_has_all_keys(void) {
  TEST_ASSERT_EQUAL_INT(0, run("status"));
  const char *keys[] = {"firmware: test", "board: sim800", "ip:", "rssi:", "signalk:", "token: not set",
                        "access:", "ram:", "largest", "lowest", "stacks:", "sd: mounted", "config: main", "alerts:"};
  for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, keys[i]), keys[i]);
}

static bool no_frames(n2k_frame_t *f, void *ctx) { (void)f; (void)ctx; return false; }
static bool no_send(const n2k_frame_t *f, void *ctx) { (void)f; (void)ctx; return false; }
static n2k_can_state_t can_ok(void *ctx) { (void)ctx; return N2K_CAN_OK; }
static void no_recover(void *ctx) { (void)ctx; }

static void test_status_says_can_off_when_the_bus_is_not_started(void) {
  app_can_stop();
  TEST_ASSERT_EQUAL_INT(0, run("status"));
  TEST_ASSERT_NOT_NULL(strstr(out, "can: off"));
}

static void test_status_reports_the_bus_when_started(void) {
  cfg_t cfg;
  cfg_defaults(&cfg);
  cfg.n2k.enabled = true;
  app_can_stop();
  TEST_ASSERT_TRUE(app_can_start(&cfg, (n2k_can_ops_t){no_frames, no_send, can_ok, no_recover, NULL}));
  TEST_ASSERT_EQUAL_INT(0, run("status"));
  TEST_ASSERT_NOT_NULL(strstr(out, "can: connecting rx=0 values=0 unmapped=0 bus_off=0"));
  app_can_stop();
}

static void test_config_show_masks(void) {
  TEST_ASSERT_EQUAL_INT(0, run("config show"));
  TEST_ASSERT_NOT_NULL(strstr(out, "Boatnet"));
  TEST_ASSERT_NULL(strstr(out, "hunter22"));
  TEST_ASSERT_NOT_NULL(strstr(out, "set (8 chars)"));
}

static void test_config_set_token_applies_signalk(void) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, run("config set sources.signalk.token abc123"), out);
  TEST_ASSERT_NOT_NULL(strstr(out, "saved; applied: reconnect signalk"));
  run("status");
  TEST_ASSERT_NOT_NULL(strstr(out, "token: set"));
  run("config show");
  TEST_ASSERT_NULL(strstr(out, "abc123"));
}

// Review Focus 2: a bad value is refused, nothing changes, and the panel keeps running.
static void test_config_set_invalid_reports(void) {
  TEST_ASSERT_EQUAL_INT(1, run("config set display.brightness abc"));
  TEST_ASSERT_NOT_NULL(strstr(out, "display.brightness"));
  TEST_ASSERT_NOT_NULL(strstr(out, "not a number"));
  TEST_ASSERT_EQUAL_INT(1, run("config set units.temperature K"));
  TEST_ASSERT_NOT_NULL(strstr(out, "unknown unit"));
  TEST_ASSERT_EQUAL_UINT8(80, app_config()->brightness);
}

static void test_config_set_display_applies(void) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, run("config set display.palette night"), out);
  TEST_ASSERT_NOT_NULL(strstr(out, "applied: display"));
}

static void test_config_reload(void) {
  write_config("{ \"wifi\": {\"ssid\": \"Boatnet\"}, \"display\": {\"palette\": \"dusk\"},"
               "  \"roles\": [ {\"role\": \"engine\"} ] }");
  TEST_ASSERT_EQUAL_INT(0, run("config reload"));
  TEST_ASSERT_NOT_NULL(strstr(out, "reloaded"));
  TEST_ASSERT_EQUAL_STRING("dusk", app_config()->palette);
  TEST_ASSERT_EQUAL_UINT8(1, app_config()->role_count);
}

static void test_request_access_needs_signalk(void) {
  TEST_ASSERT_EQUAL_INT(1, run("sk request-access"));   // this test never starts the network
  TEST_ASSERT_NOT_NULL(strstr(out, "Signal K is not running"));
  run("status");
  TEST_ASSERT_NOT_NULL(strstr(out, "access: idle"));
}

static void test_data_lists_paths_under_a_prefix_with_their_state(void) {
  bd_id_t ok = bd_intern("electrical.resolved.batteries.abc12345.voltage");
  bd_id_t bad = bd_intern("electrical.resolved.solar.def67890.current");
  bd_id_t txt = bd_intern("electrical.resolved.solar.def67890.label");
  bd_intern("propulsion.port.revolutions");
  bd_set_number(ok, 13.25, BD_SRC_ELECTRICAL, platform_millis());
  bd_set_number(bad, 1.0, BD_SRC_ELECTRICAL, platform_millis());
  bd_set_invalid(bad);
  bd_set_text(txt, "BlueSolar f5", BD_SRC_ELECTRICAL, platform_millis());
  TEST_ASSERT_EQUAL_INT(0, run("data electrical.resolved"));
  TEST_ASSERT_NOT_NULL(strstr(out, "electrical.resolved.batteries.abc12345.voltage = 13.25"));
  TEST_ASSERT_NOT_NULL(strstr(out, "electrical.resolved.solar.def67890.current = INVALID"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"BlueSolar f5\""));
  TEST_ASSERT_NULL(strstr(out, "propulsion"));   // only the prefix asked for
  TEST_ASSERT_NOT_NULL(strstr(out, "3 path"));
}

static void test_data_says_so_when_nothing_matches_and_needs_a_prefix(void) {
  TEST_ASSERT_EQUAL_INT(0, run("data nothing.here"));
  TEST_ASSERT_NOT_NULL(strstr(out, "no paths"));
  TEST_ASSERT_NOT_EQUAL(0, run("data"));
  TEST_ASSERT_NOT_NULL(strstr(out, "usage"));
}

static void test_unknown_command_usage(void) {
  TEST_ASSERT_EQUAL_INT(1, run("frobnicate"));
  TEST_ASSERT_NOT_NULL(strstr(out, "help"));
  TEST_ASSERT_EQUAL_INT(1, run("config set onlypath"));
  TEST_ASSERT_NOT_NULL(strstr(out, "usage: config set <path> <value>"));
}

int main(void) {
  lv_init();
  lv_tick_set_cb(tick_ms);
  disp = lv_display_create(800, 480);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  UNITY_BEGIN();
  RUN_TEST(test_help_lists_commands);
  RUN_TEST(test_status_has_all_keys);
  RUN_TEST(test_status_says_can_off_when_the_bus_is_not_started);
  RUN_TEST(test_status_reports_the_bus_when_started);
  RUN_TEST(test_config_show_masks);
  RUN_TEST(test_config_set_token_applies_signalk);
  RUN_TEST(test_config_set_invalid_reports);
  RUN_TEST(test_config_set_display_applies);
  RUN_TEST(test_config_reload);
  RUN_TEST(test_request_access_needs_signalk);
  RUN_TEST(test_data_lists_paths_under_a_prefix_with_their_state);
  RUN_TEST(test_data_says_so_when_nothing_matches_and_needs_a_prefix);
  RUN_TEST(test_unknown_command_usage);
  return UNITY_END();
}
