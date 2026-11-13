#include <math.h>
#include <string.h>
#include "unity.h"
#include "power_calc.h"

#define NOW 100000

static bd_sample_t num(double v, int64_t age_ms) {
  return (bd_sample_t){.kind = BD_NUMBER, .num = v, .valid = true, .rx_ms = NOW - age_ms, .source = 1};
}
static bd_sample_t bad(void) { bd_sample_t s = num(0, 0); s.valid = false; return s; }

void setUp(void) {}
void tearDown(void) {}

static void test_in_sums_positive_sources(void) {
  bd_sample_t p = num(300, 0), v = num(13.6, 0), i = num(10, 0), b = num(-50, 0);
  power_src_t src[2] = {{.p = &p}, {.v = &v, .i = &i}};
  power_src_t batt = {.p = &b};
  power_summary_t r = power_summarize(src, 2, &batt, NULL, NULL, 0, NOW);
  TEST_ASSERT_TRUE(r.in_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 436, r.in_w);
}

static void test_negative_charger_counts_as_load(void) {
  // Negative contributions are already inside the battery's net: excluded from IN, not added to load.
  bd_sample_t p = num(200, 0), v = num(13, 0), i = num(-20, 0), b = num(-100, 0);
  power_src_t src[2] = {{.p = &p}, {.v = &v, .i = &i}};
  power_src_t batt = {.p = &b};
  power_summary_t r = power_summarize(src, 2, &batt, NULL, NULL, 0, NOW);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 200, r.in_w);
  TEST_ASSERT_TRUE(r.load_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 300, r.load_w);
}

static void test_invalid_source_fails_in_and_load(void) {
  bd_sample_t p = num(300, 0), q = bad(), b = num(-50, 0);
  power_src_t src[2] = {{.p = &p}, {.p = &q}};
  power_src_t batt = {.p = &b};
  power_summary_t r = power_summarize(src, 2, &batt, NULL, NULL, 0, NOW);
  TEST_ASSERT_FALSE(r.in_ok);
  TEST_ASSERT_FALSE(r.load_ok);
  TEST_ASSERT_TRUE(r.batt_ok);
}

static void test_zero_source_is_valid(void) {
  bd_sample_t p = num(0, 0), b = num(0, 0);
  power_src_t src[1] = {{.p = &p}};
  power_src_t batt = {.p = &b};
  power_summary_t r = power_summarize(src, 1, &batt, NULL, NULL, 0, NOW);
  TEST_ASSERT_TRUE(r.in_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0, r.in_w);
}

static void test_stale_source_fails_with_max_age(void) {
  bd_sample_t p = num(300, 20000), b = num(-50, 0);
  power_src_t src[1] = {{.p = &p}};
  power_src_t batt = {.p = &b};
  TEST_ASSERT_FALSE(power_summarize(src, 1, &batt, NULL, NULL, 15000, NOW).in_ok);
  TEST_ASSERT_TRUE(power_summarize(src, 1, &batt, NULL, NULL, 0, NOW).in_ok);
}

static void test_dc_system_preferred(void) {
  bd_sample_t p = num(300, 0), b = num(-50, 0), d = num(180, 0);
  power_src_t src[1] = {{.p = &p}};
  power_src_t batt = {.p = &b};
  power_summary_t r = power_summarize(src, 1, &batt, NULL, &d, 0, NOW);
  TEST_ASSERT_TRUE(r.load_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 180, r.load_w);
  d = bad();
  r = power_summarize(src, 1, &batt, NULL, &d, 0, NOW);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 350, r.load_w);
}

static void test_batt_invalid_fails_batt_load_and_soc(void) {
  bd_sample_t p = num(300, 0), b = bad(), s = num(0.8, 0);
  power_src_t src[1] = {{.p = &p}};
  power_src_t batt = {.p = &b};
  power_summary_t r = power_summarize(src, 1, &batt, &s, NULL, 0, NOW);
  TEST_ASSERT_FALSE(r.batt_ok);
  TEST_ASSERT_FALSE(r.load_ok);
  TEST_ASSERT_TRUE(r.in_ok);
  TEST_ASSERT_FALSE(r.soc_ok);   // soc is the primary bank's: no bank, no soc
  bd_sample_t b2 = num(-50, 0), sbad = bad();
  batt.p = &b2;
  TEST_ASSERT_FALSE(power_summarize(src, 1, &batt, &sbad, NULL, 0, NOW).soc_ok);
  TEST_ASSERT_FALSE(power_summarize(src, 1, &batt, NULL, NULL, 0, NOW).soc_ok);
  r = power_summarize(src, 1, &batt, &s, NULL, 0, NOW);
  TEST_ASSERT_TRUE(r.soc_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.8, r.soc);
}

static void test_dc_system_stands_alone(void) {
  bd_sample_t p = num(300, 0), q = bad(), b = bad(), d = num(180, 0);
  power_src_t src[2] = {{.p = &p}, {.p = &q}};
  power_src_t batt = {.p = &b};
  power_summary_t r = power_summarize(src, 2, &batt, NULL, &d, 0, NOW);
  TEST_ASSERT_TRUE(r.load_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 180, r.load_w);
  TEST_ASSERT_FALSE(r.in_ok);
  TEST_ASSERT_FALSE(r.batt_ok);
}

static void test_no_sources_fails_in(void) {
  bd_sample_t b = num(-50, 0);
  power_src_t batt = {.p = &b};
  power_summary_t r = power_summarize(NULL, 0, &batt, NULL, NULL, 0, NOW);
  TEST_ASSERT_FALSE(r.in_ok);
  TEST_ASSERT_FALSE(r.load_ok);
}

static void test_primary_choice(void) {
  const char ids[2][24] = {"256", "279"};
  bool bound[2] = {false, true};
  TEST_ASSERT_EQUAL(1, power_primary(ids, bound, 2, ""));
  TEST_ASSERT_EQUAL(0, power_primary(ids, bound, 2, "256"));
  TEST_ASSERT_EQUAL(1, power_primary(ids, bound, 2, "999"));
  bool none[2] = {false, false};
  TEST_ASSERT_EQUAL(0, power_primary(ids, none, 2, ""));
}

static void test_ttg_null_while_charging(void) {
  bd_sample_t t = bad(), c = num(5, 0), cn = num(-5, 0), ci = bad(), tv = num(7200, 0);
  TEST_ASSERT_EQUAL(TTG_CHARGING, power_time_remaining(&t, &c, 0, NOW));
  TEST_ASSERT_EQUAL(TTG_FAIL, power_time_remaining(&t, &cn, 0, NOW));
  TEST_ASSERT_EQUAL(TTG_FAIL, power_time_remaining(&t, &ci, 0, NOW));
  TEST_ASSERT_EQUAL(TTG_VALUE, power_time_remaining(&tv, &c, 0, NOW));
  TEST_ASSERT_EQUAL(TTG_FAIL, power_time_remaining(NULL, NULL, 0, NOW));
  bd_sample_t c0 = num(0, 0);
  TEST_ASSERT_EQUAL(TTG_CHARGING, power_time_remaining(&t, &c0, 0, NOW));
}

static void test_flows_charging(void) {
  power_summary_t r = {.in_ok = true, .in_w = 403, .batt_ok = true, .batt_w = 203, .load_ok = true, .load_w = 200};
  power_flows_t f = power_flows(&r);
  TEST_ASSERT_TRUE(f.to_load_ok);
  TEST_ASSERT_TRUE(f.batt_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 200, f.to_load);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 203, f.to_batt);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0, f.from_batt);
}

static void test_flows_discharging(void) {
  power_summary_t r = {.in_ok = true, .in_w = 100, .batt_ok = true, .batt_w = -150, .load_ok = true, .load_w = 250};
  power_flows_t f = power_flows(&r);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 100, f.to_load);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0, f.to_batt);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 150, f.from_batt);
}

static void test_flows_battery_alone_feeds_load(void) {
  power_summary_t r = {.in_ok = true, .in_w = 0, .batt_ok = true, .batt_w = -250, .load_ok = true, .load_w = 250};
  power_flows_t f = power_flows(&r);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0, f.to_load);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 250, f.from_batt);
}

static void test_flows_load_never_negative(void) {
  // dc_system reads less than the battery's discharge: the source share clamps at zero.
  power_summary_t r = {.batt_ok = true, .batt_w = -300, .load_ok = true, .load_w = 250};
  power_flows_t f = power_flows(&r);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0, f.to_load);
}

static void test_flows_unknown_battery(void) {
  power_summary_t r = {.load_ok = true, .load_w = 180};
  power_flows_t f = power_flows(&r);
  TEST_ASSERT_FALSE(f.batt_ok);
  TEST_ASSERT_TRUE(f.to_load_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 180, f.to_load);
}

static void test_flows_unknown_load(void) {
  power_summary_t r = {.batt_ok = true, .batt_w = 40};
  power_flows_t f = power_flows(&r);
  TEST_ASSERT_FALSE(f.to_load_ok);
  TEST_ASSERT_TRUE(f.batt_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 40, f.to_batt);
}

// ---- per-bank figures ----
// Spec topology: house IN = solar + start->house converter (+ charger); house LOAD excludes what the house->nav
// converter draws (its input, else output / 0.95 marked EST); nav LOAD = the converter's output minus the nav net.

typedef struct {
  bd_sample_t into[3], hb, nav_out, nav_in, nb;
  power_src_t into_src[3], hb_src, out_src, in_src, nb_src;
  power_conv_t conv;
  power_bank_in_t house, nav;
} bank_fx_t;

static void fx(bank_fx_t *f, double solar, double start_dcdc, double charger, double house_net,
               double nav_out, double nav_in, double nav_net) {
  f->into[0] = num(solar, 0); f->into[1] = num(start_dcdc, 0); f->into[2] = num(charger, 0);
  f->hb = num(house_net, 0); f->nav_out = num(nav_out, 0); f->nav_in = num(nav_in, 0); f->nb = num(nav_net, 0);
  for (int k = 0; k < 3; k++) f->into_src[k] = (power_src_t){.p = &f->into[k]};
  f->hb_src = (power_src_t){.p = &f->hb};
  f->out_src = (power_src_t){.p = &f->nav_out};
  f->in_src = (power_src_t){.p = &f->nav_in};
  f->nb_src = (power_src_t){.p = &f->nb};
  f->conv = (power_conv_t){.out = &f->out_src, .in = &f->in_src};
  f->house = (power_bank_in_t){.batt = &f->hb_src, .into = f->into_src, .n_into = 3, .drawn = &f->conv, .n_drawn = 1};
  f->nav = (power_bank_in_t){.batt = &f->nb_src, .into = &f->out_src, .n_into = 1};
}

static void test_bank_steady_state(void) {
  bank_fx_t f;
  fx(&f, 200, 100, 0, 50, 55, 60, 5);
  power_bank_out_t h = power_summarize_bank(&f.house, 0, NOW), n = power_summarize_bank(&f.nav, 0, NOW);
  TEST_ASSERT_TRUE(h.in.ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 300, h.in.w);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 50, h.net.w);
  TEST_ASSERT_TRUE(h.load.ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 190, h.load.w);
  TEST_ASSERT_FALSE(h.load.est);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 60, h.feed.w);
  TEST_ASSERT_TRUE(n.load.ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 50, n.load.w);
  TEST_ASSERT_FALSE(n.load.est);
}

static void test_bank_nav_discharging(void) {
  bank_fx_t f;
  fx(&f, 0, 0, 0, -45, 40, 45, -20);
  power_bank_out_t h = power_summarize_bank(&f.house, 0, NOW), n = power_summarize_bank(&f.nav, 0, NOW);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 60, n.load.w);   // 40 W from the converter + 20 W from the nav battery
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0, h.in.w);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0, h.load.w);    // 0 - (-45) - 45
}

static void test_two_banks_independent_failure(void) {
  bank_fx_t f;
  fx(&f, 200, 100, 0, 50, 55, 60, 5);
  f.nb = bad();
  power_bank_out_t h = power_summarize_bank(&f.house, 0, NOW), n = power_summarize_bank(&f.nav, 0, NOW);
  TEST_ASSERT_FALSE(n.load.ok);
  TEST_ASSERT_TRUE(h.load.ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 190, h.load.w);
}

static void test_converter_input_missing_estimates_and_flags_est(void) {
  bank_fx_t f;
  fx(&f, 200, 100, 0, 50, 57, 0, 5);
  f.nav_in = bad();   // the Orion's input is not reporting
  power_bank_out_t h = power_summarize_bank(&f.house, 0, NOW), n = power_summarize_bank(&f.nav, 0, NOW);
  TEST_ASSERT_TRUE(h.load.ok);
  TEST_ASSERT_TRUE(h.load.est);
  TEST_ASSERT_TRUE(h.feed.est);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 57 / 0.95, h.feed.w);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 300 - 50 - 57 / 0.95, h.load.w);
  TEST_ASSERT_TRUE(n.load.ok);
  TEST_ASSERT_FALSE(n.load.est);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 52, n.load.w);
}

static void test_converter_input_null_pointer_estimates(void) {
  bank_fx_t f;
  fx(&f, 200, 100, 0, 50, 57, 0, 5);
  f.conv.in = NULL;
  power_bank_out_t h = power_summarize_bank(&f.house, 0, NOW);
  TEST_ASSERT_TRUE(h.load.est);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 300 - 50 - 57 / 0.95, h.load.w);
}

static void test_converter_output_missing_fails_nav_only(void) {
  bank_fx_t f;
  fx(&f, 200, 100, 0, 50, 55, 60, 5);
  f.nav_out = bad();   // the input is still good, so house load does not need the output
  power_bank_out_t h = power_summarize_bank(&f.house, 0, NOW), n = power_summarize_bank(&f.nav, 0, NOW);
  TEST_ASSERT_FALSE(n.load.ok);
  TEST_ASSERT_TRUE(h.load.ok);
  TEST_ASSERT_FALSE(h.load.est);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 190, h.load.w);
}

static void test_converter_input_and_output_missing_fails_both(void) {
  bank_fx_t f;
  fx(&f, 200, 100, 0, 50, 55, 60, 5);
  f.nav_out = bad();
  f.nav_in = bad();
  power_bank_out_t h = power_summarize_bank(&f.house, 0, NOW), n = power_summarize_bank(&f.nav, 0, NOW);
  TEST_ASSERT_FALSE(n.load.ok);
  TEST_ASSERT_FALSE(h.load.ok);
  TEST_ASSERT_FALSE(h.feed.ok);
  TEST_ASSERT_TRUE(h.in.ok);
}

static void test_in_fails_if_any_source_bad(void) {
  bank_fx_t f;
  fx(&f, 200, 100, 0, 50, 55, 60, 5);
  f.into[1] = bad();
  power_bank_out_t h = power_summarize_bank(&f.house, 0, NOW), n = power_summarize_bank(&f.nav, 0, NOW);
  TEST_ASSERT_FALSE(h.in.ok);
  TEST_ASSERT_FALSE(h.load.ok);
  TEST_ASSERT_TRUE(n.load.ok);   // nav does not depend on house sources
}

static void test_no_sources_load_fails(void) {
  bank_fx_t f;
  fx(&f, 0, 0, 0, 50, 55, 60, 5);
  f.house.n_into = 0;
  power_bank_out_t h = power_summarize_bank(&f.house, 0, NOW);
  TEST_ASSERT_FALSE(h.in.ok);
  TEST_ASSERT_FALSE(h.load.ok);
  TEST_ASSERT_TRUE(h.net.ok);
}

static void test_shunt_missing_fails_load_not_in(void) {
  bank_fx_t f;
  fx(&f, 200, 100, 0, 50, 55, 60, 5);
  f.house.batt = NULL;
  power_bank_out_t h = power_summarize_bank(&f.house, 0, NOW);
  TEST_ASSERT_TRUE(h.in.ok);
  TEST_ASSERT_FALSE(h.net.ok);
  TEST_ASSERT_FALSE(h.load.ok);
}

static void test_nothing_drawn_from_bank(void) {
  bank_fx_t f;
  fx(&f, 200, 100, 0, 50, 55, 60, 5);
  f.house.n_drawn = 0;
  power_bank_out_t h = power_summarize_bank(&f.house, 0, NOW);
  TEST_ASSERT_TRUE(h.feed.ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0, h.feed.w);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 250, h.load.w);
}

static void test_negative_source_adds_nothing_to_in(void) {
  bank_fx_t f;
  fx(&f, 200, -30, 0, 50, 55, 60, 5);
  power_bank_out_t h = power_summarize_bank(&f.house, 0, NOW);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 200, h.in.w);
}

static void test_valid_zero_is_a_reading(void) {
  bank_fx_t f;
  fx(&f, 0, 0, 0, 0, 0, 0, 0);
  power_bank_out_t h = power_summarize_bank(&f.house, 0, NOW);
  TEST_ASSERT_TRUE(h.in.ok);
  TEST_ASSERT_TRUE(h.load.ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0, h.load.w);
}

static void test_bank_of_other_kind_uses_same_formula(void) {   // a thruster bank charged by a converter from house
  bank_fx_t f;
  fx(&f, 0, 0, 0, 0, 80, 85, -400);
  power_bank_out_t t = power_summarize_bank(&f.nav, 0, NOW);   // stands in for any downstream bank
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 480, t.load.w);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_in_sums_positive_sources);
  RUN_TEST(test_negative_charger_counts_as_load);
  RUN_TEST(test_invalid_source_fails_in_and_load);
  RUN_TEST(test_zero_source_is_valid);
  RUN_TEST(test_stale_source_fails_with_max_age);
  RUN_TEST(test_dc_system_preferred);
  RUN_TEST(test_batt_invalid_fails_batt_load_and_soc);
  RUN_TEST(test_primary_choice);
  RUN_TEST(test_ttg_null_while_charging);
  RUN_TEST(test_dc_system_stands_alone);
  RUN_TEST(test_no_sources_fails_in);
  RUN_TEST(test_flows_charging);
  RUN_TEST(test_flows_discharging);
  RUN_TEST(test_flows_battery_alone_feeds_load);
  RUN_TEST(test_flows_load_never_negative);
  RUN_TEST(test_flows_unknown_battery);
  RUN_TEST(test_flows_unknown_load);
  RUN_TEST(test_bank_steady_state);
  RUN_TEST(test_bank_nav_discharging);
  RUN_TEST(test_two_banks_independent_failure);
  RUN_TEST(test_converter_input_missing_estimates_and_flags_est);
  RUN_TEST(test_converter_input_null_pointer_estimates);
  RUN_TEST(test_converter_output_missing_fails_nav_only);
  RUN_TEST(test_converter_input_and_output_missing_fails_both);
  RUN_TEST(test_in_fails_if_any_source_bad);
  RUN_TEST(test_no_sources_load_fails);
  RUN_TEST(test_shunt_missing_fails_load_not_in);
  RUN_TEST(test_nothing_drawn_from_bank);
  RUN_TEST(test_negative_source_adds_nothing_to_in);
  RUN_TEST(test_valid_zero_is_a_reading);
  RUN_TEST(test_bank_of_other_kind_uses_same_formula);
  return UNITY_END();
}
