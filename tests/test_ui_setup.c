// Setup screens: frame, menu, Display, About. Headless display; a fake LVGL clock.
#include <string.h>
#include "alerts.h"
#include "config.h"
#include "ui_alerts.h"
#include "ui_quick.h"
#include "ui_setup.h"
#include "ui_shell.h"
#include "unity.h"

static uint8_t buf[800 * 48 * 2];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  lv_display_flush_ready(d);
}
static uint32_t now_ms;
static uint32_t fake_tick(void) { return now_ms; }
static lv_obj_t *scr;
static cfg_t cfg;

static cfg_t saved;
static int saves;
static void on_save(const cfg_t *c) {
  saved = *c;
  saves++;
}
static void on_about(char *out, size_t n) { snprintf(out, n, "firmware: test\nboard: sim800"); }
static uint8_t on_discovery(ui_disc_role_t *out, uint8_t max) {
  (void)max;
  memset(out, 0, sizeof *out);
  strcpy(out[0].role, "engine");
  strcpy(out[0].ids[0], "port");
  strcpy(out[0].ids[1], "starboard");
  strcpy(out[0].ids[2], "wing");
  out[0].count = 3;
  return 1;
}
static int rediscoveries;
static void on_run_discovery(void) { rediscoveries++; }
static const char *const implemented[] = {"engine", "power", "tanks", NULL};
static ui_setup_cb_t cb = {.save = on_save, .about = on_about, .has_buzzer = true, .has_sd = true, .dims = true,
                           .discovery = on_discovery, .run_discovery = on_run_discovery, .roles_available = implemented};

static void click(const char *part) {
  lv_obj_t *o = ui_setup_part(part);
  TEST_ASSERT_NOT_NULL_MESSAGE(o, part);
  lv_obj_send_event(o, LV_EVENT_CLICKED, NULL);
  lv_timer_handler();
}

static void build_page(lv_obj_t *area, uint8_t page, size_class_t sc, void *ctx) {
  (void)area; (void)page; (void)sc; (void)ctx;
}
static const ui_role_view_t views[] = {{"ENGINE", 1, build_page, NULL, NULL}, {"TANKS", 1, build_page, NULL, NULL}};
static int8_t role_of(const char *id) { (void)id; return 0; }

void setUp(void) {
  now_ms = 1000;
  ui_setup_close();
  ui_quick_close();
  lv_timer_handler();
  cfg_defaults(&cfg);
  saves = 0;
  rediscoveries = 0;
  cb.has_buzzer = true;
  cb.has_sd = true;
}
void tearDown(void) {}

static void open_setup(void) {
  ui_setup_open(scr, &cfg, SIZE_WIDE, &cb);
  TEST_ASSERT_TRUE(ui_setup_is_open());
  TEST_ASSERT_EQUAL(SETUP_MENU, ui_setup_screen());
}

static int setup_from_quick;
static void quick_setup(void) { setup_from_quick++; }

static void test_setup_opens_from_quick_panel(void) {
  static const ui_quick_cb_t qcb = {NULL, NULL, quick_setup};
  setup_from_quick = 0;
  ui_quick_open(scr, "day", 80, true, &qcb);
  lv_obj_t *b = ui_quick_part(UI_QUICK_SETUP);
  TEST_ASSERT_NOT_NULL(b);
  lv_obj_send_event(b, LV_EVENT_CLICKED, NULL);
  TEST_ASSERT_EQUAL_INT(1, setup_from_quick);
}

static void test_menu_opens_display_and_about(void) {
  open_setup();
  click("menu_about");
  TEST_ASSERT_EQUAL(SETUP_ABOUT, ui_setup_screen());
  TEST_ASSERT_NOT_NULL(strstr(lv_label_get_text(ui_setup_part("about_text")), "board: sim800"));
  click("back");
  TEST_ASSERT_EQUAL(SETUP_MENU, ui_setup_screen());
  click("menu_display");
  TEST_ASSERT_EQUAL(SETUP_DISPLAY, ui_setup_screen());
}

static void test_back_with_changes_asks(void) {
  open_setup();
  click("menu_display");
  click("unit_temperature_F");
  TEST_ASSERT_TRUE(ui_setup_dirty());
  TEST_ASSERT_EQUAL_STRING("F", ui_setup_working()->units.temperature);
  click("back");
  TEST_ASSERT_EQUAL(SETUP_DISPLAY, ui_setup_screen());   // asks first
  click("discard_no");
  TEST_ASSERT_EQUAL(SETUP_DISPLAY, ui_setup_screen());
  TEST_ASSERT_TRUE(ui_setup_dirty());
  click("back");
  click("discard_yes");
  TEST_ASSERT_EQUAL(SETUP_MENU, ui_setup_screen());
  TEST_ASSERT_FALSE(ui_setup_dirty());
  TEST_ASSERT_EQUAL_STRING("C", ui_setup_working()->units.temperature);
  TEST_ASSERT_EQUAL_INT(0, saves);
}

static void test_display_save_calls_save_with_units(void) {
  open_setup();
  click("menu_display");
  click("unit_temperature_F");
  click("unit_pressure_psi");
  click("palette_night");
  click("save");
  TEST_ASSERT_EQUAL_INT(1, saves);
  TEST_ASSERT_EQUAL_STRING("F", saved.units.temperature);
  TEST_ASSERT_EQUAL_STRING("psi", saved.units.pressure);
  TEST_ASSERT_EQUAL_STRING("night", saved.palette);
  TEST_ASSERT_FALSE(ui_setup_dirty());
  TEST_ASSERT_EQUAL(SETUP_MENU, ui_setup_screen());
}

static void test_inactivity_closes(void) {
  open_setup();
  click("menu_display");
  click("unit_temperature_F");
  now_ms += SETUP_IDLE_MS - 1;
  ui_setup_tick();
  TEST_ASSERT_TRUE(ui_setup_is_open());
  now_ms += 2;
  ui_setup_tick();
  TEST_ASSERT_FALSE(ui_setup_is_open());
  TEST_ASSERT_EQUAL_INT(0, saves);   // unsaved changes are discarded
}

static void raise_alarm(void) {
  alerts_source_up(true);
  alert_t al = {.state = AL_ALARM, .audible = true};
  strcpy(al.id, "propulsion.port.temperature");
  strcpy(al.message, "Port coolant high");
  alerts_upsert(&al, 0);
  static alerts_snapshot_t s;
  alerts_snapshot(&s);
  ui_alerts_update(&s, 0);
  ui_alerts_update(&s, 2000);   // past the tap guard
  lv_timer_handler();
}

static void test_banner_stays_above_setup(void) {
  open_setup();
  raise_alarm();
  lv_obj_t *banner = ui_alerts_part(UI_ALERTS_BANNER), *root = ui_setup_part("root");
  TEST_ASSERT_TRUE(lv_obj_get_index(banner) > lv_obj_get_index(root));
  alerts_source_up(false);
}

static void test_go_closes_setup(void) {
  open_setup();
  raise_alarm();
  lv_obj_send_event(ui_alerts_part(UI_ALERTS_GO), LV_EVENT_CLICKED, NULL);
  lv_timer_handler();
  TEST_ASSERT_FALSE(ui_setup_is_open());
  alerts_source_up(false);
}

// Review Focus 4: a rebind rebuilds the pages; setup stays open on top.
static void test_setup_survives_shell_rebuild(void) {
  open_setup();
  click("menu_display");
  ui_shell_rebuild();
  lv_timer_handler();
  TEST_ASSERT_TRUE(ui_setup_is_open());
  TEST_ASSERT_EQUAL(SETUP_DISPLAY, ui_setup_screen());
}

static void test_buzzer_hidden_without_buzzer(void) {
  cb.has_buzzer = false;
  open_setup();
  click("menu_display");
  TEST_ASSERT_NULL(ui_setup_part("buzzer"));
  ui_setup_close();
  lv_timer_handler();
  cb.has_buzzer = true;
  open_setup();
  click("menu_display");
  TEST_ASSERT_NOT_NULL(ui_setup_part("buzzer"));
}

static void test_no_sd_note(void) {
  cb.has_sd = false;
  open_setup();
  click("menu_display");
  TEST_ASSERT_NOT_NULL(ui_setup_part("nosd"));
}

static int role_index(const cfg_t *c, const char *name) {
  for (int i = 0; i < c->role_count; i++)
    if (!strcmp(c->roles[i].role, name)) return i;
  return -1;
}

static void test_roles_reorder_and_save(void) {
  open_setup();
  click("menu_roles");
  TEST_ASSERT_EQUAL(SETUP_ROLES, ui_setup_screen());
  click("down_engine");
  TEST_ASSERT_EQUAL_STRING("power", ui_setup_working()->roles[0].role);
  TEST_ASSERT_EQUAL_STRING("engine", ui_setup_working()->roles[1].role);
  click("save");
  TEST_ASSERT_EQUAL_INT(1, saves);
  TEST_ASSERT_EQUAL_STRING("power", saved.roles[0].role);
}

static void test_add_offers_missing_roles_only(void) {
  open_setup();
  click("menu_roles");
  TEST_ASSERT_NULL(ui_setup_part("add_tanks"));   // already there
  click("remove_tanks");
  TEST_ASSERT_EQUAL_INT(-1, role_index(ui_setup_working(), "tanks"));
  TEST_ASSERT_NULL(ui_setup_part("add_engine"));
  click("add_tanks");
  TEST_ASSERT_TRUE(role_index(ui_setup_working(), "tanks") >= 0);
  TEST_ASSERT_EQUAL_UINT8(0, ui_setup_working()->roles[role_index(ui_setup_working(), "tanks")].src_entry);
}

static void test_discovery_confirm_writes_pins_and_excludes(void) {
  open_setup();
  click("menu_discovery");
  click("run");
  TEST_ASSERT_EQUAL_INT(1, rediscoveries);
  click("keep_engine_wing");   // keep -> skip
  click("confirm_engine");
  const cfg_role_t *r = &ui_setup_working()->roles[role_index(ui_setup_working(), "engine")];
  TEST_ASSERT_EQUAL_UINT8(2, r->inst_count);
  TEST_ASSERT_EQUAL_STRING("port", r->inst[0].id);
  TEST_ASSERT_EQUAL_STRING("starboard", r->inst[1].id);
  TEST_ASSERT_EQUAL_UINT8(1, r->exclude_count);
  TEST_ASSERT_EQUAL_STRING("wing", r->exclude[0]);
}

static void test_discovery_missing_shown(void) {
  cfg_role_t *e = &cfg.roles[role_index(&cfg, "engine")];
  strcpy(e->inst[0].id, "port");
  strcpy(e->inst[1].id, "center");
  e->inst_count = 2;
  open_setup();
  click("menu_discovery");
  TEST_ASSERT_NOT_NULL(ui_setup_part("missing_engine_center"));
  TEST_ASSERT_NULL(ui_setup_part("missing_engine_port"));
}

static void test_auto_clears_pins(void) {
  cfg_role_t *e = &cfg.roles[role_index(&cfg, "engine")];
  strcpy(e->inst[0].id, "port");
  e->inst_count = 1;
  strcpy(e->exclude[0], "wing");
  e->exclude_count = 1;
  open_setup();
  click("menu_discovery");
  click("auto_engine");
  const cfg_role_t *r = &ui_setup_working()->roles[role_index(ui_setup_working(), "engine")];
  TEST_ASSERT_EQUAL_UINT8(0, r->inst_count);
  TEST_ASSERT_EQUAL_UINT8(0, r->exclude_count);
}

int main(void) {
  lv_init();
  lv_tick_set_cb(fake_tick);
  lv_display_t *disp = lv_display_create(800, 480);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  alerts_init();
  ui_shell_create(disp, SIZE_WIDE, PALETTE_DAY, views, 2, 0);
  scr = lv_display_get_screen_active(disp);
  ui_alerts_attach(scr, ui_shell_strip(), SIZE_WIDE, role_of);
  UNITY_BEGIN();
  RUN_TEST(test_setup_opens_from_quick_panel);
  RUN_TEST(test_menu_opens_display_and_about);
  RUN_TEST(test_back_with_changes_asks);
  RUN_TEST(test_display_save_calls_save_with_units);
  RUN_TEST(test_inactivity_closes);
  RUN_TEST(test_banner_stays_above_setup);
  RUN_TEST(test_go_closes_setup);
  RUN_TEST(test_setup_survives_shell_rebuild);
  RUN_TEST(test_buzzer_hidden_without_buzzer);
  RUN_TEST(test_no_sd_note);
  RUN_TEST(test_roles_reorder_and_save);
  RUN_TEST(test_add_offers_missing_roles_only);
  RUN_TEST(test_discovery_confirm_writes_pins_and_excludes);
  RUN_TEST(test_discovery_missing_shown);
  RUN_TEST(test_auto_clears_pins);
  return UNITY_END();
}
