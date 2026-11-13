#include <string.h>
#include "electrical.h"
#include "unity.h"

static el_t el;
void setUp(void) { el_init(&el); }
void tearDown(void) {}

#define A 10   // refs: each is its own device on feed 0
#define B 20
#define C 30

static const el_endpoint_t *by_ref(el_ref_t ref) {
  for (size_t i = 0; i < el_endpoint_count(&el); i++)
    if (el_endpoint(&el, i)->src[EL_F_VOLTAGE] == ref) return el_endpoint(&el, i);
  return NULL;
}

// One reading of a battery-type endpoint (DC type 0 on a device that is not converter-class).
static void bat(el_ref_t ref, uint8_t inst, double v, double i, double soc, int64_t t) {
  el_observe(&el, 0, ref, inst, EL_F_DC_TYPE, 0, t);
  el_observe(&el, 0, ref, inst, EL_F_VOLTAGE, v, t);
  if (i > -900) el_observe(&el, 0, ref, inst, EL_F_CURRENT, i, t);
  if (soc >= 0) el_observe(&el, 0, ref, inst, EL_F_SOC, soc, t);
}
static void monitor_identity(el_ref_t ref) {
  el_identity_t id = {.model_id = "Some Shunt 500A"};
  el_set_identity(&el, 0, ref, &id);
}
// Both devices report the same bank every second until `to`.
static void run_pair(int64_t from, int64_t to, double vb) {
  for (int64_t t = from; t <= to; t += 1000) {
    bat(A, 0, 13.82, 0.0, 1.0, t);
    bat(B, 7, vb, 0.0, 1.0, t);
  }
}

static void test_mirror_pair_collapses_to_authoritative(void) {
  monitor_identity(A);
  run_pair(0, 12000, 13.82);
  TEST_ASSERT_FALSE(by_ref(A)->hidden);
  TEST_ASSERT_TRUE(by_ref(B)->hidden);
  TEST_ASSERT_TRUE(by_ref(B)->merged_into == by_ref(A)->key);
}

static void test_mirror_not_merged_before_window(void) {
  monitor_identity(A);
  run_pair(0, 5000, 13.82);
  TEST_ASSERT_FALSE(by_ref(A)->hidden);
  TEST_ASSERT_FALSE(by_ref(B)->hidden);
}

static void test_same_voltage_no_current_not_merged(void) {
  for (int64_t t = 0; t <= 20000; t += 1000) {
    bat(A, 0, 13.2, -999, -1, t);
    bat(B, 1, 13.2, -999, -1, t);
  }
  TEST_ASSERT_FALSE(by_ref(A)->hidden);
  TEST_ASSERT_FALSE(by_ref(B)->hidden);
}

static void test_independent_banks_with_different_current_not_merged(void) {
  for (int64_t t = 0; t <= 20000; t += 1000) {
    bat(A, 0, 13.2, 0.0, 1.0, t);
    bat(B, 0, 13.2, 10.0, 1.0, t);
  }
  TEST_ASSERT_FALSE(by_ref(A)->hidden);
  TEST_ASSERT_FALSE(by_ref(B)->hidden);
}

static void test_winner_quiet_briefly_does_not_flip(void) {
  monitor_identity(A);
  run_pair(0, 12000, 13.82);
  for (int64_t t = 13000; t <= 20000; t += 1000) bat(B, 7, 13.82, 0.0, 1.0, t);   // A silent for 8 s
  bat(B, 7, 13.60, 0.0, 1.0, 21000);   // drifted while A was quiet
  TEST_ASSERT_FALSE(by_ref(A)->hidden);
  TEST_ASSERT_TRUE(by_ref(B)->hidden);
  run_pair(22000, 26000, 13.60);   // A back; both now agree on the new value
  TEST_ASSERT_FALSE(by_ref(A)->hidden);
  TEST_ASSERT_TRUE(by_ref(B)->hidden);
}

static void test_lasting_disagreement_unmerges(void) {
  monitor_identity(A);
  run_pair(0, 12000, 13.82);
  run_pair(13000, 30000, 12.10);   // now clearly two different banks
  TEST_ASSERT_FALSE(by_ref(A)->hidden);
  TEST_ASSERT_FALSE(by_ref(B)->hidden);
}

static void test_authority_gained_later_takes_over_after_window(void) {
  for (int64_t t = 0; t <= 12000; t += 1000) {
    bat(A, 0, 13.5, 1.0, -1, t);
    bat(B, 0, 13.5, 1.0, -1, t);
  }
  TEST_ASSERT_TRUE(by_ref(A)->hidden != by_ref(B)->hidden);   // one of them won
  uint64_t first_winner = by_ref(A)->hidden ? by_ref(B)->key : by_ref(A)->key;
  el_ref_t other = by_ref(A)->hidden ? A : B;
  if (first_winner == by_ref(A)->key) other = B;
  for (int64_t t = 13000; t <= 26000; t += 1000) {   // the loser starts reporting SoC
    bat(A, 0, 13.5, 1.0, other == A ? 0.9 : -1, t);
    bat(B, 0, 13.5, 1.0, other == B ? 0.9 : -1, t);
  }
  TEST_ASSERT_FALSE(by_ref(other)->hidden);
  TEST_ASSERT_TRUE(by_ref(other == A ? B : A)->hidden);
}

static void test_charger_output_current_not_used_as_bank_current(void) {
  for (int64_t t = 0; t <= 20000; t += 1000) {
    bat(A, 0, 13.8, -999, 0.9, t);                    // a bank sender with no current
    el_observe(&el, 0, B, 0, EL_F_CONV_STATE, 4, t);  // a charger on the same bank
    bat(B, 0, 13.8, 10.0, -1, t);
  }
  const el_endpoint_t *bank = by_ref(A), *chg = by_ref(B);
  TEST_ASSERT_EQUAL(EL_FN_BATTERY, bank->fn);
  TEST_ASSERT_EQUAL(EL_FN_CHARGER, chg->fn);
  TEST_ASSERT_FALSE(bank->hidden);
  TEST_ASSERT_FALSE(chg->hidden);
  double v;
  el_ref_t src;
  TEST_ASSERT_FALSE(el_leaf(&el, bank, EL_F_CURRENT, &v, &src));
}

static void test_leaf_gap_filled_from_merged_mirror_with_provenance(void) {
  monitor_identity(A);
  for (int64_t t = 0; t <= 12000; t += 1000) {
    bat(A, 0, 13.82, 0.0, 1.0, t);
    bat(B, 7, 13.82, 0.0, 1.0, t);
    el_observe(&el, 0, B, 7, EL_F_TIME_REMAINING, 5000, t);
  }
  const el_endpoint_t *win = by_ref(A);
  TEST_ASSERT_FALSE(win->hidden);
  double v = 0;
  el_ref_t src = 0;
  TEST_ASSERT_TRUE(el_leaf(&el, win, EL_F_SOC, &v, &src));
  TEST_ASSERT_EQUAL_UINT32(A, src);   // the winner's own leaf, not the mirror's
  TEST_ASSERT_TRUE(el_leaf(&el, win, EL_F_TIME_REMAINING, &v, &src));
  TEST_ASSERT_EQUAL_DOUBLE(5000, v);
  TEST_ASSERT_EQUAL_UINT32(B, src);   // the gap came from the mirror and says so
}

static bool hidden_after(bool reversed) {
  el_init(&el);
  monitor_identity(A);
  for (int64_t t = 0; t <= 12000; t += 1000) {
    if (reversed) { bat(B, 7, 13.82, 0.0, 1.0, t); bat(A, 0, 13.82, 0.0, 1.0, t); }
    else { bat(A, 0, 13.82, 0.0, 1.0, t); bat(B, 7, 13.82, 0.0, 1.0, t); }
  }
  return by_ref(B)->hidden && !by_ref(A)->hidden;
}
static void test_merge_independent_of_arrival_order(void) {
  TEST_ASSERT_TRUE(hidden_after(false));
  TEST_ASSERT_TRUE(hidden_after(true));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_mirror_pair_collapses_to_authoritative);
  RUN_TEST(test_mirror_not_merged_before_window);
  RUN_TEST(test_same_voltage_no_current_not_merged);
  RUN_TEST(test_independent_banks_with_different_current_not_merged);
  RUN_TEST(test_winner_quiet_briefly_does_not_flip);
  RUN_TEST(test_lasting_disagreement_unmerges);
  RUN_TEST(test_authority_gained_later_takes_over_after_window);
  RUN_TEST(test_charger_output_current_not_used_as_bank_current);
  RUN_TEST(test_leaf_gap_filled_from_merged_mirror_with_provenance);
  RUN_TEST(test_merge_independent_of_arrival_order);
  return UNITY_END();
}
