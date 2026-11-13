// The Power pages with the power configuration: not set up, unassigned rows and marker, any number of banks.
#include <stdio.h>
#include <string.h>
#include "alerts.h"
#include "platform.h"
#include "power_topo.h"
#include "ui_shell.h"
#include "unity.h"

static uint8_t buf[800 * 48 * 2];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  (void)a; (void)px;
  lv_display_flush_ready(d);
}
static uint32_t ms;
static uint32_t tick_ms(void) { return ms; }

#define NOW platform_millis()
#define SHUNT "c046aa002cc001f9"
#define MPPT "c046a0002cc001f5"
static cfg_t cfg;
static cfg_role_t rc;
static role_binding_t b;
static lv_obj_t *scr;
static int opened;
static void opener(void) { opened++; }

static void endpoint(const char *group, const char *id, const char *ref, double v) {
  char p[96];
  snprintf(p, sizeof p, "electrical.resolved.%s.%s.ref", group, id);
  bd_set_text(bd_intern(p), ref, 1, NOW);
  snprintf(p, sizeof p, "electrical.resolved.%s.%s.voltage", group, id);
  bd_set_number(bd_intern(p), v, 1, NOW);
  snprintf(p, sizeof p, "electrical.resolved.%s.%s.current", group, id);
  bd_set_number(bd_intern(p), 1.0, 1, NOW);
}
static void add_bank(const char *id, const char *name, const char *kind, const char *battery) {
  cfg_bank_t *k = &rc.bank[rc.bank_count++];
  snprintf(k->id, sizeof k->id, "%s", id);
  snprintf(k->name, sizeof k->name, "%s", name);
  snprintf(k->kind, sizeof k->kind, "%s", kind);
  snprintf(k->battery, sizeof k->battery, "%s", battery);
}
static void add_flow(const char *device, const char *kind, const char *to) {
  cfg_flow_t *f = &rc.flow[rc.flow_count++];
  snprintf(f->device, sizeof f->device, "%s", device);
  snprintf(f->kind, sizeof f->kind, "%s", kind);
  snprintf(f->to, sizeof f->to, "%s", to);
}
static void bind_power(void) { role_bind(role_find("power"), &rc, &cfg.units, &b); }

void setUp(void) {
  ui_theme_set_current(PALETTE_DAY);
  bd_init();
  alerts_init();
  cfg_defaults(&cfg);
  memset(&rc, 0, sizeof rc);
  strcpy(rc.role, "power");
  opened = 0;
  power_set_setup_opener(opener);
  lv_obj_clean(scr);
}
void tearDown(void) {}

static lv_obj_t *build(uint8_t page, size_class_t sc) {
  lv_obj_t *area = lv_obj_create(scr);
  lv_obj_remove_style_all(area);
  bool wide = sc == SIZE_WIDE;
  lv_obj_set_size(area, wide ? 800 : 480, wide ? 440 : 444);
  lv_obj_set_pos(area, 0, 0);
  role_find("power")->build_page(area, page, sc, &b);
  lv_obj_update_layout(area);
  return area;
}

// The first label under `o` whose text contains `needle`, else NULL.
static lv_obj_t *find_text(lv_obj_t *o, const char *needle) {
  if (lv_obj_check_type(o, &lv_label_class) && strstr(lv_label_get_text(o), needle)) return o;
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
    lv_obj_t *f = find_text(lv_obj_get_child(o, (int32_t)i), needle);
    if (f) return f;
  }
  return NULL;
}
static void click_text(lv_obj_t *area, const char *needle) {
  lv_obj_t *o = find_text(area, needle);
  TEST_ASSERT_NOT_NULL(o);
  while (o && !lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) o = lv_obj_get_parent(o);
  TEST_ASSERT_NOT_NULL(o);
  lv_obj_send_event(o, LV_EVENT_CLICKED, NULL);
}

static void test_summary_not_set_up_without_house(void) {
  endpoint("batteries", "h", SHUNT ".0", 12.6);
  bind_power();
  lv_obj_t *a = build(0, SIZE_WIDE);
  TEST_ASSERT_NOT_NULL(find_text(a, "NOT SET UP"));
  click_text(a, "SET UP POWER");
  TEST_ASSERT_EQUAL_INT(1, opened);
}

static void test_summary_marker_counts_unassigned(void) {
  endpoint("batteries", "h", SHUNT ".0", 12.6);
  endpoint("batteries", "s", SHUNT ".1", 12.8);
  endpoint("batteries", "x", "bb00000000000001.0", 12.8);
  add_bank("house", "HOUSE", "house", SHUNT ".0");
  bind_power();
  lv_obj_t *a = build(0, SIZE_WIDE);
  TEST_ASSERT_NULL(find_text(a, "NOT SET UP"));
  TEST_ASSERT_NOT_NULL(find_text(a, "2 UNASSIGNED"));
  click_text(a, "2 UNASSIGNED");
  TEST_ASSERT_EQUAL_INT(1, opened);
}

static void test_summary_has_no_marker_when_everything_is_placed(void) {
  endpoint("batteries", "h", SHUNT ".0", 12.6);
  add_bank("house", "HOUSE", "house", SHUNT ".0");
  bind_power();
  TEST_ASSERT_NULL(find_text(build(0, SIZE_WIDE), "UNASSIGNED"));
}

static void five_banks(void) {
  const char *kinds[] = {"house", "start", "nav", "thruster", "other"};
  const char *names[] = {"HOUSE", "START", "NAV", "BOW", "ODD JOB"};
  for (int i = 0; i < 5; i++) {
    char ref[32], id[8];
    snprintf(ref, sizeof ref, SHUNT ".%d", i);
    snprintf(id, sizeof id, "b%d", i);
    endpoint("batteries", id, ref, 12.0 + i);
    add_bank(id, names[i], kinds[i], ref);
  }
}

static void test_banks_page_lists_five_banks_in_order(void) {
  five_banks();
  bind_power();
  const role_def_t *pw = role_find("power");
  // Summary, two Banks pages (four wide, then one), Batteries, Charging
  TEST_ASSERT_EQUAL_UINT8(5, pw->pages(&b, SIZE_WIDE));
  lv_obj_t *p1 = build(1, SIZE_WIDE);
  const char *first[] = {"HOUSE", "START", "NAV", "BOW"};
  lv_coord_t last_x = -1;
  for (int i = 0; i < 4; i++) {
    lv_obj_t *l = find_text(p1, first[i]);
    TEST_ASSERT_NOT_NULL(l);
    lv_area_t ar;
    lv_obj_get_coords(l, &ar);
    TEST_ASSERT_GREATER_THAN_INT(last_x, ar.x1);   // left to right in configured order
    last_x = ar.x1;
  }
  TEST_ASSERT_NULL(find_text(p1, "ODD JOB"));
  TEST_ASSERT_NOT_NULL(find_text(build(2, SIZE_WIDE), "ODD JOB"));
}

static void test_batteries_unassigned_row_excluded_from_totals(void) {
  endpoint("batteries", "h", SHUNT ".0", 12.6);
  endpoint("batteries", "x", "bb00000000000001.0", 12.8);
  add_bank("house", "HOUSE", "house", SHUNT ".0");
  bind_power();
  lv_obj_t *a = build(2, SIZE_WIDE);   // Summary, Banks, Batteries
  TEST_ASSERT_NOT_NULL(find_text(a, "HOUSE"));
  TEST_ASSERT_NOT_NULL(find_text(a, "UNASSIGNED"));
  power_topo_t t;
  power_topo_resolve(&b, &t);
  power_banks_view_t v;
  power_banks_compute(&b, &t, NOW, &v);
  TEST_ASSERT_EQUAL_UINT8(1, t.bank_count);   // the unplaced battery is in no figure
}

static void test_charging_unassigned_flow_row(void) {
  endpoint("batteries", "h", SHUNT ".0", 12.6);
  endpoint("chargers", "m", MPPT ".0", 13.5);
  endpoint("chargers", "q", "dd00000000000001.0", 13.5);
  add_bank("house", "HOUSE", "house", SHUNT ".0");
  add_flow(MPPT, "solar", "house");
  bind_power();
  lv_obj_t *a = build(3, SIZE_WIDE);   // Summary, Banks, Batteries, Charging
  TEST_ASSERT_NOT_NULL(find_text(a, "TO HOUSE"));
  TEST_ASSERT_NOT_NULL(find_text(a, "UNASSIGNED"));
}

int main(void) {
  lv_init();
  lv_tick_set_cb(tick_ms);
  lv_display_t *disp = lv_display_create(800, 480);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  scr = lv_screen_active();
  UNITY_BEGIN();
  RUN_TEST(test_summary_not_set_up_without_house);
  RUN_TEST(test_summary_marker_counts_unassigned);
  RUN_TEST(test_summary_has_no_marker_when_everything_is_placed);
  RUN_TEST(test_banks_page_lists_five_banks_in_order);
  RUN_TEST(test_batteries_unassigned_row_excluded_from_totals);
  RUN_TEST(test_charging_unassigned_flow_row);
  return UNITY_END();
}
