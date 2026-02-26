#pragma once

#include <stddef.h>
#include <stdint.h>

void pack_u16_le(uint8_t* p, uint16_t v);
void pack_u32_le(uint8_t* p, uint32_t v);
void pack_i32_le(uint8_t* p, int32_t v);

int32_t signExtend24(uint32_t x);

uint16_t crc16_ccitt(const uint8_t* data, size_t len);
size_t cobsEncode(const uint8_t* input, size_t length, uint8_t* output);
