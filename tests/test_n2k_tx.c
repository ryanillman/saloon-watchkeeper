#include <string.h>
#include "unity.h"
#include "n2k.h"

static n2k_fp_slot_t slots[2];
static n2k_rx_t rx;
static int got_count;
static uint16_t got_len;
static uint8_t got_data[N2K_FP_MAX];

static void on_msg(const n2k_msg_t *m, void *ctx) {
  (void)ctx;
  got_len = m->len;
  memcpy(got_data, m->data, m->len);
  got_count++;
}
void setUp(void) {
  got_count = 0;
  n2k_rx_init(&rx, slots, 2, on_msg, NULL);
}
void tearDown(void) {}

static void test_single_frame_padded(void) {
  const uint8_t d[3] = {1, 2, 3};
  n2k_tx_t tx;
  n2k_frame_t f;
  TEST_ASSERT_TRUE(n2k_tx_begin(&tx, 2, 127488, 0x17, 255, d, 3, 0));
  TEST_ASSERT_TRUE(n2k_tx_next(&tx, &f));
  TEST_ASSERT_EQUAL_HEX32(0x09F20017, f.id);
  TEST_ASSERT_EQUAL_UINT8(8, f.len);
  const uint8_t want[8] = {1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, f.data, 8);
  TEST_ASSERT_FALSE(n2k_tx_next(&tx, &f));
}

static void test_non_fast_over_8_refused(void) {
  uint8_t d[9] = {0};
  n2k_tx_t tx;
  TEST_ASSERT_FALSE(n2k_tx_begin(&tx, 2, 127488, 0x17, 255, d, 9, 0));
}

static void test_zero_and_oversize_refused(void) {
  uint8_t d[224] = {0};
  n2k_tx_t tx;
  TEST_ASSERT_FALSE(n2k_tx_begin(&tx, 6, 126996, 0x20, 255, d, 0, 0));
  TEST_ASSERT_FALSE(n2k_tx_begin(&tx, 6, 126996, 0x20, 255, d, 224, 0));
  TEST_ASSERT_TRUE(n2k_tx_begin(&tx, 6, 126996, 0x20, 255, d, 223, 0));
}

static void test_fast_short_payload_uses_fast_framing(void) {
  const uint8_t d[5] = {10, 11, 12, 13, 14};
  n2k_tx_t tx;
  n2k_frame_t f;
  TEST_ASSERT_TRUE(n2k_tx_begin(&tx, 2, 126983, 0x20, 255, d, 5, 2));
  TEST_ASSERT_TRUE(n2k_tx_next(&tx, &f));
  const uint8_t want[8] = {0x40, 5, 10, 11, 12, 13, 14, 0xFF};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, f.data, 8);
  TEST_ASSERT_FALSE(n2k_tx_next(&tx, &f));
}

static void test_round_trip_all_lengths(void) {
  uint8_t d[N2K_FP_MAX];
  for (uint16_t len = 9; len <= N2K_FP_MAX; len++) {
    for (uint16_t i = 0; i < len; i++) d[i] = (uint8_t)(i * 7 + 3);
    n2k_tx_t tx;
    n2k_frame_t f;
    got_count = 0;
    TEST_ASSERT_TRUE(n2k_tx_begin(&tx, 6, 126996, 0x20, 255, d, len, 3));
    int frames = 0;
    while (n2k_tx_next(&tx, &f)) {
      frames++;
      n2k_feed_frame(&rx, f.id, f.data, f.len, 0);
    }
    TEST_ASSERT_EQUAL_INT(1 + (len - 6 + 6) / 7, frames);
    TEST_ASSERT_EQUAL_INT(1, got_count);
    TEST_ASSERT_EQUAL_UINT16(len, got_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(d, got_data, len);
  }
}

static void test_frame_counters_and_seq(void) {
  uint8_t d[20];
  memset(d, 1, sizeof d);
  n2k_tx_t tx;
  n2k_frame_t f;
  TEST_ASSERT_TRUE(n2k_tx_begin(&tx, 6, 126996, 0x20, 255, d, 20, 5));
  TEST_ASSERT_TRUE(n2k_tx_next(&tx, &f));
  TEST_ASSERT_EQUAL_HEX8(0xA0, f.data[0]);
  TEST_ASSERT_EQUAL_UINT8(20, f.data[1]);
  TEST_ASSERT_TRUE(n2k_tx_next(&tx, &f));
  TEST_ASSERT_EQUAL_HEX8(0xA1, f.data[0]);
  TEST_ASSERT_TRUE(n2k_tx_next(&tx, &f));
  TEST_ASSERT_EQUAL_HEX8(0xA2, f.data[0]);
  TEST_ASSERT_FALSE(n2k_tx_next(&tx, &f));
}

static void test_seq_counts_per_pgn_and_dst_and_wraps(void) {
  n2k_seq_t s;
  memset(&s, 0, sizeof s);
  for (int i = 0; i < 8; i++) TEST_ASSERT_EQUAL_UINT8(i, n2k_seq_next(&s, 126996, 255));
  TEST_ASSERT_EQUAL_UINT8(0, n2k_seq_next(&s, 126996, 255));
  TEST_ASSERT_EQUAL_UINT8(0, n2k_seq_next(&s, 126996, 0x42));
  TEST_ASSERT_EQUAL_UINT8(0, n2k_seq_next(&s, 126983, 255));
  TEST_ASSERT_EQUAL_UINT8(1, n2k_seq_next(&s, 126996, 0x42));
}

static void test_seq_table_full_reuses_oldest(void) {
  n2k_seq_t s;
  memset(&s, 0, sizeof s);
  for (uint8_t d = 0; d < 8; d++) n2k_seq_next(&s, 126996, d);
  n2k_seq_next(&s, 126996, 0);  // entry 0 now at 2
  TEST_ASSERT_EQUAL_UINT8(0, n2k_seq_next(&s, 126996, 8));  // ninth pair evicts the oldest and starts at 0
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_single_frame_padded);
  RUN_TEST(test_non_fast_over_8_refused);
  RUN_TEST(test_zero_and_oversize_refused);
  RUN_TEST(test_fast_short_payload_uses_fast_framing);
  RUN_TEST(test_round_trip_all_lengths);
  RUN_TEST(test_frame_counters_and_seq);
  RUN_TEST(test_seq_counts_per_pgn_and_dst_and_wraps);
  RUN_TEST(test_seq_table_full_reuses_oldest);
  return UNITY_END();
}
