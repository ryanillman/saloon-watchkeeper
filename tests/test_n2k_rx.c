#include <string.h>
#include "unity.h"
#include "n2k.h"

#define PGN_FAST 126996
#define SRC_A 0x20
#define SRC_B 0x21

static n2k_fp_slot_t slots[4];
static n2k_rx_t rx;
static int got_count;
static n2k_msg_t got;
static uint8_t got_data[N2K_FP_MAX];

static void on_msg(const n2k_msg_t *m, void *ctx) {
  (void)ctx;
  got = *m;
  memcpy(got_data, m->data, m->len);
  got.data = got_data;
  got_count++;
}

static void init(size_t nslots) {
  got_count = 0;
  memset(&got, 0, sizeof got);
  n2k_rx_init(&rx, slots, nslots, on_msg, NULL);
}
void setUp(void) { init(4); }
void tearDown(void) {}

static void fr(uint8_t src, uint32_t now, const uint8_t *d, uint8_t len) {
  n2k_feed_frame(&rx, n2k_id_pack(6, PGN_FAST, src, 255), d, len, now);
}

// Payload bytes base..base+19 as three frames with the given sequence number.
static void frame0(uint8_t src, uint8_t seq, uint8_t base, uint32_t now) {
  uint8_t f[8] = {(uint8_t)(seq << 5), 20};
  for (int i = 0; i < 6; i++) f[2 + i] = base + i;
  fr(src, now, f, 8);
}
static void frameN(uint8_t src, uint8_t seq, uint8_t n, uint8_t base, uint32_t now) {
  uint8_t f[8] = {(uint8_t)(seq << 5 | n)};
  for (int i = 0; i < 7; i++) f[1 + i] = base + 6 + 7 * (n - 1) + i;
  fr(src, now, f, 8);
}
static void expect_payload(uint8_t base) {
  TEST_ASSERT_EQUAL_INT(1, got_count);
  TEST_ASSERT_EQUAL_UINT32(PGN_FAST, got.pgn);
  TEST_ASSERT_EQUAL_UINT16(20, got.len);
  for (int i = 0; i < 20; i++) TEST_ASSERT_EQUAL_UINT8(base + i, got_data[i]);
}

static void test_single_frame_passes_through(void) {
  const uint8_t d[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  n2k_feed_frame(&rx, n2k_id_pack(2, 127488, 0x17, 255), d, 8, 0);
  TEST_ASSERT_EQUAL_INT(1, got_count);
  TEST_ASSERT_EQUAL_UINT32(127488, got.pgn);
  TEST_ASSERT_EQUAL_UINT16(8, got.len);
  TEST_ASSERT_EQUAL_UINT8(0x17, got.src);
  TEST_ASSERT_EQUAL_UINT8(2, got.prio);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(d, got_data, 8);
}

static void test_fast_packet_reassembles(void) {
  frame0(SRC_A, 0, 0, 0);
  TEST_ASSERT_EQUAL_INT(0, got_count);
  frameN(SRC_A, 0, 1, 0, 1);
  frameN(SRC_A, 0, 2, 0, 2);
  expect_payload(0);
  TEST_ASSERT_EQUAL_UINT8(SRC_A, got.src);
}

static void test_out_of_order_drops(void) {
  frame0(SRC_A, 0, 0, 0);
  frameN(SRC_A, 0, 2, 0, 1);
  frameN(SRC_A, 0, 1, 0, 2);
  TEST_ASSERT_EQUAL_INT(0, got_count);
  frame0(SRC_A, 1, 50, 3);
  frameN(SRC_A, 1, 1, 50, 4);
  frameN(SRC_A, 1, 2, 50, 5);
  expect_payload(50);
}

static void test_duplicate_frame_ignored(void) {
  frame0(SRC_A, 0, 0, 0);
  frameN(SRC_A, 0, 1, 0, 1);
  frameN(SRC_A, 0, 1, 0, 2);
  TEST_ASSERT_EQUAL_INT(0, got_count);
  frameN(SRC_A, 0, 2, 0, 3);
  expect_payload(0);
}

static void test_lost_frame_drops_then_recovers(void) {
  frame0(SRC_A, 0, 0, 0);
  frameN(SRC_A, 0, 2, 0, 1);
  TEST_ASSERT_EQUAL_INT(0, got_count);
  frame0(SRC_A, 1, 10, 2);
  frameN(SRC_A, 1, 1, 10, 3);
  frameN(SRC_A, 1, 2, 10, 4);
  expect_payload(10);
}

static void test_new_frame0_replaces_incomplete(void) {
  frame0(SRC_A, 0, 0, 0);
  frameN(SRC_A, 0, 1, 0, 1);
  frame0(SRC_A, 1, 100, 2);
  frameN(SRC_A, 1, 1, 100, 3);
  frameN(SRC_A, 1, 2, 100, 4);
  expect_payload(100);  // never a mix of the two messages
}

static void test_two_sources_interleaved(void) {
  frame0(SRC_A, 0, 0, 0);
  frame0(SRC_B, 3, 40, 0);
  frameN(SRC_A, 0, 1, 0, 1);
  frameN(SRC_B, 3, 1, 40, 1);
  frameN(SRC_A, 0, 2, 0, 2);
  expect_payload(0);
  TEST_ASSERT_EQUAL_UINT8(SRC_A, got.src);
  got_count = 0;
  frameN(SRC_B, 3, 2, 40, 3);
  expect_payload(40);
  TEST_ASSERT_EQUAL_UINT8(SRC_B, got.src);
}

static void test_stale_slot_reclaimed(void) {
  init(1);
  frame0(SRC_A, 0, 0, 0);
  frame0(SRC_B, 0, 70, N2K_FP_TIMEOUT_MS + 1);
  frameN(SRC_B, 0, 1, 70, N2K_FP_TIMEOUT_MS + 2);
  frameN(SRC_B, 0, 2, 70, N2K_FP_TIMEOUT_MS + 3);
  expect_payload(70);
}

static void test_slots_exhausted_drops_new(void) {
  init(1);
  frame0(SRC_A, 0, 0, 0);
  frame0(SRC_B, 0, 70, 100);  // within the timeout: no free slot, dropped
  frameN(SRC_B, 0, 1, 70, 101);
  frameN(SRC_A, 0, 1, 0, 102);
  frameN(SRC_A, 0, 2, 0, 103);
  expect_payload(0);
  TEST_ASSERT_EQUAL_UINT8(SRC_A, got.src);
}

static void test_malformed_dropped(void) {
  uint8_t short0[1] = {0x00};
  fr(SRC_A, 0, short0, 1);
  uint8_t zero_len[8] = {0x00, 0, 1, 2, 3, 4, 5, 6};
  fr(SRC_A, 0, zero_len, 8);
  uint8_t too_long[8] = {0x00, 224, 1, 2, 3, 4, 5, 6};
  fr(SRC_A, 0, too_long, 8);
  uint8_t orphan[8] = {0x01, 1, 2, 3, 4, 5, 6, 7};
  fr(SRC_A, 0, orphan, 8);
  frame0(SRC_A, 0, 0, 0);
  frameN(SRC_A, 0, 1, 0, 1);
  uint8_t skip[8] = {0x03, 0, 0, 0, 0, 0, 0, 0};
  fr(SRC_A, 0, skip, 8);
  uint8_t empty[1] = {0};
  n2k_feed_frame(&rx, n2k_id_pack(6, PGN_FAST, SRC_A, 255), empty, 0, 0);
  TEST_ASSERT_EQUAL_INT(0, got_count);
}

static void test_timeout_survives_clock_wrap(void) {
  init(1);
  frame0(SRC_A, 0, 0, 0xFFFFFF00u);
  frame0(SRC_B, 0, 70, 0x00000100u);  // 0x200 ms later: A is still alive, B is dropped
  frameN(SRC_A, 0, 1, 0, 0x00000101u);
  frameN(SRC_A, 0, 2, 0, 0x00000102u);
  expect_payload(0);
  got_count = 0;
  frame0(SRC_A, 1, 20, 0x00000200u);
  frame0(SRC_B, 1, 90, 0x00000200u + N2K_FP_TIMEOUT_MS + 1);  // A now stale
  frameN(SRC_B, 1, 1, 90, 0x00000200u + N2K_FP_TIMEOUT_MS + 2);
  frameN(SRC_B, 1, 2, 90, 0x00000200u + N2K_FP_TIMEOUT_MS + 3);
  expect_payload(90);
}

static void test_feed_pgn_delivers_directly(void) {
  const uint8_t d[5] = {9, 8, 7, 6, 5};
  n2k_feed_pgn(&rx, PGN_FAST, SRC_A, d, 5, 0);
  TEST_ASSERT_EQUAL_INT(1, got_count);
  TEST_ASSERT_EQUAL_UINT32(PGN_FAST, got.pgn);
  TEST_ASSERT_EQUAL_UINT8(SRC_A, got.src);
  TEST_ASSERT_EQUAL_UINT16(5, got.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(d, got_data, 5);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_single_frame_passes_through);
  RUN_TEST(test_fast_packet_reassembles);
  RUN_TEST(test_out_of_order_drops);
  RUN_TEST(test_duplicate_frame_ignored);
  RUN_TEST(test_lost_frame_drops_then_recovers);
  RUN_TEST(test_new_frame0_replaces_incomplete);
  RUN_TEST(test_two_sources_interleaved);
  RUN_TEST(test_stale_slot_reclaimed);
  RUN_TEST(test_slots_exhausted_drops_new);
  RUN_TEST(test_malformed_dropped);
  RUN_TEST(test_timeout_survives_clock_wrap);
  RUN_TEST(test_feed_pgn_delivers_directly);
  return UNITY_END();
}
