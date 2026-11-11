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
static bool disc_extra;   // discovery now also finds engine "center"
static uint8_t on_discovery(ui_disc_role_t *out, uint8_t max) {
  (void)max;
  memset(out, 0, sizeof *out);
  strcpy(out[0].role, "engine");
  strcpy(out[0].ids[0], "port");
  strcpy(out[0].ids[1], "starboard");
  strcpy(out[0].ids[2], "wing");
  out[0].count = 3;
  if (disc_extra) strcpy(out[0].ids[out[0].count++], "center");
  return 1;
}
static int rediscoveries;
static void on_run_discovery(void) { rediscoveries++; }
static const char *const implemented[] = {"engine", "power", "tanks", NULL};
static int scans, access_requests;
static void on_scan(void) { scans++; }
static void on_request_access(void) { access_requests++; }
static void on_line(char *out, size_t n) { snprintf(out, n, "state line"); }
static ui_setup_cb_t cb = {.save = on_save, .about = on_about, .has_buzzer = true, .has_sd = true, .dims = true,
                           .discovery = on_discovery, .run_discovery = on_run_discovery, .roles_available = implemented,
                           .wifi_scan = on_scan, .wifi_state = on_line, .sk_state = on_line,
                           .request_access = on_request_access, .access_state = on_line};

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
  scans = access_requests = 0;
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
  static const ui_quick_cb_t qcb = {NULL, NULL, quick_setup, NULL, NULL, NULL, NULL, NULL, NULL};
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

static void test_display_time_zone_row_steps_by_half_hours(void) {
  open_setup();
  click("menu_display");
  TEST_ASSERT_NOT_NULL(ui_setup_part("tz_value"));
  click("tz_plus");
  click("tz_plus");
  TEST_ASSERT_NOT_NULL(strstr(lv_label_get_text(ui_setup_part("tz_value")), "UTC+01:00"));
  click("tz_minus");
  click("tz_minus");
  click("tz_minus");
  TEST_ASSERT_NOT_NULL(strstr(lv_label_get_text(ui_setup_part("tz_value")), "UTC-00:30"));
  click("tz_plus");
  click("tz_plus");
  click("tz_plus");
  click("save");
  TEST_ASSERT_EQUAL_INT16(60, saved.utc_offset_min);
}

static void test_time_zone_stops_at_the_limits(void) {
  open_setup();
  click("menu_display");
  for (int i = 0; i < 60; i++) click("tz_minus");
  TEST_ASSERT_EQUAL_INT16(-720, ui_setup_working()->utc_offset_min);
  for (int i = 0; i < 80; i++) click("tz_plus");
  TEST_ASSERT_EQUAL_INT16(840, ui_setup_working()->utc_offset_min);
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
  TEST_ASSERT_EQUAL_STRING("glance", ui_setup_working()->roles[0].role);   // the default list starts with the glance page
  TEST_ASSERT_EQUAL_STRING("power", ui_setup_working()->roles[1].role);
  TEST_ASSERT_EQUAL_STRING("engine", ui_setup_working()->roles[2].role);
  click("save");
  TEST_ASSERT_EQUAL_INT(1, saves);
  TEST_ASSERT_EQUAL_STRING("power", saved.roles[1].role);
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

static void test_roles_anchor_plugin_row(void) {
  open_setup();
  click("menu_roles");
  TEST_ASSERT_EQUAL_STRING("hoekens-anchor-alarm", ui_setup_working()->anchor_plugin);
  click("plugin_anchoralarm");
  TEST_ASSERT_EQUAL_STRING("anchoralarm", ui_setup_working()->anchor_plugin);
  click("plugin_hoekens-anchor-alarm");
  TEST_ASSERT_EQUAL_STRING("hoekens-anchor-alarm", ui_setup_working()->anchor_plugin);
  click("plugin_anchoralarm");
  click("save");
  TEST_ASSERT_EQUAL_STRING("anchoralarm", saved.anchor_plugin);
}

static void test_roles_anchor_plugin_row_needs_the_anchor_role(void) {
  open_setup();
  click("menu_roles");
  click("remove_anchor");
  TEST_ASSERT_NULL(ui_setup_part("plugin_anchoralarm"));
}

static void test_discovery_confirm_writes_pins_and_excludes(void) {
  open_setup();
  click("menu_discovery");
  click("run");
  TEST_ASSERT_EQUAL_INT(1, rediscoveries);
  lv_obj_t *sw = ui_setup_part("keep_engine_wing");
  TEST_ASSERT_TRUE(lv_obj_check_type(sw, &lv_switch_class));
  TEST_ASSERT_TRUE(lv_obj_has_state(sw, LV_STATE_CHECKED));   // on: shown
  lv_obj_remove_state(sw, LV_STATE_CHECKED);                   // what a touch on the switch does
  lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, NULL);
  click("confirm_engine");
  const cfg_role_t *r = &ui_setup_working()->roles[role_index(ui_setup_working(), "engine")];
  TEST_ASSERT_EQUAL_UINT8(2, r->inst_count);
  TEST_ASSERT_EQUAL_STRING("port", r->inst[0].id);
  TEST_ASSERT_EQUAL_STRING("starboard", r->inst[1].id);
  TEST_ASSERT_EQUAL_UINT8(1, r->exclude_count);
  TEST_ASSERT_EQUAL_STRING("wing", r->exclude[0]);
}

// Discovery re-reads the tree every 20 s. A refresh must not throw away switches the user has flipped
// but not confirmed yet.
static void flip(const char *part) {
  lv_obj_t *sw = ui_setup_part(part);
  TEST_ASSERT_NOT_NULL_MESSAGE(sw, part);
  if (lv_obj_has_state(sw, LV_STATE_CHECKED)) lv_obj_remove_state(sw, LV_STATE_CHECKED);
  else lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, NULL);
}

static void test_discovery_refresh_with_nothing_new_leaves_the_screen_alone(void) {
  open_setup();
  click("menu_discovery");
  flip("keep_engine_wing");
  lv_obj_t *before = ui_setup_part("keep_engine_wing");
  ui_setup_discovery_changed();
  lv_timer_handler();
  TEST_ASSERT_EQUAL_PTR(before, ui_setup_part("keep_engine_wing"));   // not rebuilt
  TEST_ASSERT_FALSE(lv_obj_has_state(ui_setup_part("keep_engine_wing"), LV_STATE_CHECKED));
}

static void test_discovery_refresh_with_a_new_id_keeps_unconfirmed_choices(void) {
  open_setup();
  click("menu_discovery");
  flip("keep_engine_wing");
  TEST_ASSERT_NULL(ui_setup_part("keep_engine_center"));
  disc_extra = true;
  ui_setup_discovery_changed();
  lv_timer_handler();
  disc_extra = false;
  TEST_ASSERT_NOT_NULL(ui_setup_part("keep_engine_center"));
  TEST_ASSERT_FALSE(lv_obj_has_state(ui_setup_part("keep_engine_wing"), LV_STATE_CHECKED));   // still off
  TEST_ASSERT_TRUE(lv_obj_has_state(ui_setup_part("keep_engine_port"), LV_STATE_CHECKED));
  click("confirm_engine");
  const cfg_role_t *r = &ui_setup_working()->roles[role_index(ui_setup_working(), "engine")];
  TEST_ASSERT_EQUAL_UINT8(1, r->exclude_count);
  TEST_ASSERT_EQUAL_STRING("wing", r->exclude[0]);
}

static void test_discovery_confirm_keeps_other_roles_unconfirmed_choices(void) {
  open_setup();
  click("menu_discovery");
  flip("keep_engine_wing");
  click("confirm_engine");   // rebuilds the screen
  TEST_ASSERT_FALSE(lv_obj_has_state(ui_setup_part("keep_engine_wing"), LV_STATE_CHECKED));   // as confirmed
}

static void test_auto_after_flipping_resets_the_role(void) {
  open_setup();
  click("menu_discovery");
  flip("keep_engine_wing");
  click("auto_engine");
  TEST_ASSERT_TRUE(lv_obj_has_state(ui_setup_part("keep_engine_wing"), LV_STATE_CHECKED));
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

// Types text into the open keyboard and presses its OK key.
static void type_and_ok(const char *t) {
  lv_obj_t *ta = ui_setup_part("kb_text");
  TEST_ASSERT_NOT_NULL(ta);
  lv_textarea_set_text(ta, t);
  lv_obj_send_event(ui_setup_part("kb"), LV_EVENT_READY, NULL);
  lv_timer_handler();
}

static void test_wifi_scan_lists_sorted(void) {
  open_setup();
  click("menu_wifi");
  click("scan");
  TEST_ASSERT_EQUAL_INT(1, scans);
  ui_ap_t aps[] = {{"Far", -80, true}, {"Near", -50, true}};
  ui_setup_wifi_results(aps, 2);
  lv_timer_handler();
  lv_obj_t *near = ui_setup_part("ap_Near"), *far = ui_setup_part("ap_Far");
  TEST_ASSERT_NOT_NULL(near);
  TEST_ASSERT_NOT_NULL(far);
  TEST_ASSERT_TRUE(lv_obj_get_index(near) < lv_obj_get_index(far));   // strongest first
  TEST_ASSERT_NOT_NULL(ui_setup_part("ap_other"));
}

static void test_wifi_choose_then_password_saves_wifi_keys(void) {
  open_setup();
  click("menu_wifi");
  click("scan");
  ui_ap_t aps[] = {{"Boatnet", -50, true}};
  ui_setup_wifi_results(aps, 1);
  lv_timer_handler();
  click("ap_Boatnet");
  type_and_ok("secret12");
  TEST_ASSERT_EQUAL_STRING("Boatnet", ui_setup_working()->wifi.ssid);
  TEST_ASSERT_EQUAL_STRING("secret12", ui_setup_working()->wifi.password);
  click("save");
  TEST_ASSERT_EQUAL_STRING("Boatnet", saved.wifi.ssid);
  TEST_ASSERT_EQUAL_STRING("secret12", saved.wifi.password);
}

// "Other network": the name keyboard hands straight to the password keyboard with no rebuild
// between, so the first keyboard's textarea must not stay registered as "kb_text".
static void test_wifi_other_network_then_password(void) {
  open_setup();
  click("menu_wifi");
  click("scan");
  ui_ap_t aps[] = {{"Boatnet", -50, true}};
  ui_setup_wifi_results(aps, 1);
  lv_timer_handler();
  click("ap_other");
  lv_obj_t *first = ui_setup_part("kb_text");
  type_and_ok("Golden");
  TEST_ASSERT_FALSE(lv_obj_is_valid(first));   // the name keyboard is gone
  lv_obj_t *second = ui_setup_part("kb_text");
  TEST_ASSERT_NOT_NULL(second);
  TEST_ASSERT_TRUE(lv_obj_is_valid(second));   // "kb_text" is the password keyboard's field
  type_and_ok("secret12");
  TEST_ASSERT_EQUAL_STRING("Golden", ui_setup_working()->wifi.ssid);
  TEST_ASSERT_EQUAL_STRING("secret12", ui_setup_working()->wifi.password);
}

// True when the open keyboard's current page has a key with exactly this text.
static bool kb_has_key(const char *key) {
  const char *const *map = lv_keyboard_get_map_array(ui_setup_part("kb"));
  for (; map[0][0]; map++)
    if (!strcmp(map[0], key)) return true;
  return false;
}

// An IP address is typed without leaving the first page: digits, '.' and ':' together.
static void test_sk_address_pad_has_digits_and_dot_together(void) {
  open_setup();
  click("menu_signalk");
  click("sk_manual");   // host starts empty or "auto": no name to edit
  for (const char *k = "0123456789.:"; *k; k++) {
    char key[2] = {*k, 0};
    TEST_ASSERT_TRUE_MESSAGE(kb_has_key(key), key);
  }
  TEST_ASSERT_TRUE(kb_has_key("abc"));    // a way to the letters, for a hostname
  TEST_ASSERT_FALSE(kb_has_key("q"));
  type_and_ok("192.168.0.108");
  TEST_ASSERT_EQUAL_STRING("192.168.0.108", ui_setup_working()->sk.host);
  click("edit_host");   // an IP already set: still the pad
  TEST_ASSERT_TRUE(kb_has_key("."));
  TEST_ASSERT_FALSE(kb_has_key("q"));
}

static void test_sk_hostname_opens_on_letters(void) {
  open_setup();
  click("menu_signalk");
  click("sk_manual");
  type_and_ok("signalk.local");
  click("edit_host");
  TEST_ASSERT_TRUE(kb_has_key("q"));      // a name is edited with the letters
}

static void test_sk_manual_host_saves(void) {
  open_setup();
  click("menu_signalk");
  click("sk_manual");
  type_and_ok("10.0.0.5");
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", ui_setup_working()->sk.host);
  click("edit_port");
  type_and_ok("3443");
  TEST_ASSERT_EQUAL_UINT16(3443, ui_setup_working()->sk.port);
  click("save");
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", saved.sk.host);
  click("menu_signalk");
  click("sk_auto");
  TEST_ASSERT_EQUAL_STRING("auto", ui_setup_working()->sk.host);
}

static void test_request_access_button_calls_cb(void) {
  open_setup();
  click("menu_signalk");
  click("request_access");
  TEST_ASSERT_EQUAL_INT(1, access_requests);
  TEST_ASSERT_NOT_NULL(ui_setup_part("access_state"));
}

static void test_sources_page_picks_the_mode_and_saves_it(void) {
  open_setup();
  TEST_ASSERT_FALSE(ui_setup_working()->n2k.enabled);   // the default: Signal K only
  click("menu_sources");
  TEST_ASSERT_NOT_NULL(ui_setup_part("src_sk"));
  TEST_ASSERT_FALSE(ui_setup_dirty());
  click("src_both");
  TEST_ASSERT_TRUE(ui_setup_working()->n2k.enabled);
  TEST_ASSERT_FALSE(ui_setup_working()->n2k.only);
  TEST_ASSERT_NOT_NULL(strstr(lv_label_get_text(ui_setup_part("src_note")), "Signal K first"));
  click("src_bus");
  TEST_ASSERT_TRUE(ui_setup_working()->n2k.enabled);
  TEST_ASSERT_TRUE(ui_setup_working()->n2k.only);
  TEST_ASSERT_NOT_NULL(strstr(lv_label_get_text(ui_setup_part("src_note")), "no alarms"));
  TEST_ASSERT_TRUE(ui_setup_dirty());
  click("save");
  TEST_ASSERT_EQUAL_INT(1, saves);
  TEST_ASSERT_TRUE(saved.n2k.enabled && saved.n2k.only);
  click("menu_sources");
  click("src_sk");   // back to Signal K only: the bus is off and the stale "only" is cleared
  TEST_ASSERT_FALSE(ui_setup_working()->n2k.enabled);
  TEST_ASSERT_FALSE(ui_setup_working()->n2k.only);
}

// Review Focus 5: on the 4" screen the field being edited stays above the keyboard.
static void test_keyboard_keeps_field_visible_480(void) {
  static uint8_t b2[480 * 48 * 2];
  lv_display_t *d0 = lv_display_get_default();
  lv_display_t *sq = lv_display_create(480, 480);
  lv_display_set_flush_cb(sq, flush);
  lv_display_set_buffers(sq, b2, NULL, sizeof b2, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_default(sq);
  ui_setup_open(lv_display_get_screen_active(sq), &cfg, SIZE_SQUARE, &cb);
  click("menu_signalk");
  click("sk_manual");
  lv_obj_update_layout(lv_display_get_screen_active(sq));
  lv_area_t ta, kb;
  lv_obj_get_coords(ui_setup_part("kb_text"), &ta);
  lv_obj_get_coords(ui_setup_part("kb"), &kb);
  TEST_ASSERT_TRUE(ta.y2 < kb.y1);
  TEST_ASSERT_TRUE(ta.y1 >= 0);
  ui_setup_close();
  lv_timer_handler();
  lv_display_set_default(d0);
  lv_display_delete(sq);
}

static void test_discovery_label_edit(void) {
  open_setup();
  click("menu_discovery");
  click("label_engine_port");
  type_and_ok("MAIN");
  click("confirm_engine");
  const cfg_role_t *r = &ui_setup_working()->roles[role_index(ui_setup_working(), "engine")];
  TEST_ASSERT_EQUAL_STRING("port", r->inst[0].id);
  TEST_ASSERT_EQUAL_STRING("MAIN", r->inst[0].label);
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
  ui_alerts_attach(scr, ui_shell_strip(), SIZE_WIDE, role_of, NULL);
  UNITY_BEGIN();
  RUN_TEST(test_setup_opens_from_quick_panel);
  RUN_TEST(test_menu_opens_display_and_about);
  RUN_TEST(test_back_with_changes_asks);
  RUN_TEST(test_display_save_calls_save_with_units);
  RUN_TEST(test_display_time_zone_row_steps_by_half_hours);
  RUN_TEST(test_time_zone_stops_at_the_limits);
  RUN_TEST(test_inactivity_closes);
  RUN_TEST(test_banner_stays_above_setup);
  RUN_TEST(test_go_closes_setup);
  RUN_TEST(test_setup_survives_shell_rebuild);
  RUN_TEST(test_buzzer_hidden_without_buzzer);
  RUN_TEST(test_no_sd_note);
  RUN_TEST(test_roles_reorder_and_save);
  RUN_TEST(test_add_offers_missing_roles_only);
  RUN_TEST(test_roles_anchor_plugin_row);
  RUN_TEST(test_roles_anchor_plugin_row_needs_the_anchor_role);
  RUN_TEST(test_discovery_confirm_writes_pins_and_excludes);
  RUN_TEST(test_discovery_refresh_with_nothing_new_leaves_the_screen_alone);
  RUN_TEST(test_discovery_refresh_with_a_new_id_keeps_unconfirmed_choices);
  RUN_TEST(test_discovery_confirm_keeps_other_roles_unconfirmed_choices);
  RUN_TEST(test_auto_after_flipping_resets_the_role);
  RUN_TEST(test_discovery_missing_shown);
  RUN_TEST(test_auto_clears_pins);
  RUN_TEST(test_wifi_scan_lists_sorted);
  RUN_TEST(test_wifi_choose_then_password_saves_wifi_keys);
  RUN_TEST(test_wifi_other_network_then_password);
  RUN_TEST(test_sk_address_pad_has_digits_and_dot_together);
  RUN_TEST(test_sk_hostname_opens_on_letters);
  RUN_TEST(test_sk_manual_host_saves);
  RUN_TEST(test_sources_page_picks_the_mode_and_saves_it);
  RUN_TEST(test_request_access_button_calls_cb);
  RUN_TEST(test_keyboard_keeps_field_visible_480);
  RUN_TEST(test_discovery_label_edit);
  return UNITY_END();
}
