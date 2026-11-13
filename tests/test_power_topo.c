#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "power_topo.h"

static cfg_t cfg;
static cfg_role_t rc;
static role_binding_t b;
static power_topo_t t;

static void add_bank(const char *id, const char *name, const char *kind) {
  cfg_bank_t *k = &rc.bank[rc.bank_count++];
  snprintf(k->id, sizeof k->id, "%s", id);
  snprintf(k->name, sizeof k->name, "%s", name);
  snprintf(k->kind, sizeof k->kind, "%s", kind);
}
static void add_link(const char *src, uint8_t output, const char *from, const char *to) {
  cfg_link_t *l = &rc.link[rc.link_count++];
  snprintf(l->src, sizeof l->src, "%s", src);
  l->output = output;
  snprintf(l->from, sizeof l->from, "%s", from);
  snprintf(l->to, sizeof l->to, "%s", to);
}
static void spec_topology(void) {
  add_bank("279", "HOUSE", "house");
  add_bank("280", "START", "start");
  add_bank("281", "", "nav");
  add_link("charger:276", 0, "", "house");
  add_link("charger:276", 1, "", "start");
  add_link("alternator:0", 0, "", "start");
  add_link("solar:288", 0, "", "house");
  add_link("dcdc:1", 0, "start", "house");
  add_link("dcdc:2", 0, "house", "nav");
}
static void bind_power(void) { role_bind(role_find("power"), &rc, &cfg.units, &b); }
static void intern_all(void) {
  const char *p[] = {"electrical.resolved.batteries.279.voltage", "electrical.resolved.batteries.280.voltage",
                     "electrical.resolved.batteries.281.voltage", "electrical.resolved.chargers.276.current",
                     "electrical.alternators.0.current", "electrical.resolved.solar.288.panelPower",
                     "electrical.dcdc.1.current", "electrical.dcdc.2.current"};
  for (size_t i = 0; i < sizeof p / sizeof p[0]; i++) bd_intern(p[i]);
}

void setUp(void) {
  bd_init();
  cfg_defaults(&cfg);
  memset(&rc, 0, sizeof rc);
  strcpy(rc.role, "power");
}
void tearDown(void) {}

static void test_topo_inactive_without_banks(void) {
  intern_all();
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_FALSE(t.active);
  TEST_ASSERT_EQUAL_UINT8(0, t.link_count);
}

static void test_topo_resolves_instances(void) {
  intern_all();
  spec_topology();
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_TRUE(t.active);
  for (int k = 0; k < BANK_KINDS; k++) TEST_ASSERT_TRUE(t.bank_bound[k]);
  TEST_ASSERT_EQUAL_STRING("279", b.g[G_BATTERY].inst_id[t.bank_inst[BANK_HOUSE]]);
  TEST_ASSERT_EQUAL_STRING("280", b.g[G_BATTERY].inst_id[t.bank_inst[BANK_START]]);
  TEST_ASSERT_EQUAL_STRING("281", b.g[G_BATTERY].inst_id[t.bank_inst[BANK_NAV]]);
  TEST_ASSERT_EQUAL_STRING("NAV", t.bank_name[BANK_NAV]);   // empty name falls back to the kind
  TEST_ASSERT_EQUAL_UINT8(6, t.link_count);
  for (int n = 0; n < 6; n++) TEST_ASSERT_TRUE(t.link[n].bound);
  TEST_ASSERT_EQUAL_UINT8(G_CHARGER, t.link[1].g);
  TEST_ASSERT_EQUAL_UINT8(1, t.link[1].output);
  TEST_ASSERT_EQUAL_INT8(BANK_START, t.link[1].to);
  TEST_ASSERT_EQUAL_INT8(-1, t.link[1].from);
  TEST_ASSERT_EQUAL_UINT8(G_DCDC, t.link[4].g);
  TEST_ASSERT_EQUAL_INT8(BANK_START, t.link[4].from);
  TEST_ASSERT_EQUAL_INT8(BANK_HOUSE, t.link[4].to);
  TEST_ASSERT_EQUAL_INT8(BANK_HOUSE, t.link[5].from);
  TEST_ASSERT_EQUAL_INT8(BANK_NAV, t.link[5].to);
}

static void test_topo_unbound_source_keeps_link(void) {
  const char *p[] = {"electrical.resolved.batteries.279.voltage", "electrical.resolved.batteries.280.voltage",
                     "electrical.resolved.batteries.281.voltage", "electrical.resolved.solar.288.panelPower",
                     "electrical.dcdc.1.current", "electrical.dcdc.2.current"};   // no charger, no alternator
  for (size_t i = 0; i < sizeof p / sizeof p[0]; i++) bd_intern(p[i]);
  spec_topology();
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_EQUAL_UINT8(6, t.link_count);
  TEST_ASSERT_FALSE(t.link[0].bound);
  TEST_ASSERT_FALSE(t.link[1].bound);
  TEST_ASSERT_FALSE(t.link[2].bound);
  TEST_ASSERT_TRUE(t.link[3].bound);
  TEST_ASSERT_TRUE(t.link[4].bound);
}

static void test_topo_unbound_bank(void) {
  const char *p[] = {"electrical.resolved.batteries.279.voltage", "electrical.resolved.batteries.280.voltage"};   // no 281
  for (size_t i = 0; i < 2; i++) bd_intern(p[i]);
  spec_topology();
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_TRUE(t.active);
  TEST_ASSERT_TRUE(t.bank_cfg[BANK_NAV]);
  TEST_ASSERT_FALSE(t.bank_bound[BANK_NAV]);
  TEST_ASSERT_TRUE(t.bank_bound[BANK_HOUSE]);
}

static void test_topo_banks_without_links(void) {
  intern_all();
  add_bank("279", "HOUSE", "house");
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_TRUE(t.active);
  TEST_ASSERT_EQUAL_UINT8(0, t.link_count);
  TEST_ASSERT_FALSE(t.bank_cfg[BANK_NAV]);
}

// ---- power_banks_compute: the spec topology with known samples ----
#define NOW 5000

static void set(const char *path, double v) { bd_set_number(bd_intern(path), v, 1, NOW); }
// 10 V on every source, so amps = watts / 10.
static void source(const char *group, const char *id, double watts) {
  char p[96];
  snprintf(p, sizeof p, "electrical.%s.%s.voltage", group, id);
  set(p, 10);
  snprintf(p, sizeof p, "electrical.%s.%s.current", group, id);
  set(p, watts / 10);
}
static void shunt_w(const char *id, double watts) {
  char p[96];
  snprintf(p, sizeof p, "electrical.resolved.batteries.%s.power", id);
  set(p, watts);
}
static void live(bool with_charger) {
  source("resolved.solar", "288", 200);
  source("dcdc", "1", 100);
  if (with_charger) source("resolved.chargers", "276", 0);
  source("alternators", "0", 40);   // charges START: not part of the house arithmetic
  source("dcdc", "2", 55);          // house -> nav converter, output side
  set("electrical.dcdc.2.input.voltage", 10);
  set("electrical.dcdc.2.input.current", 6);
  shunt_w("279", 50);
  shunt_w("280", 12);
  shunt_w("281", 5);
  set("electrical.resolved.batteries.279.capacity.stateOfCharge", 0.8);
  set("electrical.resolved.batteries.280.capacity.stateOfCharge", 0.95);
  set("electrical.resolved.batteries.281.capacity.stateOfCharge", 0.6);
}

static void test_compute_three_bank_scenario(void) {
  spec_topology();
  live(true);
  bind_power();
  power_topo_resolve(&b, &t);
  power_banks_view_t v;
  power_banks_compute(&b, &t, NOW, &v);
  TEST_ASSERT_TRUE(v.fig.house_in.ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 300, v.fig.house_in.w);
  TEST_ASSERT_TRUE(v.fig.house_load.ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 190, v.fig.house_load.w);
  TEST_ASSERT_FALSE(v.fig.house_load.est);
  TEST_ASSERT_TRUE(v.fig.nav_load.ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 50, v.fig.nav_load.w);
  TEST_ASSERT_TRUE(v.house_soc_ok && v.start_soc_ok && v.nav_soc_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.8, v.house_soc);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.95, v.start_soc);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.6, v.nav_soc);
}

static void test_compute_unbound_source_fails_house_in_not_nav(void) {
  spec_topology();
  live(false);   // the charger is not on the network
  bind_power();
  power_topo_resolve(&b, &t);
  power_banks_view_t v;
  power_banks_compute(&b, &t, NOW, &v);
  TEST_ASSERT_FALSE(v.fig.house_in.ok);
  TEST_ASSERT_FALSE(v.fig.house_load.ok);
  TEST_ASSERT_TRUE(v.fig.nav_load.ok);
}

static void test_compute_converter_input_not_published_estimates(void) {
  spec_topology();
  live(true);
  bind_power();
  power_topo_resolve(&b, &t);
  bd_set_invalid(bd_find("electrical.dcdc.2.input.current"));   // the Orion's input stops reporting
  power_banks_view_t v;
  power_banks_compute(&b, &t, NOW, &v);
  TEST_ASSERT_TRUE(v.fig.house_load.ok);
  TEST_ASSERT_TRUE(v.fig.house_load.est);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 300 - 50 - 55 / 0.95, v.fig.house_load.w);
}

static void test_compute_inactive_leaves_view_empty(void) {
  live(true);
  bind_power();
  power_topo_resolve(&b, &t);
  power_banks_view_t v;
  power_banks_compute(&b, &t, NOW, &v);
  TEST_ASSERT_FALSE(v.fig.house_in.ok);
  TEST_ASSERT_FALSE(v.house_soc_ok);
}

static void test_sources_text_per_bank(void) {
  spec_topology();
  live(true);
  bind_power();
  power_topo_resolve(&b, &t);
  char txt[96];
  power_bank_sources(&b, &t, BANK_START, NOW, "  ", txt, sizeof txt);
  TEST_ASSERT_EQUAL_STRING("FLOAT CHARGER  ALTERNATOR 40 W", txt);   // link order; the float side has no reading of its own
  power_bank_sources(&b, &t, BANK_HOUSE, NOW, "  ", txt, sizeof txt);
  TEST_ASSERT_EQUAL_STRING("CHARGER 0 W  SOLAR 200 W", txt);       // converters are arrows, not sources
  power_bank_sources(&b, &t, BANK_NAV, NOW, "  ", txt, sizeof txt);
  TEST_ASSERT_EQUAL_STRING("", txt);
}

static void test_sources_text_unbound_reads_fail(void) {
  spec_topology();
  live(false);
  bind_power();
  power_topo_resolve(&b, &t);
  char txt[96];
  power_bank_sources(&b, &t, BANK_HOUSE, NOW, " | ", txt, sizeof txt);
  TEST_ASSERT_EQUAL_STRING("CHARGER FAIL | SOLAR 200 W", txt);
}

static void test_sources_text_truncates_without_overflow(void) {
  spec_topology();
  live(true);
  bind_power();
  power_topo_resolve(&b, &t);
  char txt[12];
  power_bank_sources(&b, &t, BANK_HOUSE, NOW, "  ", txt, sizeof txt);
  TEST_ASSERT_TRUE(strlen(txt) < sizeof txt);
}

static void test_dest_of_a_source(void) {
  spec_topology();
  live(true);
  bind_power();
  power_topo_resolve(&b, &t);
  int from = 99;
  TEST_ASSERT_EQUAL_INT(BANK_HOUSE, power_topo_dest(&t, G_CHARGER, 0, &from));   // output 0 wins
  TEST_ASSERT_EQUAL_INT(-1, from);
  TEST_ASSERT_EQUAL_INT(BANK_NAV, power_topo_dest(&t, G_DCDC, 1, &from));   // dcdc 2 is the second dcdc instance
  TEST_ASSERT_EQUAL_INT(BANK_HOUSE, from);
  TEST_ASSERT_EQUAL_INT(-1, power_topo_dest(&t, G_SOLAR, 5, NULL));
}

static void test_banks_page_adds_one_page(void) {
  const role_def_t *pw = role_find("power");
  intern_all();
  bind_power();
  uint8_t plain = pw->pages(&b, SIZE_WIDE);
  TEST_ASSERT_EQUAL_UINT8(3, plain);   // Summary, Batteries, Charging
  spec_topology();
  bind_power();
  TEST_ASSERT_EQUAL_UINT8(4, pw->pages(&b, SIZE_WIDE));   // Summary, Banks, Batteries, Charging
  TEST_ASSERT_EQUAL_UINT8(5, pw->pages(&b, SIZE_SQUARE));   // five charging rows, four to a square page: two Charging pages
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_topo_inactive_without_banks);
  RUN_TEST(test_topo_resolves_instances);
  RUN_TEST(test_topo_unbound_source_keeps_link);
  RUN_TEST(test_topo_unbound_bank);
  RUN_TEST(test_topo_banks_without_links);
  RUN_TEST(test_compute_three_bank_scenario);
  RUN_TEST(test_compute_unbound_source_fails_house_in_not_nav);
  RUN_TEST(test_compute_converter_input_not_published_estimates);
  RUN_TEST(test_compute_inactive_leaves_view_empty);
  RUN_TEST(test_sources_text_per_bank);
  RUN_TEST(test_sources_text_unbound_reads_fail);
  RUN_TEST(test_sources_text_truncates_without_overflow);
  RUN_TEST(test_dest_of_a_source);
  RUN_TEST(test_banks_page_adds_one_page);
  return UNITY_END();
}
