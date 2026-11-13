#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "power_topo.h"

// Devices, by CAN NAME. Endpoints are published the way the resolver does: values plus a `ref` text leaf.
#define SHUNT "c046aa002cc001f9"    // house on .0, start on .1
#define NAVSH "c046aa002cc00201"
#define MPPT "c046a0002cc001f5"     // panel input .1 (solar group), battery side .0 (charger group)
#define PHOENIX "c046a0002cc001f8"  // two outputs .0 and .1, AC input .0
#define ALT "aa00000000000001"
#define ORION1 "aa00000000000002"
#define ORION2 "aa00000000000003"
#define THRUSTER "aa00000000000004"

static cfg_t cfg;
static cfg_role_t rc;
static role_binding_t b;
static power_topo_t t;

#define NOW 5000
static void set(const char *path, double v) { bd_set_number(bd_intern(path), v, 1, NOW); }
static void endpoint(const char *group, const char *id, const char *ref) {
  char p[96];
  snprintf(p, sizeof p, "electrical.resolved.%s.%s.ref", group, id);
  bd_set_text(bd_intern(p), ref, 1, NOW);
  snprintf(p, sizeof p, "electrical.resolved.%s.%s.label", group, id);   // the resolver publishes a label with every endpoint
  bd_set_text(bd_intern(p), group, 1, NOW);
  snprintf(p, sizeof p, "electrical.resolved.%s.%s.voltage", group, id);
  bd_intern(p);
}
// 10 V on an endpoint, so amps = watts / 10.
static void source(const char *group, const char *id, const char *ref, double watts) {
  endpoint(group, id, ref);
  char p[96];
  snprintf(p, sizeof p, "electrical.resolved.%s.%s.voltage", group, id);
  set(p, 10);
  snprintf(p, sizeof p, "electrical.resolved.%s.%s.current", group, id);
  set(p, watts / 10);
}
static void shunt_w(const char *id, double watts, double soc) {
  char p[96];
  snprintf(p, sizeof p, "electrical.resolved.batteries.%s.power", id);
  set(p, watts);
  snprintf(p, sizeof p, "electrical.resolved.batteries.%s.capacity.stateOfCharge", id);
  set(p, soc);
}
static void battery(const char *id, const char *ref) { endpoint("batteries", id, ref); }

static void add_bank(const char *id, const char *name, const char *kind, const char *battery) {
  cfg_bank_t *k = &rc.bank[rc.bank_count++];
  snprintf(k->id, sizeof k->id, "%s", id);
  snprintf(k->name, sizeof k->name, "%s", name);
  snprintf(k->kind, sizeof k->kind, "%s", kind);
  snprintf(k->battery, sizeof k->battery, "%s", battery);
}
static void add_flow(const char *device, const char *kind, const char *from, const char *to, uint8_t output) {
  cfg_flow_t *l = &rc.flow[rc.flow_count++];
  snprintf(l->device, sizeof l->device, "%s", device);
  snprintf(l->kind, sizeof l->kind, "%s", kind);
  snprintf(l->from, sizeof l->from, "%s", from);
  snprintf(l->to, sizeof l->to, "%s", to);
  l->output = output;
}
static void spec_topology(void) {
  add_bank("house", "HOUSE", "house", SHUNT ".0");
  add_bank("start", "START", "start", SHUNT ".1");
  add_bank("nav", "", "nav", NAVSH ".0");
  add_flow(MPPT, "solar", "", "house", 0);
  add_flow(PHOENIX, "shore", "", "house", 0);
  add_flow(PHOENIX, "shore", "", "start", 1);
  add_flow(ALT, "engine", "", "start", 0);
  add_flow(ORION1, "converter", "start", "house", 0);
  add_flow(ORION2, "converter", "house", "nav", 0);
}
static void bind_power(void) { role_bind(role_find("power"), &rc, &cfg.units, &b); }

// The fixture boat. `with_charger` false leaves the Phoenix off the network.
static void live(bool with_charger) {
  battery("h", SHUNT ".0");
  battery("s", SHUNT ".1");
  battery("n", NAVSH ".0");
  shunt_w("h", 50, 0.8);
  shunt_w("s", 12, 0.95);
  shunt_w("n", 5, 0.6);
  source("solar", "p", MPPT ".1", 210);        // panel side: not the flow's reading
  source("chargers", "m", MPPT ".0", 200);
  if (with_charger) {
    source("chargers", "c0", PHOENIX ".0", 0);
    source("chargers", "c1", PHOENIX ".1", 3);
    source("acin", "ac", PHOENIX ".0", 2);
  }
  source("alternators", "a", ALT ".0", 40);    // charges START: not part of the house arithmetic
  source("chargers", "o1", ORION1 ".0", 100);
  source("chargers", "o2", ORION2 ".0", 55);   // house -> nav converter, output side
}

void setUp(void) {
  bd_init();
  cfg_defaults(&cfg);
  memset(&rc, 0, sizeof rc);
  strcpy(rc.role, "power");
}
void tearDown(void) {}

static void test_topo_inactive_without_house(void) {
  live(true);
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_FALSE(t.active);
  TEST_ASSERT_EQUAL_UINT8(0, t.bank_count);
  add_bank("s", "START", "start", SHUNT ".1");   // banks, but none of them house
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_FALSE(t.active);
}

static void test_topo_binds_by_ref(void) {
  live(true);
  spec_topology();
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_TRUE(t.active);
  TEST_ASSERT_EQUAL_UINT8(3, t.bank_count);
  for (int k = 0; k < 3; k++) TEST_ASSERT_TRUE(t.bank[k].bound);
  TEST_ASSERT_EQUAL_STRING("h", b.g[G_BATTERY].inst_id[t.bank[0].inst]);
  TEST_ASSERT_EQUAL_STRING("s", b.g[G_BATTERY].inst_id[t.bank[1].inst]);   // same shunt, its other input
  TEST_ASSERT_EQUAL_STRING("n", b.g[G_BATTERY].inst_id[t.bank[2].inst]);
  TEST_ASSERT_EQUAL_STRING("NAV", t.bank[2].name);   // empty name falls back to the kind
  TEST_ASSERT_EQUAL_INT(0, t.house);
  TEST_ASSERT_EQUAL_INT(1, t.start);
  TEST_ASSERT_EQUAL_INT(2, t.nav);
  TEST_ASSERT_EQUAL_UINT8(6, t.flow_count);
  for (int n = 0; n < 6; n++) TEST_ASSERT_TRUE(t.flow[n].bound);
  TEST_ASSERT_EQUAL_UINT8(G_CHARGER, t.flow[0].g);                // a solar flow reads the output side, not the panel
  TEST_ASSERT_EQUAL_STRING("m", b.g[G_CHARGER].inst_id[t.flow[0].i]);
  TEST_ASSERT_EQUAL_INT8(0, t.flow[0].to);
  TEST_ASSERT_EQUAL_INT8(-1, t.flow[0].from);
  TEST_ASSERT_EQUAL_INT8(1, t.flow[4].from);
  TEST_ASSERT_EQUAL_INT8(0, t.flow[4].to);
  TEST_ASSERT_EQUAL_UINT8(FLOW_CONVERTER, t.flow[5].kind);
  TEST_ASSERT_EQUAL_INT8(0, t.flow[5].from);
  TEST_ASSERT_EQUAL_INT8(2, t.flow[5].to);
}

static void test_topo_unseen_battery_unbound_others_ok(void) {
  live(true);
  spec_topology();
  bd_set_text(bd_find("electrical.resolved.batteries.n.ref"), "ffffffffffffffff.0", 1, NOW);   // the nav shunt's NAME is not the one configured
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_TRUE(t.active);
  TEST_ASSERT_FALSE(t.bank[2].bound);
  TEST_ASSERT_TRUE(t.bank[0].bound);
  TEST_ASSERT_TRUE(t.bank[1].bound);
  power_banks_view_t v;
  power_banks_compute(&b, &t, NOW, &v);
  TEST_ASSERT_FALSE(v.bank[2].fig.load.ok);
  TEST_ASSERT_FALSE(v.bank[2].soc_ok);
  TEST_ASSERT_TRUE(v.bank[0].fig.in.ok);   // house does not need the nav shunt
}

static void test_topo_two_banks_of_one_kind(void) {
  live(true);
  add_bank("house", "HOUSE", "house", SHUNT ".0");
  add_bank("start-port", "PORT", "start", SHUNT ".1");
  add_bank("start-stbd", "STBD", "start", NAVSH ".0");
  add_flow(ALT, "engine", "", "start-stbd", 0);
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_EQUAL_UINT8(3, t.bank_count);
  TEST_ASSERT_EQUAL_INT(1, t.start);   // the first of the kind
  TEST_ASSERT_EQUAL_INT8(2, t.flow[0].to);
  TEST_ASSERT_EQUAL_INT(2, power_topo_bank(&t, "start-stbd"));
  TEST_ASSERT_EQUAL_INT(-1, power_topo_bank(&t, "nope"));
}

static void test_topo_every_kind(void) {
  live(true);
  source("chargers", "t", THRUSTER ".0", 0);
  battery("t", THRUSTER ".0");
  battery("w", ORION1 ".5");
  battery("g", ORION2 ".5");
  battery("o", ALT ".5");
  add_bank("house", "HOUSE", "house", SHUNT ".0");
  add_bank("start", "START", "start", SHUNT ".1");
  add_bank("nav", "NAV", "nav", NAVSH ".0");
  add_bank("bow", "BOW", "thruster", THRUSTER ".0");
  add_bank("wl", "WINDLASS", "windlass", ORION1 ".5");
  add_bank("gen", "GENSET", "genset", ORION2 ".5");
  add_bank("odd", "ODD JOB", "other", ALT ".5");
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_EQUAL_UINT8(7, t.bank_count);
  uint8_t kinds[] = {BK_HOUSE, BK_START, BK_NAV, BK_THRUSTER, BK_WINDLASS, BK_GENSET, BK_OTHER};
  for (int k = 0; k < 7; k++) {
    TEST_ASSERT_EQUAL_UINT8(kinds[k], t.bank[k].kind);
    TEST_ASSERT_TRUE(t.bank[k].bound);
  }
  TEST_ASSERT_EQUAL_STRING("ODD JOB", t.bank[6].name);
}

static void test_topo_two_outputs_two_flows(void) {
  live(true);
  spec_topology();
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_EQUAL_STRING("c0", b.g[G_CHARGER].inst_id[t.flow[1].i]);   // output 0, by instance order
  TEST_ASSERT_EQUAL_STRING("c1", b.g[G_CHARGER].inst_id[t.flow[2].i]);
  TEST_ASSERT_EQUAL_INT8(0, t.flow[1].to);
  TEST_ASSERT_EQUAL_INT8(1, t.flow[2].to);
}

static void test_topo_engine_flow_reads_alternator(void) {
  live(true);
  spec_topology();
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_EQUAL_UINT8(G_ALTERNATOR, t.flow[3].g);
  TEST_ASSERT_TRUE(t.flow[3].bound);
}

static void test_topo_unseen_flow_keeps_place(void) {
  live(false);   // the Phoenix is not on the network
  spec_topology();
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_EQUAL_UINT8(6, t.flow_count);
  TEST_ASSERT_FALSE(t.flow[1].bound);
  TEST_ASSERT_FALSE(t.flow[2].bound);
  TEST_ASSERT_TRUE(t.flow[0].bound);
}

static void test_topo_counts_unassigned(void) {
  live(true);
  battery("x", "bb00000000000001.0");   // a battery nobody placed
  add_bank("house", "HOUSE", "house", SHUNT ".0");
  add_flow(MPPT, "solar", "", "house", 0);
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_EQUAL_UINT8(3, t.unassigned_batt);   // s, n and x
  // chargers: m is placed; c0, c1, o1, o2 and the alternator are not
  TEST_ASSERT_EQUAL_UINT8(5, t.unassigned_dev);
}

// ---- power_banks_compute: the spec topology with known samples ----

static void test_compute_house_load_excludes_nav_and_converters(void) {
  spec_topology();
  live(true);
  bind_power();
  power_topo_resolve(&b, &t);
  power_banks_view_t v;
  power_banks_compute(&b, &t, NOW, &v);
  const power_bank_view_t *h = &v.bank[0], *s = &v.bank[1], *n = &v.bank[2];
  TEST_ASSERT_TRUE(h->fig.in.ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 300, h->fig.in.w);   // MPPT 200 + Orion1 100 + Phoenix output 0 (0 W)
  TEST_ASSERT_TRUE(h->fig.load.ok);
  TEST_ASSERT_TRUE(h->fig.load.est);                   // Orion2's input is not reported
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 300 - 50 - 55 / 0.95, h->fig.load.w);
  TEST_ASSERT_TRUE(n->fig.load.ok);
  TEST_ASSERT_FALSE(n->fig.load.est);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 50, n->fig.load.w);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 43, s->fig.in.w);    // Phoenix output 1 (3 W) + alternator 40 W
  TEST_ASSERT_TRUE(h->soc_ok && s->soc_ok && n->soc_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.8, h->soc);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.95, s->soc);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.6, n->soc);
}

static void test_compute_unseen_flow_fails_in_not_vanishes(void) {
  spec_topology();
  live(false);
  bind_power();
  power_topo_resolve(&b, &t);
  power_banks_view_t v;
  power_banks_compute(&b, &t, NOW, &v);
  TEST_ASSERT_FALSE(v.bank[0].fig.in.ok);
  TEST_ASSERT_FALSE(v.bank[0].fig.load.ok);
  TEST_ASSERT_TRUE(v.bank[2].fig.load.ok);   // a failure in one bank never fails another
}

static void test_compute_inactive_leaves_view_empty(void) {
  live(true);
  bind_power();
  power_topo_resolve(&b, &t);
  power_banks_view_t v;
  power_banks_compute(&b, &t, NOW, &v);
  TEST_ASSERT_FALSE(v.bank[0].fig.in.ok);
  TEST_ASSERT_FALSE(v.bank[0].soc_ok);
}

static void test_sources_text_per_bank(void) {
  spec_topology();
  live(true);
  bind_power();
  power_topo_resolve(&b, &t);
  char txt[96];
  power_bank_sources(&b, &t, 1, NOW, "  ", txt, sizeof txt);
  TEST_ASSERT_EQUAL_STRING("CHARGER 2 3 W  ALTERNATOR 40 W", txt);   // flow order; converters are arrows, not sources
  power_bank_sources(&b, &t, 0, NOW, "  ", txt, sizeof txt);
  TEST_ASSERT_EQUAL_STRING("SOLAR 200 W  CHARGER 0 W", txt);
  power_bank_sources(&b, &t, 2, NOW, "  ", txt, sizeof txt);
  TEST_ASSERT_EQUAL_STRING("", txt);
}

static void test_sources_text_unseen_reads_fail(void) {
  spec_topology();
  live(false);
  bind_power();
  power_topo_resolve(&b, &t);
  char txt[96];
  power_bank_sources(&b, &t, 0, NOW, " | ", txt, sizeof txt);
  TEST_ASSERT_EQUAL_STRING("SOLAR 200 W | CHARGER FAIL", txt);
}

static void test_sources_text_truncates_without_overflow(void) {
  spec_topology();
  live(true);
  bind_power();
  power_topo_resolve(&b, &t);
  char txt[12];
  power_bank_sources(&b, &t, 0, NOW, "  ", txt, sizeof txt);
  TEST_ASSERT_TRUE(strlen(txt) < sizeof txt);
}

static void test_dest_of_a_source(void) {
  spec_topology();
  live(true);
  bind_power();
  power_topo_resolve(&b, &t);
  int from = 99;
  uint8_t mppt = 0, orion2 = 0, panel = 0;
  for (uint8_t i = 0; i < b.g[G_CHARGER].inst_count; i++) {
    if (!strcmp(b.g[G_CHARGER].inst_id[i], "m")) mppt = i;
    if (!strcmp(b.g[G_CHARGER].inst_id[i], "o2")) orion2 = i;
  }
  TEST_ASSERT_EQUAL_INT(0, power_topo_dest(&b, &t, G_CHARGER, mppt, &from));
  TEST_ASSERT_EQUAL_INT(-1, from);
  TEST_ASSERT_EQUAL_INT(2, power_topo_dest(&b, &t, G_CHARGER, orion2, &from));
  TEST_ASSERT_EQUAL_INT(0, from);
  TEST_ASSERT_EQUAL_INT(0, power_topo_dest(&b, &t, G_SOLAR, panel, NULL));   // the panel input follows its device's flow
  TEST_ASSERT_EQUAL_INT(-1, power_topo_dest(&b, &t, G_SOLAR, 5, NULL));
}

static void test_banks_page_adds_one_page(void) {
  const role_def_t *pw = role_find("power");
  live(true);
  bind_power();
  uint8_t plain = pw->pages(&b, SIZE_WIDE);
  TEST_ASSERT_EQUAL_UINT8(4, plain);   // Summary, Batteries, two Charging (seven rows, six a page)
  spec_topology();
  bind_power();
  // seven charging rows (solar panel, five chargers and converters, alternator): two pages wide (six a page) and square (four)
  TEST_ASSERT_EQUAL_UINT8(5, pw->pages(&b, SIZE_WIDE));   // Summary, Banks, Batteries, two Charging
  TEST_ASSERT_EQUAL_UINT8(5, pw->pages(&b, SIZE_SQUARE));
}

// ---- shore means AC present ----

static void set_at(const char *path, double v, int64_t at) { bd_set_number(bd_intern(path), v, 1, at); }

static void test_shore_present_from_ac_with_zero_output(void) {
  spec_topology();
  live(true);   // the Phoenix reports AC current (2 A) while its house output is 0 W
  bind_power();
  power_topo_resolve(&b, &t);
  double w = -1;
  TEST_ASSERT_EQUAL_INT(SHORE_PRESENT, power_shore(&b, &t, NOW, &w));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 3, w);   // both outputs: 0 W and 3 W
}

static void test_shore_absent_when_ac_endpoint_stale(void) {
  spec_topology();
  live(true);
  set_at("electrical.resolved.acin.ac.current", 2, NOW - 2 * SHORE_FRESH_MS);
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_EQUAL_INT(SHORE_ABSENT, power_shore(&b, &t, NOW, NULL));
}

static void test_shore_from_inverter_source(void) {
  spec_topology();
  live(false);   // no Phoenix on the network, so no AC endpoint
  bd_set_text(bd_intern("electrical.inverters.275.acin.acState.acSource"), "grid", 1, NOW);
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_EQUAL_INT(SHORE_PRESENT, power_shore(&b, &t, NOW, NULL));
}

static void test_shore_inferred_from_output(void) {
  spec_topology();
  live(true);
  bd_set_text(bd_find("electrical.resolved.acin.ac.ref"), "bb00000000000009.0", 1, NOW);   // another device's AC input: the Phoenix sends none
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_EQUAL_INT(SHORE_INFERRED_PRESENT, power_shore(&b, &t, NOW, NULL));   // output 1 carries 3 W
  set("electrical.resolved.chargers.c1.current", 0);
  TEST_ASSERT_EQUAL_INT(SHORE_INFERRED_ABSENT, power_shore(&b, &t, NOW, NULL));
}

static void test_solar_flow_never_counts_as_shore(void) {
  live(true);
  add_bank("house", "HOUSE", "house", SHUNT ".0");
  add_flow(MPPT, "solar", "", "house", 0);
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_EQUAL_INT(SHORE_UNKNOWN, power_shore(&b, &t, NOW, NULL));
}

// ---- power_rows: what Setup > Power lists ----

static const ui_power_row_t *row_of(const ui_power_row_t *rows, uint8_t n, const char *ref) {
  for (uint8_t i = 0; i < n; i++)
    if (!strcmp(rows[i].ref, ref)) return &rows[i];
  return NULL;
}

static void test_power_rows_list_seen_endpoints(void) {
  live(true);
  battery("noname", "");   // a shunt whose identity has not arrived
  bd_set_text(bd_find("electrical.resolved.batteries.noname.ref"), "", 1, NOW);
  bd_set_text(bd_intern("electrical.resolved.batteries.h.label"), "SmartShunt", 1, NOW);
  set("electrical.resolved.batteries.h.voltage", 10);
  bd_set_text(bd_intern("electrical.resolved.batteries.noname.label"), "Battery", 1, NOW);
  ui_power_row_t rows[UI_POWER_ROWS];
  uint8_t n = power_rows(rows, UI_POWER_ROWS);
  TEST_ASSERT_EQUAL_UINT8(10, n);   // four batteries, five chargers and converters, one alternator; no panel input
  const ui_power_row_t *h = row_of(rows, n, SHUNT ".0");
  TEST_ASSERT_NOT_NULL(h);
  TEST_ASSERT_EQUAL_UINT8(UI_PW_BATTERY, h->fn);
  TEST_ASSERT_EQUAL_STRING("SmartShunt", h->label);
  TEST_ASSERT_TRUE(h->value_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 10, h->value);
  const ui_power_row_t *m = row_of(rows, n, MPPT ".0");
  TEST_ASSERT_NOT_NULL(m);
  TEST_ASSERT_EQUAL_UINT8(UI_PW_OUTPUT, m->fn);
  TEST_ASSERT_EQUAL_STRING("solar", m->proposed);   // its device also has a panel input
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 200, m->value);   // watts
  TEST_ASSERT_EQUAL_STRING("shore", row_of(rows, n, PHOENIX ".1")->proposed);
  const ui_power_row_t *a = row_of(rows, n, ALT ".0");
  TEST_ASSERT_EQUAL_STRING("engine", a->proposed);
  TEST_ASSERT_EQUAL_UINT8(1, a->group);
  const ui_power_row_t *nn = row_of(rows, n, "");
  TEST_ASSERT_NOT_NULL(nn);
  TEST_ASSERT_EQUAL_STRING("Battery", nn->label);
}

static void test_power_rows_skip_withdrawn_endpoints(void) {
  live(true);
  bd_set_invalid(bd_find("electrical.resolved.batteries.n.label"));   // the resolver re-keyed or merged it away
  ui_power_row_t rows[UI_POWER_ROWS];
  uint8_t n = power_rows(rows, UI_POWER_ROWS);
  TEST_ASSERT_NULL(row_of(rows, n, NAVSH ".0"));
  TEST_ASSERT_NOT_NULL(row_of(rows, n, SHUNT ".0"));
}

static void test_other_flow_on_an_alternator_output_binds(void) {
  live(true);
  add_bank("house", "HOUSE", "house", SHUNT ".0");
  add_flow(ALT, "other", "", "house", 0);   // a generator's charger the resolver calls an alternator
  bind_power();
  power_topo_resolve(&b, &t);
  TEST_ASSERT_TRUE(t.flow[0].bound);
  TEST_ASSERT_EQUAL_UINT8(G_ALTERNATOR, t.flow[0].g);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_topo_inactive_without_house);
  RUN_TEST(test_topo_binds_by_ref);
  RUN_TEST(test_topo_unseen_battery_unbound_others_ok);
  RUN_TEST(test_topo_two_banks_of_one_kind);
  RUN_TEST(test_topo_every_kind);
  RUN_TEST(test_topo_two_outputs_two_flows);
  RUN_TEST(test_topo_engine_flow_reads_alternator);
  RUN_TEST(test_topo_unseen_flow_keeps_place);
  RUN_TEST(test_topo_counts_unassigned);
  RUN_TEST(test_compute_house_load_excludes_nav_and_converters);
  RUN_TEST(test_compute_unseen_flow_fails_in_not_vanishes);
  RUN_TEST(test_compute_inactive_leaves_view_empty);
  RUN_TEST(test_sources_text_per_bank);
  RUN_TEST(test_sources_text_unseen_reads_fail);
  RUN_TEST(test_sources_text_truncates_without_overflow);
  RUN_TEST(test_dest_of_a_source);
  RUN_TEST(test_banks_page_adds_one_page);
  RUN_TEST(test_shore_present_from_ac_with_zero_output);
  RUN_TEST(test_shore_absent_when_ac_endpoint_stale);
  RUN_TEST(test_shore_from_inverter_source);
  RUN_TEST(test_shore_inferred_from_output);
  RUN_TEST(test_solar_flow_never_counts_as_shore);
  RUN_TEST(test_power_rows_list_seen_endpoints);
  RUN_TEST(test_power_rows_skip_withdrawn_endpoints);
  RUN_TEST(test_other_flow_on_an_alternator_output_binds);
  return UNITY_END();
}
