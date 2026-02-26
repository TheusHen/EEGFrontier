#include <unity.h>

#include "fw_utils.h"

void test_sign_extend_24bit_values() {
  TEST_ASSERT_EQUAL_INT32(0, signExtend24(0x000000));
  TEST_ASSERT_EQUAL_INT32(1, signExtend24(0x000001));
  TEST_ASSERT_EQUAL_INT32(0x007FFFFF, signExtend24(0x007FFFFF));
  TEST_ASSERT_EQUAL_INT32(-1, signExtend24(0x00FFFFFF));
  TEST_ASSERT_EQUAL_INT32(-8388608, signExtend24(0x00800000));
}

void test_crc16_ccitt_known_vector() {
  static const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16_ccitt(data, sizeof(data)));
}

void test_cobs_encode_no_zeros() {
  const uint8_t in[] = {0x11, 0x22, 0x33};
  uint8_t out[8] = {0};
  size_t len = cobsEncode(in, sizeof(in), out);

  TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(len));
  TEST_ASSERT_EQUAL_UINT8(0x04, out[0]);
  TEST_ASSERT_EQUAL_UINT8(0x11, out[1]);
  TEST_ASSERT_EQUAL_UINT8(0x22, out[2]);
  TEST_ASSERT_EQUAL_UINT8(0x33, out[3]);
}

void test_cobs_encode_with_zeros() {
  const uint8_t in[] = {0x11, 0x00, 0x22, 0x00, 0x00, 0x33};
  uint8_t out[16] = {0};
  size_t len = cobsEncode(in, sizeof(in), out);

  const uint8_t expected[] = {
      0x02, 0x11,
      0x02, 0x22,
      0x01,
      0x02, 0x33};

  TEST_ASSERT_EQUAL_UINT32(sizeof(expected), static_cast<uint32_t>(len));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, sizeof(expected));
}

void test_pack_helpers_little_endian() {
  uint8_t buf[4] = {0};
  pack_u16_le(buf, 0xABCD);
  TEST_ASSERT_EQUAL_UINT8(0xCD, buf[0]);
  TEST_ASSERT_EQUAL_UINT8(0xAB, buf[1]);

  pack_u32_le(buf, 0x12345678UL);
  TEST_ASSERT_EQUAL_UINT8(0x78, buf[0]);
  TEST_ASSERT_EQUAL_UINT8(0x56, buf[1]);
  TEST_ASSERT_EQUAL_UINT8(0x34, buf[2]);
  TEST_ASSERT_EQUAL_UINT8(0x12, buf[3]);

  pack_i32_le(buf, -2);
  TEST_ASSERT_EQUAL_UINT8(0xFE, buf[0]);
  TEST_ASSERT_EQUAL_UINT8(0xFF, buf[1]);
  TEST_ASSERT_EQUAL_UINT8(0xFF, buf[2]);
  TEST_ASSERT_EQUAL_UINT8(0xFF, buf[3]);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_sign_extend_24bit_values);
  RUN_TEST(test_crc16_ccitt_known_vector);
  RUN_TEST(test_cobs_encode_no_zeros);
  RUN_TEST(test_cobs_encode_with_zeros);
  RUN_TEST(test_pack_helpers_little_endian);
  return UNITY_END();
}
