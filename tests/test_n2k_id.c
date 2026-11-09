#include "unity.h"
#include "n2k.h"

void setUp(void) {}
void tearDown(void) {}

static void test_pack_pdu2_engine_rapid(void) {
  TEST_ASSERT_EQUAL_HEX32(0x09F20017, n2k_id_pack(2, 127488, 0x17, 255));
}

static void test_pack_pdu1_iso_request(void) {
  TEST_ASSERT_EQUAL_HEX32(0x18EAFF23, n2k_id_pack(6, 59904, 0x23, 0xFF));
  TEST_ASSERT_EQUAL_HEX32(0x18EA4223, n2k_id_pack(6, 59904, 0x23, 0x42));
}

static void test_pack_address_claim(void) {
  TEST_ASSERT_EQUAL_HEX32(0x18EEFF00, n2k_id_pack(6, 60928, 0x00, 255));
}

static void test_unpack_round_trip(void) {
  n2k_id_t i;
  n2k_id_unpack(0x09F20017, &i);
  TEST_ASSERT_EQUAL_UINT8(2, i.prio);
  TEST_ASSERT_EQUAL_UINT32(127488, i.pgn);
  TEST_ASSERT_EQUAL_UINT8(0x17, i.src);
  TEST_ASSERT_EQUAL_UINT8(255, i.dst);
  n2k_id_unpack(0x18EA4223, &i);
  TEST_ASSERT_EQUAL_UINT8(6, i.prio);
  TEST_ASSERT_EQUAL_UINT32(59904, i.pgn);
  TEST_ASSERT_EQUAL_UINT8(0x23, i.src);
  TEST_ASSERT_EQUAL_UINT8(0x42, i.dst);
  n2k_id_unpack(0x18EEFF00, &i);
  TEST_ASSERT_EQUAL_UINT32(60928, i.pgn);
  TEST_ASSERT_EQUAL_UINT8(255, i.dst);
}

static void test_pdu1_pgn_excludes_destination(void) {
  n2k_id_t i;
  n2k_id_unpack(0x18EA4223, &i);
  TEST_ASSERT_NOT_EQUAL(0xEA42, i.pgn);
  TEST_ASSERT_EQUAL_UINT32(59904, i.pgn);
}

static void test_fast_pgn_table(void) {
  const uint32_t fast[] = {126996, 126983, 126984, 126985, 126986, 126208, 126720, 127489, 130816, 131071};
  const uint32_t single[] = {59904, 60928, 127488, 127508, 130306, 61184, 65280, 126993};
  for (size_t k = 0; k < sizeof fast / sizeof *fast; k++) TEST_ASSERT_TRUE_MESSAGE(n2k_pgn_is_fast(fast[k]), "fast");
  for (size_t k = 0; k < sizeof single / sizeof *single; k++) TEST_ASSERT_FALSE_MESSAGE(n2k_pgn_is_fast(single[k]), "single");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_pack_pdu2_engine_rapid);
  RUN_TEST(test_pack_pdu1_iso_request);
  RUN_TEST(test_pack_address_claim);
  RUN_TEST(test_unpack_round_trip);
  RUN_TEST(test_pdu1_pgn_excludes_destination);
  RUN_TEST(test_fast_pgn_table);
  return UNITY_END();
}
