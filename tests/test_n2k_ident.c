#include <string.h>
#include "unity.h"
#include "n2k_ident.h"

void setUp(void) {}
void tearDown(void) {}

static n2k_msg_t msg(uint32_t pgn, const uint8_t *d, uint16_t len) {
  n2k_msg_t m = {pgn, 0x24, 255, 6, len, d};
  return m;
}

static void test_claim_name_little_endian(void) {
  const uint8_t d[8] = {0xF8, 0x01, 0xC0, 0x2C, 0x00, 0xA0, 0x46, 0xC0};   // a claim from a real capture
  n2k_msg_t m = msg(60928, d, 8);
  n2k_claim_t c;
  TEST_ASSERT_TRUE(n2k_decode_claim(&m, &c));
  TEST_ASSERT_TRUE(c.name == 0xC046A0002CC001F8ull);
}

static void test_claim_wrong_length_rejected(void) {
  const uint8_t d[7] = {0};
  n2k_msg_t m = msg(60928, d, 7);
  n2k_claim_t c;
  TEST_ASSERT_FALSE(n2k_decode_claim(&m, &c));
  m.pgn = 60929;
  const uint8_t e[8] = {0};
  m.data = e; m.len = 8;
  TEST_ASSERT_FALSE(n2k_decode_claim(&m, &c));
}

static void put(uint8_t *d, int off, const char *s, int width) {
  memset(d + off, '@', (size_t)width);   // NMEA pads with '@' or spaces or NUL
  memcpy(d + off, s, strlen(s));
  d[off + (int)strlen(s)] = 0;
}

static void test_prodinfo_strings_trimmed(void) {
  uint8_t d[134];
  memset(d, 0, sizeof d);
  d[0] = 0x30; d[1] = 0x0B;    // db version
  d[2] = 0x34; d[3] = 0x12;    // product code 0x1234
  put(d, 4, "Model X", 32);
  put(d, 36, "1.0", 32);
  put(d, 68, "v2", 32);
  memset(d + 100, ' ', 32);
  memcpy(d + 100, "HQ123  ", 7);   // space padded
  n2k_msg_t m = msg(126996, d, sizeof d);
  n2k_prodinfo_t p;
  TEST_ASSERT_TRUE(n2k_decode_prodinfo(&m, &p));
  TEST_ASSERT_EQUAL_STRING("Model X", p.model_id);
  TEST_ASSERT_EQUAL_STRING("HQ123", p.serial);
  TEST_ASSERT_EQUAL_UINT16(0x1234, p.product_code);
}

static void test_prodinfo_short_rejected(void) {
  uint8_t d[40] = {0};
  n2k_msg_t m = msg(126996, d, sizeof d);
  n2k_prodinfo_t p;
  TEST_ASSERT_FALSE(n2k_decode_prodinfo(&m, &p));
}

static void test_iso_request_encodes_pgn(void) {
  uint8_t d[3];
  uint16_t len = 0;
  TEST_ASSERT_TRUE(n2k_encode_iso_request(126996, d, &len));
  TEST_ASSERT_EQUAL_UINT16(3, len);
  TEST_ASSERT_EQUAL_UINT8(0x14, d[0]);   // 126996 = 0x1F014
  TEST_ASSERT_EQUAL_UINT8(0xF0, d[1]);
  TEST_ASSERT_EQUAL_UINT8(0x01, d[2]);
  TEST_ASSERT_FALSE(n2k_encode_iso_request(0x1000000, d, &len));   // wider than 24 bits
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_claim_name_little_endian);
  RUN_TEST(test_claim_wrong_length_rejected);
  RUN_TEST(test_prodinfo_strings_trimmed);
  RUN_TEST(test_prodinfo_short_rejected);
  RUN_TEST(test_iso_request_encodes_pgn);
  return UNITY_END();
}
