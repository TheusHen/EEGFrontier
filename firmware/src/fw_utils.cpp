#include "fw_utils.h"

void pack_u16_le(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void pack_u32_le(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

void pack_i32_le(uint8_t* p, int32_t v) {
  pack_u32_le(p, static_cast<uint32_t>(v));
}

int32_t signExtend24(uint32_t x) {
  if (x & 0x00800000UL) {
    x |= 0xFF000000UL;
  }
  return static_cast<int32_t>(x);
}

uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (static_cast<uint16_t>(data[i]) << 8);
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 0x8000) {
        crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
      } else {
        crc = static_cast<uint16_t>(crc << 1);
      }
    }
  }
  return crc;
}

size_t cobsEncode(const uint8_t* input, size_t length, uint8_t* output) {
  uint8_t* outStart = output;
  uint8_t* codePtr = output++;
  uint8_t code = 1;

  for (size_t i = 0; i < length; i++) {
    if (input[i] == 0) {
      *codePtr = code;
      codePtr = output++;
      code = 1;
    } else {
      *output++ = input[i];
      code++;
      if (code == 0xFF) {
        *codePtr = code;
        codePtr = output++;
        code = 1;
      }
    }
  }

  *codePtr = code;
  return static_cast<size_t>(output - outStart);
}

