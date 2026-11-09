#include <errno.h>
#include <linux/can.h>
#include <string.h>
#include <unistd.h>
#include "unity.h"
#include "can_socketcan.h"

void setUp(void) {}
void tearDown(void) {}

static void test_convert_extended_data_frame(void) {
  struct can_frame cf;
  memset(&cf, 0, sizeof cf);
  cf.can_id = 0x09F20017 | CAN_EFF_FLAG;
  cf.len = 3;
  cf.data[0] = 1;
  cf.data[1] = 2;
  cf.data[2] = 3;
  n2k_frame_t f;
  TEST_ASSERT_TRUE(can_socketcan_convert(&cf, &f));
  TEST_ASSERT_EQUAL_HEX32(0x09F20017, f.id);
  TEST_ASSERT_EQUAL_UINT8(3, f.len);
  TEST_ASSERT_EQUAL_UINT8(2, f.data[1]);
}

static void test_convert_rejects_standard_rtr_and_error(void) {
  struct can_frame cf;
  n2k_frame_t f;
  memset(&cf, 0, sizeof cf);
  cf.can_id = 0x123;  // standard ID
  TEST_ASSERT_FALSE(can_socketcan_convert(&cf, &f));
  cf.can_id = 0x09F20017 | CAN_EFF_FLAG | CAN_RTR_FLAG;
  TEST_ASSERT_FALSE(can_socketcan_convert(&cf, &f));
  cf.can_id = CAN_ERR_FLAG | 0x40;
  TEST_ASSERT_FALSE(can_socketcan_convert(&cf, &f));
}

static void test_open_unknown_interface_fails(void) {
  TEST_ASSERT_NULL(can_socketcan_open("nosuchcan9"));
}

static void test_loopback_when_vcan_available(void) {
  can_socketcan_t *a = can_socketcan_open("vcan0");
  if (!a) TEST_IGNORE_MESSAGE("no vcan0 (sudo modprobe vcan; sudo ip link add dev vcan0 type vcan; sudo ip link set up vcan0)");
  can_socketcan_t *b = can_socketcan_open("vcan0");
  TEST_ASSERT_NOT_NULL(b);
  n2k_can_ops_t oa = can_socketcan_ops(a), ob = can_socketcan_ops(b);
  n2k_frame_t out = {0x09F20017, 2, {0xAA, 0xBB}}, in;
  TEST_ASSERT_FALSE(ob.recv(&in, ob.ctx));   // nothing yet, and it does not block
  TEST_ASSERT_TRUE(oa.send(&out, oa.ctx));
  bool got = false;
  for (int i = 0; i < 20 && !got; i++) {
    got = ob.recv(&in, ob.ctx);
    if (!got) usleep(5000);
  }
  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_EQUAL_HEX32(0x09F20017, in.id);
  TEST_ASSERT_EQUAL_UINT8(0xBB, in.data[1]);
  TEST_ASSERT_EQUAL(N2K_CAN_OK, ob.state(ob.ctx));
  can_socketcan_close(a);
  can_socketcan_close(b);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_convert_extended_data_frame);
  RUN_TEST(test_convert_rejects_standard_rtr_and_error);
  RUN_TEST(test_open_unknown_interface_fails);
  RUN_TEST(test_loopback_when_vcan_available);
  return UNITY_END();
}
