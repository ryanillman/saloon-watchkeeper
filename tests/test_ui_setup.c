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
static bool disc_power;   // ... and reports the power role too
static uint8_t on_discovery(ui_disc_role_t *out, uint8_t max) {
  (void)max;
  memset(out, 0, sizeof *out);
  strcpy(out[0].role, "engine");
  strcpy(out[0].ids[0], "port");
  strcpy(out[0].ids[1], "starboard");
  strcpy(out[0].ids[2], "wing");
  out[0].count = 3;
  if (disc_extra) strcpy(out[0].ids[out[0].count++], "center");
  if (disc_power) {
    memset(&out[1], 0, sizeof out[1]);
    strcpy(out[1].role, "power");
    strcpy(out[1].ids[0], "battery.x");
    out[1].count = 1;
    return 2;
  }
  return 1;
}
static void on_disc_name(const char *role, const char *id, char *out, size_t n) {
  snprintf(out, n, "%s", !strcmp(role, "engine") && !strcmp(id, "port") ? "Helm" : "");
}
static int rediscoveries;
static void on_run_discovery(void) { rediscoveries++; }
static const char *const implemented[] = {"engine", "power", "tanks", NULL};
static int scans, access_requests;
static void on_scan(void) { scans++; }
static void on_request_access(void) { access_requests++; }
static void on_line(char *out, size_t n) { snprintf(out, n, "state line"); }
static ui_setup_cb_t cb = {.save = on_save, .about = on_about, .has_buzzer = true, .has_sd = true, .dims = true,
                           .discovery = on_discovery, .disc_name = on_disc_name, .run_discovery = on_run_discovery, .roles_available = implemented,
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

static void test_discovery_shows_the_name_the_source_gave(void) {
  open_setup();
  click("menu_discovery");
  const char *line = lv_label_get_text(ui_setup_part("row_engine_port"));
  TEST_ASSERT_NOT_NULL(strstr(line, "port"));
  TEST_ASSERT_NOT_NULL(strstr(line, "Helm"));
  TEST_ASSERT_NULL(strstr(lv_label_get_text(ui_setup_part("row_engine_starboard")), "Helm"));   // no name: the id alone
}

static void test_a_saved_label_replaces_the_sources_name_and_the_name_is_never_saved(void) {
  open_setup();
  click("menu_discovery");
  click("confirm_engine");   // confirm without labelling: the source's name must not become a saved label
  const cfg_role_t *r = &ui_setup_working()->roles[role_index(ui_setup_working(), "engine")];
  TEST_ASSERT_EQUAL_STRING("", r->inst[0].label);
  click("label_engine_port");
  type_and_ok("MAIN");
  const char *line = lv_label_get_text(ui_setup_part("row_engine_port"));
  TEST_ASSERT_NOT_NULL(strstr(line, "MAIN"));
  TEST_ASSERT_NULL(strstr(line, "Helm"));
  click("label_engine_port");
  type_and_ok("");   // cleared: back to the source's name
  TEST_ASSERT_NOT_NULL(strstr(lv_label_get_text(ui_setup_part("row_engine_port")), "Helm"));
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

// ---- Setup > Power ----

#define PN1 "c046aa002cc001f9"
#define PN2 "c046a0002cc001f5"
#define PN3 "c046a0002cc001f8"
static ui_power_row_t pw_rows[UI_POWER_ROWS];
static uint8_t pw_n;
static uint8_t on_power_rows(ui_power_row_t *out, uint8_t max) {
  (void)max;
  memcpy(out, pw_rows, sizeof pw_rows[0] * pw_n);
  return pw_n;
}
static void pw_add(const char *ref, const char *label, uint8_t fn, double v, const char *proposed) {
  ui_power_row_t *r = &pw_rows[pw_n++];
  memset(r, 0, sizeof *r);
  snprintf(r->ref, sizeof r->ref, "%s", ref);
  snprintf(r->label, sizeof r->label, "%s", label);
  r->fn = fn;
  r->value = v;
  r->value_ok = true;
  snprintf(r->proposed, sizeof r->proposed, "%s", proposed);
}
static void pw_fixture(void) {
  pw_n = 0;
  pw_add(PN1 ".0", "SmartShunt", UI_PW_BATTERY, 13.1, "");
  pw_add(PN1 ".1", "SmartShunt", UI_PW_BATTERY, 12.8, "");
  pw_add("", "Battery", UI_PW_BATTERY, 12.0, "");                    // identity not here yet
  pw_add(PN2 ".0", "BlueSolar", UI_PW_OUTPUT, 200, "solar");
  pw_add(PN3 ".0", "Phoenix", UI_PW_OUTPUT, 3, "shore");
  pw_add(PN3 ".1", "Phoenix", UI_PW_OUTPUT, 0, "shore");
}
static cfg_role_t *pw_role(void) {
  cfg_t *w = (cfg_t *)ui_setup_working();
  for (uint8_t i = 0; i < w->role_count; i++)
    if (!strcmp(w->roles[i].role, "power")) return &w->roles[i];
  return NULL;
}
static void pw_open(void) {
  cb.power_rows = on_power_rows;
  pw_fixture();
  cfg_defaults(&cfg);
  bool have = false;
  for (uint8_t i = 0; i < cfg.role_count; i++) have |= !strcmp(cfg.roles[i].role, "power");
  if (!have) strcpy(cfg.roles[cfg.role_count++].role, "power");
  open_setup();
}
static void pw_bank(cfg_role_t *r, const char *id, const char *name, const char *kind, const char *ref) {
  cfg_bank_t *k = &r->bank[r->bank_count++];
  snprintf(k->id, sizeof k->id, "%s", id);
  snprintf(k->name, sizeof k->name, "%s", name);
  snprintf(k->kind, sizeof k->kind, "%s", kind);
  snprintf(k->battery, sizeof k->battery, "%s", ref);
}
static const char *text_of(const char *part) {
  lv_obj_t *o = ui_setup_part(part);
  return o && lv_obj_check_type(o, &lv_label_class) ? lv_label_get_text(o) : "(none)";
}


// A choice button opens one shared menu (a column of options); these drive it as a finger would.
static lv_obj_t *menu_obj(void) { return ui_setup_part("menu"); }
static void choose(const char *part, uint32_t idx) {
  click(part);
  lv_obj_t *m = menu_obj();
  TEST_ASSERT_NOT_NULL_MESSAGE(m, part);
  lv_buttonmatrix_set_selected_button(m, (uint32_t)idx);
  lv_obj_send_event(m, LV_EVENT_VALUE_CHANGED, NULL);
  lv_timer_handler();
}
static char options_buf[512];
static const char *options_of(const char *part) {   // the options a choice offers, one a line
  click(part);
  lv_obj_t *m = menu_obj();
  options_buf[0] = 0;
  if (!m) return "(none)";
  for (uint32_t i = 0; i < 16; i++) {
    const char *t = lv_buttonmatrix_get_button_text(m, i);
    if (!t || !*t || !strcmp(t, "\n")) break;
    strncat(options_buf, t, sizeof options_buf - strlen(options_buf) - 2);
    strcat(options_buf, "\n");
  }
  click("menu_cancel");
  return options_buf;
}
static const char *button_text(const char *part) {
  lv_obj_t *b = ui_setup_part(part);
  lv_obj_t *l = b ? lv_obj_get_child(b, 0) : NULL;
  return l ? lv_label_get_text(l) : "(none)";
}

static void test_power_screen_lists_sections(void) {
  pw_open();
  click("menu_power");
  TEST_ASSERT_EQUAL(SETUP_POWER, ui_setup_screen());
  TEST_ASSERT_NOT_NULL(ui_setup_part("add_bank"));
  TEST_ASSERT_NOT_NULL(ui_setup_part("addto_" PN1 ".0"));   // a battery: ADD TO BANK
  TEST_ASSERT_NULL(ui_setup_part("feeds_" PN2 ".0"));       // a source needs a bank to feed first
  TEST_ASSERT_NOT_NULL(ui_setup_part("wait_2"));
}

static void test_power_unnamed_device_says_it_is_identifying(void) {
  pw_open();
  click("menu_power");
  TEST_ASSERT_NOT_NULL(strstr(text_of("wait_2"), "Identifying"));
  TEST_ASSERT_NULL(strstr(text_of("wait_2"), "identity"));   // not the bus's word for it
}

static void test_power_bank_kind_is_a_menu(void) {
  pw_open();
  pw_bank(pw_role(), "x", "Anchor", "other", PN1 ".0");
  click("menu_power");
  TEST_ASSERT_NOT_NULL(strstr(options_of("bank_kind_0"), "THRUSTER"));
  choose("bank_kind_0", 0);
  TEST_ASSERT_EQUAL_STRING("house", pw_role()->bank[0].kind);
}

static void test_power_kind_menu_does_not_offer_a_second_house(void) {
  pw_open();
  pw_bank(pw_role(), "house", "HOUSE", "house", PN1 ".0");
  pw_bank(pw_role(), "start", "START", "start", PN1 ".1");
  click("menu_power");
  const char *o = options_of("bank_kind_1");   // the start bank's own kind and the free ones
  TEST_ASSERT_NULL(strstr(o, "HOUSE"));
  TEST_ASSERT_NOT_NULL(strstr(o, "START"));
  TEST_ASSERT_NOT_NULL(strstr(o, "NAV"));
}

static void test_power_battery_menu_lists_unplaced_batteries_by_input(void) {
  pw_open();
  pw_bank(pw_role(), "house", "House", "house", PN1 ".0");
  click("menu_power");
  const char *o = options_of("bank_batt_0");
  TEST_ASSERT_NOT_NULL(strstr(o, "input 1"));   // two inputs of one shunt are told apart
  TEST_ASSERT_NOT_NULL(strstr(o, "input 2"));
  TEST_ASSERT_NULL(strstr(o, "Battery"));       // the one still identifying is not offered
  choose("bank_batt_0", 1);
  TEST_ASSERT_EQUAL_STRING(PN1 ".1", pw_role()->bank[0].battery);
}

static void test_power_unassigned_source_picks_the_bank_it_feeds(void) {
  pw_open();
  pw_bank(pw_role(), "house", "House", "house", PN1 ".0");
  pw_bank(pw_role(), "start", "Start", "start", PN1 ".1");
  click("menu_power");
  TEST_ASSERT_NOT_NULL(strstr(options_of("feeds_" PN2 ".0"), "START"));   // it names banks, not devices
  choose("feeds_" PN2 ".0", 0);
  choose("feeds_" PN3 ".1", 1);
  cfg_role_t *r = pw_role();
  TEST_ASSERT_EQUAL_UINT8(2, r->flow_count);
  TEST_ASSERT_EQUAL_STRING(PN2, r->flow[0].device);
  TEST_ASSERT_EQUAL_STRING("solar", r->flow[0].kind);       // the proposal, to be changed in Sources
  TEST_ASSERT_EQUAL_STRING("house", r->flow[0].to);
  TEST_ASSERT_EQUAL_STRING(PN3, r->flow[1].device);
  TEST_ASSERT_EQUAL_UINT8(1, r->flow[1].output);            // the second output of that device
  TEST_ASSERT_EQUAL_STRING("start", r->flow[1].to);
  TEST_ASSERT_NULL(ui_setup_part("feeds_" PN3 ".1"));       // placed
  TEST_ASSERT_NOT_NULL(ui_setup_part("feeds_" PN3 ".0"));
}

static void test_power_source_row_shows_kind_and_fed_bank(void) {
  pw_open();
  pw_bank(pw_role(), "house", "House", "house", PN1 ".0");
  pw_bank(pw_role(), "start", "Start", "start", PN1 ".1");
  click("menu_power");
  choose("feeds_" PN2 ".0", 0);
  TEST_ASSERT_NOT_NULL(strstr(button_text("flow_kind_0"), "SOLAR"));
  TEST_ASSERT_NOT_NULL(strstr(button_text("flow_to_0"), "HOUSE"));   // it names the bank, not a device
  choose("flow_to_0", 1);
  TEST_ASSERT_EQUAL_STRING("start", pw_role()->flow[0].to);
  choose("flow_kind_0", 4);   // SOLAR SHORE ENGINE CONVERTER OTHER
  TEST_ASSERT_EQUAL_STRING("other", pw_role()->flow[0].kind);
}

static void test_power_converter_kind_asks_where_it_draws_from(void) {
  pw_open();
  pw_bank(pw_role(), "house", "House", "house", PN1 ".0");
  pw_bank(pw_role(), "start", "Start", "start", PN1 ".1");
  click("menu_power");
  choose("feeds_" PN3 ".0", 0);
  choose("flow_kind_0", 3);
  TEST_ASSERT_EQUAL_STRING("converter", pw_role()->flow[0].kind);
  TEST_ASSERT_EQUAL_STRING("start", pw_role()->flow[0].from);   // not the bank it feeds
  TEST_ASSERT_NOT_NULL(ui_setup_part("flow_from_0"));
  char why[96];
  TEST_ASSERT_TRUE_MESSAGE(cfg_power_check(pw_role(), why, sizeof why), why);
}

static void test_power_second_output_of_a_device_says_which(void) {
  pw_open();
  pw_bank(pw_role(), "house", "House", "house", PN1 ".0");
  click("menu_power");
  choose("feeds_" PN3 ".1", 0);
  TEST_ASSERT_NOT_NULL(ui_setup_part("flow_out_0"));
  TEST_ASSERT_NOT_NULL(strstr(options_of("flow_out_0"), "OUTPUT 2"));
}

static void test_power_save_refuses_without_a_house_bank(void) {
  pw_open();
  click("menu_power");
  click("addto_" PN1 ".0");
  lv_buttonmatrix_set_selected_button(menu_obj(), 6);   // OTHER
  lv_obj_send_event(menu_obj(), LV_EVENT_VALUE_CHANGED, NULL);
  lv_timer_handler();
  click("save");
  TEST_ASSERT_EQUAL_INT(0, saves);
  TEST_ASSERT_EQUAL(SETUP_POWER, ui_setup_screen());
  TEST_ASSERT_NOT_NULL(strstr(text_of("power_error"), "house"));
  choose("bank_kind_0", 0);
  click("save");
  TEST_ASSERT_EQUAL_INT(1, saves);
  const cfg_role_t *sp = NULL;
  for (uint8_t i = 0; i < saved.role_count; i++)
    if (!strcmp(saved.roles[i].role, "power")) sp = &saved.roles[i];
  TEST_ASSERT_NOT_NULL(sp);
  TEST_ASSERT_EQUAL_STRING("house", sp->bank[0].kind);
}

static void test_power_placed_unseen_reads_missing(void) {
  pw_open();
  pw_bank(pw_role(), "house", "HOUSE", "house", "bb00000000000009.0");
  cfg_flow_t *f = &pw_role()->flow[pw_role()->flow_count++];
  strcpy(f->device, "aa00000000000009");
  strcpy(f->kind, "engine");
  strcpy(f->to, "house");
  click("menu_power");
  TEST_ASSERT_NOT_NULL(ui_setup_part("missing_bank_0"));
  TEST_ASSERT_NOT_NULL(ui_setup_part("missing_flow_0"));
}

static void test_power_old_keys_message_shown(void) {
  pw_open();
  pw_role()->power_old_keys = true;
  pw_role()->power_rejected = true;
  click("menu_power");
  TEST_ASSERT_NOT_NULL(strstr(text_of("power_notice"), "old-format"));
}

static void test_power_delete_bank_returns_its_sources_to_unassigned(void) {
  pw_open();
  pw_bank(pw_role(), "house", "HOUSE", "house", PN1 ".0");
  cfg_flow_t *f = &pw_role()->flow[pw_role()->flow_count++];
  strcpy(f->device, PN2);
  strcpy(f->kind, "solar");
  strcpy(f->to, "house");
  click("menu_power");
  click("bank_del_0");
  TEST_ASSERT_EQUAL_UINT8(0, pw_role()->bank_count);
  TEST_ASSERT_EQUAL_UINT8(0, pw_role()->flow_count);          // a source never exists without a destination
  TEST_ASSERT_NOT_NULL(ui_setup_part("addto_" PN1 ".0"));     // and the battery and the source are listed again
}

static void test_power_one_bank_places_a_one_battery_boat(void) {
  pw_open();
  pw_n = 0;
  pw_add(PN1 ".0", "SmartShunt", UI_PW_BATTERY, 13.1, "");
  pw_add(PN2 ".0", "BlueSolar", UI_PW_OUTPUT, 200, "solar");
  pw_add(PN3 ".0", "Phoenix", UI_PW_OUTPUT, 3, "shore");
  pw_add("", "Charger", UI_PW_OUTPUT, 0, "shore");   // still identifying: left alone
  click("menu_power");
  click("one_bank");
  cfg_role_t *r = pw_role();
  TEST_ASSERT_EQUAL_UINT8(1, r->bank_count);
  TEST_ASSERT_EQUAL_STRING("house", r->bank[0].kind);
  TEST_ASSERT_EQUAL_STRING("HOUSE", r->bank[0].name);
  TEST_ASSERT_EQUAL_STRING(PN1 ".0", r->bank[0].battery);
  TEST_ASSERT_EQUAL_UINT8(2, r->flow_count);
  for (int i = 0; i < 2; i++) TEST_ASSERT_EQUAL_STRING("house", r->flow[i].to);
  TEST_ASSERT_EQUAL_STRING("solar", r->flow[0].kind);
  TEST_ASSERT_EQUAL_STRING("shore", r->flow[1].kind);
  char why[96];
  TEST_ASSERT_TRUE_MESSAGE(cfg_power_check(r, why, sizeof why), why);
}

static void test_power_one_bank_noop_with_two_batteries(void) {
  pw_open();   // the fixture has three batteries
  click("menu_power");
  click("one_bank");
  TEST_ASSERT_EQUAL_UINT8(0, pw_role()->bank_count);
  TEST_ASSERT_NOT_NULL(strstr(text_of("power_error"), "one battery"));
}

static void test_power_one_bank_skips_converters(void) {
  pw_open();
  pw_n = 0;
  pw_add(PN1 ".0", "SmartShunt", UI_PW_BATTERY, 13.1, "");
  pw_add(PN2 ".0", "Orion", UI_PW_OUTPUT, 40, "converter");   // needs two banks
  pw_add(PN3 ".0", "Phoenix", UI_PW_OUTPUT, 3, "shore");
  click("menu_power");
  click("one_bank");
  TEST_ASSERT_EQUAL_UINT8(1, pw_role()->flow_count);
  TEST_ASSERT_EQUAL_STRING("shore", pw_role()->flow[0].kind);
}

static void test_power_one_bank_is_offered_only_when_nothing_is_placed(void) {
  pw_open();
  pw_bank(pw_role(), "house", "HOUSE", "house", PN1 ".0");
  click("menu_power");
  TEST_ASSERT_NULL(ui_setup_part("one_bank"));
}

static void test_discovery_leaves_power_to_its_own_page(void) {
  disc_power = true;
  pw_open();
  click("menu_discovery");
  TEST_ASSERT_NULL(ui_setup_part("keep_power_battery.x"));
  TEST_ASSERT_NOT_NULL(ui_setup_part("open_power"));
  disc_power = false;
}

static void test_power_a_menu_left_open_does_not_outlive_setup(void) {
  pw_open();
  pw_bank(pw_role(), "house", "House", "house", PN1 ".0");
  click("menu_power");
  click("bank_kind_0");                 // the menu is open
  TEST_ASSERT_NOT_NULL(ui_setup_part("menu"));
  ui_setup_close();
  lv_timer_handler();
  pw_open();                             // reopened: nothing stale to delete
  pw_bank(pw_role(), "house", "House", "house", PN1 ".0");
  click("menu_power");
  click("bank_kind_0");
  TEST_ASSERT_NOT_NULL(ui_setup_part("menu"));
  click("menu_cancel");
  TEST_ASSERT_NULL(ui_setup_part("menu"));
}

static void test_power_bank_battery_button_names_the_battery(void) {
  pw_open();
  pw_bank(pw_role(), "start", "Start", "start", PN1 ".1");
  click("menu_power");
  TEST_ASSERT_NOT_NULL(strstr(button_text("bank_batt_0"), "SmartShunt input 2"));   // which input, not just "BATTERY"
  TEST_ASSERT_NOT_NULL(strstr(button_text("bank_batt_0"), "12.80 V"));
}

static void test_power_add_to_bank_asks_which_bank(void) {
  pw_open();
  click("menu_power");
  click("addto_" PN1 ".0");
  TEST_ASSERT_NULL(ui_setup_part("kb"));                      // nothing is typed
  TEST_ASSERT_NOT_NULL(ui_setup_part("menu"));
  lv_buttonmatrix_set_selected_button(menu_obj(), 0);         // HOUSE, START, NAV, ...
  lv_obj_send_event(menu_obj(), LV_EVENT_VALUE_CHANGED, NULL);
  lv_timer_handler();
  cfg_role_t *r = pw_role();
  TEST_ASSERT_EQUAL_UINT8(1, r->bank_count);
  TEST_ASSERT_EQUAL_STRING("house", r->bank[0].kind);
  TEST_ASSERT_EQUAL_STRING("HOUSE", r->bank[0].name);         // the name is the kind
  TEST_ASSERT_EQUAL_STRING(PN1 ".0", r->bank[0].battery);
  TEST_ASSERT_NULL(ui_setup_part("addto_" PN1 ".0"));         // placed, no longer unassigned
}

static void test_power_the_house_bank_is_not_offered_twice(void) {
  pw_open();
  pw_bank(pw_role(), "house", "HOUSE", "house", PN1 ".0");
  click("menu_power");
  click("addto_" PN1 ".1");
  lv_obj_t *m = menu_obj();
  TEST_ASSERT_NOT_NULL(m);
  TEST_ASSERT_EQUAL_STRING("START", lv_buttonmatrix_get_button_text(m, 0));   // first on the list now
  for (uint32_t i = 0; i < 8; i++) {
    const char *t = lv_buttonmatrix_get_button_text(m, i);
    if (!t || !*t || !strcmp(t, "\n")) break;
    TEST_ASSERT_NOT_EQUAL(0, strcmp(t, "HOUSE"));
  }
  click("menu_cancel");
}

static void test_power_a_kind_already_added_is_not_offered_again(void) {
  pw_open();
  pw_bank(pw_role(), "house", "HOUSE", "house", PN1 ".0");
  pw_bank(pw_role(), "start", "START", "start", PN1 ".1");
  pw_add("c046aa002cc00201.0", "NavShunt", UI_PW_BATTERY, 13.0, "");   // a third battery is seen
  click("menu_power");
  click("addto_c046aa002cc00201.0");
  lv_obj_t *m = menu_obj();
  TEST_ASSERT_NOT_NULL(m);
  TEST_ASSERT_EQUAL_STRING("NAV", lv_buttonmatrix_get_button_text(m, 0));   // house and start are taken
  for (uint32_t i = 0; i < 8; i++) {
    const char *t = lv_buttonmatrix_get_button_text(m, i);
    if (!t || !*t || !strcmp(t, "\n")) break;
    TEST_ASSERT_NOT_EQUAL(0, strcmp(t, "HOUSE"));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(t, "START"));
  }
  click("menu_cancel");
}

static void test_power_every_kind_added_means_no_more_banks(void) {
  pw_open();
  const char *kinds[] = {"house", "start", "nav", "windlass", "thruster", "genset", "other"};
  for (int i = 0; i < 7; i++) {
    char ref[32];
    snprintf(ref, sizeof ref, PN1 ".%d", i + 3);
    pw_bank(pw_role(), kinds[i], kinds[i], kinds[i], ref);
  }
  pw_add("c046aa002cc00201.0", "NavShunt", UI_PW_BATTERY, 13.0, "");
  click("menu_power");
  click("addto_c046aa002cc00201.0");
  TEST_ASSERT_NULL(menu_obj());
  TEST_ASSERT_NOT_NULL(strstr(text_of("power_error"), "kind of bank"));
}

static void test_power_add_bank_picks_the_kind_and_takes_an_unplaced_battery(void) {
  pw_open();
  pw_bank(pw_role(), "house", "HOUSE", "house", PN1 ".0");
  click("menu_power");
  click("add_bank");
  lv_buttonmatrix_set_selected_button(menu_obj(), 0);   // START (the house bank exists)
  lv_obj_send_event(menu_obj(), LV_EVENT_VALUE_CHANGED, NULL);
  lv_timer_handler();
  cfg_role_t *r = pw_role();
  TEST_ASSERT_EQUAL_UINT8(2, r->bank_count);
  TEST_ASSERT_EQUAL_STRING("start", r->bank[1].kind);
  TEST_ASSERT_EQUAL_STRING(PN1 ".1", r->bank[1].battery);   // not the placed one, not the one still identifying
  click("add_bank");                                         // nothing unplaced is left: no menu, a reason
  TEST_ASSERT_NULL(ui_setup_part("menu"));
  TEST_ASSERT_EQUAL_UINT8(2, r->bank_count);
  TEST_ASSERT_NOT_NULL(strstr(text_of("power_error"), "No unplaced"));
}

static void test_power_bank_row_leads_with_its_kind(void) {
  pw_open();
  pw_bank(pw_role(), "house", "HOUSE", "house", PN1 ".0");
  click("menu_power");
  TEST_ASSERT_NULL(ui_setup_part("bank_name_0"));            // there is no name to type
  lv_obj_t *kind = ui_setup_part("bank_kind_0"), *batt = ui_setup_part("bank_batt_0");
  TEST_ASSERT_NOT_NULL(kind);
  TEST_ASSERT_NOT_NULL(batt);
  lv_obj_update_layout(scr);
  lv_area_t ak, ab;
  lv_obj_get_coords(kind, &ak);
  lv_obj_get_coords(batt, &ab);
  TEST_ASSERT_LESS_THAN_INT(ab.x1, ak.x1);                   // the kind is the leftmost field
}

static void test_power_changing_a_banks_kind_renames_it(void) {
  pw_open();
  pw_bank(pw_role(), "x", "HOUSE", "house", PN1 ".0");
  click("menu_power");
  choose("bank_kind_0", 3);   // WINDLASS
  TEST_ASSERT_EQUAL_STRING("windlass", pw_role()->bank[0].kind);
  TEST_ASSERT_EQUAL_STRING("WINDLASS", pw_role()->bank[0].name);
}

static void test_power_screen_square_battery_on_second_line(void) {
  cb.power_rows = on_power_rows;
  pw_fixture();
  cfg_defaults(&cfg);
  bool have = false;
  for (uint8_t i = 0; i < cfg.role_count; i++) have |= !strcmp(cfg.roles[i].role, "power");
  if (!have) strcpy(cfg.roles[cfg.role_count++].role, "power");
  ui_setup_open(scr, &cfg, SIZE_SQUARE, &cb);
  pw_bank(pw_role(), "house", "HOUSE", "house", PN1 ".0");
  click("menu_power");
  lv_obj_update_layout(scr);
  lv_obj_t *kind = ui_setup_part("bank_kind_0"), *batt = ui_setup_part("bank_batt_0");
  TEST_ASSERT_NOT_NULL(kind);
  TEST_ASSERT_NOT_NULL(batt);
  lv_area_t a, b;
  lv_obj_get_coords(kind, &a);
  lv_obj_get_coords(batt, &b);
  TEST_ASSERT_GREATER_THAN_INT(a.y2, b.y1 - 1);   // the long battery button starts below the kind, not beside it
}

static void test_power_menu_options_fire_on_a_click_not_a_touch(void) {
  pw_open();
  click("menu_power");
  click("addto_" PN1 ".0");
  lv_obj_t *m = menu_obj();
  TEST_ASSERT_NOT_NULL(m);
  for (uint32_t i = 0; i < 7; i++) {   // a swipe to scroll a long menu must not choose what the finger lands on
    TEST_ASSERT_TRUE(lv_buttonmatrix_has_button_ctrl(m, i, LV_BUTTONMATRIX_CTRL_CLICK_TRIG));
    TEST_ASSERT_TRUE(lv_buttonmatrix_has_button_ctrl(m, i, LV_BUTTONMATRIX_CTRL_NO_REPEAT));
  }
  click("menu_cancel");
}

static void test_power_one_bank_does_not_duplicate_sources_left_without_a_bank(void) {
  pw_open();
  pw_n = 0;
  pw_add(PN1 ".0", "SmartShunt", UI_PW_BATTERY, 13.1, "");
  pw_add(PN2 ".0", "BlueSolar", UI_PW_OUTPUT, 200, "solar");
  pw_add(PN3 ".0", "Phoenix", UI_PW_OUTPUT, 3, "shore");
  cfg_flow_t *f = &pw_role()->flow[pw_role()->flow_count++];   // what is left when the only bank is deleted in an older build
  strcpy(f->device, PN2);
  strcpy(f->kind, "solar");
  click("menu_power");
  click("one_bank");
  cfg_role_t *r = pw_role();
  TEST_ASSERT_EQUAL_UINT8(2, r->flow_count);                    // the solar source is not added twice
  TEST_ASSERT_EQUAL_STRING("house", r->flow[0].to);             // it gets the new bank
  char why[96];
  TEST_ASSERT_TRUE_MESSAGE(cfg_power_check(r, why, sizeof why), why);
}

static void test_power_a_file_banks_own_name_is_what_its_row_shows(void) {
  pw_open();
  pw_bank(pw_role(), "house", "HOUSE", "house", PN1 ".0");
  pw_bank(pw_role(), "fridge", "Fridge", "other", PN1 ".1");
  click("menu_power");
  TEST_ASSERT_NOT_NULL(strstr(button_text("bank_kind_1"), "FRIDGE"));   // the same name the source menus list
}

static void test_power_changing_a_kind_updates_the_id_and_the_sources_that_name_it(void) {
  pw_open();
  pw_bank(pw_role(), "house", "HOUSE", "house", PN1 ".0");
  pw_bank(pw_role(), "start", "START", "start", PN1 ".1");
  cfg_flow_t *f = &pw_role()->flow[pw_role()->flow_count++];
  strcpy(f->device, PN2);
  strcpy(f->kind, "solar");
  strcpy(f->to, "start");
  click("menu_power");
  // the start bank's menu has no HOUSE (taken): START NAV WINDLASS THRUSTER GENSET OTHER
  choose("bank_kind_1", 1);
  cfg_role_t *r = pw_role();
  TEST_ASSERT_EQUAL_STRING("nav", r->bank[1].kind);
  TEST_ASSERT_EQUAL_STRING("NAV", r->bank[1].name);
  TEST_ASSERT_EQUAL_STRING("nav", r->bank[1].id);
  TEST_ASSERT_EQUAL_STRING("nav", r->flow[0].to);               // the source follows its bank
  char why[96];
  TEST_ASSERT_TRUE_MESSAGE(cfg_power_check(r, why, sizeof why), why);
  choose("bank_kind_1", 5);                                     // OTHER
  TEST_ASSERT_EQUAL_STRING("OTHER", r->bank[1].name);           // an other bank still has a name
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
  RUN_TEST(test_discovery_shows_the_name_the_source_gave);
  RUN_TEST(test_a_saved_label_replaces_the_sources_name_and_the_name_is_never_saved);
  RUN_TEST(test_discovery_label_edit);
  RUN_TEST(test_power_screen_lists_sections);
  RUN_TEST(test_power_unnamed_device_says_it_is_identifying);
  RUN_TEST(test_power_bank_kind_is_a_menu);
  RUN_TEST(test_power_kind_menu_does_not_offer_a_second_house);
  RUN_TEST(test_power_battery_menu_lists_unplaced_batteries_by_input);
  RUN_TEST(test_power_unassigned_source_picks_the_bank_it_feeds);
  RUN_TEST(test_power_source_row_shows_kind_and_fed_bank);
  RUN_TEST(test_power_converter_kind_asks_where_it_draws_from);
  RUN_TEST(test_power_second_output_of_a_device_says_which);
  RUN_TEST(test_power_save_refuses_without_a_house_bank);
  RUN_TEST(test_power_placed_unseen_reads_missing);
  RUN_TEST(test_power_old_keys_message_shown);
  RUN_TEST(test_power_delete_bank_returns_its_sources_to_unassigned);
  RUN_TEST(test_power_one_bank_places_a_one_battery_boat);
  RUN_TEST(test_power_one_bank_noop_with_two_batteries);
  RUN_TEST(test_power_one_bank_skips_converters);
  RUN_TEST(test_power_one_bank_is_offered_only_when_nothing_is_placed);
  RUN_TEST(test_power_a_menu_left_open_does_not_outlive_setup);
  RUN_TEST(test_power_bank_battery_button_names_the_battery);
  RUN_TEST(test_discovery_leaves_power_to_its_own_page);
  RUN_TEST(test_power_add_to_bank_asks_which_bank);
  RUN_TEST(test_power_the_house_bank_is_not_offered_twice);
  RUN_TEST(test_power_a_kind_already_added_is_not_offered_again);
  RUN_TEST(test_power_every_kind_added_means_no_more_banks);
  RUN_TEST(test_power_menu_options_fire_on_a_click_not_a_touch);
  RUN_TEST(test_power_one_bank_does_not_duplicate_sources_left_without_a_bank);
  RUN_TEST(test_power_a_file_banks_own_name_is_what_its_row_shows);
  RUN_TEST(test_power_changing_a_kind_updates_the_id_and_the_sources_that_name_it);
  RUN_TEST(test_power_add_bank_picks_the_kind_and_takes_an_unplaced_battery);
  RUN_TEST(test_power_bank_row_leads_with_its_kind);
  RUN_TEST(test_power_changing_a_banks_kind_renames_it);
  RUN_TEST(test_power_screen_square_battery_on_second_line);
  return UNITY_END();
}
