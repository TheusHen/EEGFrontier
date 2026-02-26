#pragma once

#include <Arduino.h>

void printLine(const char* s);
void printKV(const char* key, const char* value);
void printKVU32(const char* key, uint32_t value);
void printKVU64(const char* key, uint64_t value);

bool emitBinaryRawPacket(const uint8_t* raw, size_t rawLen);
bool emitEventPacket(uint8_t eventCode, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0);
bool emitErrorPacket(uint8_t errorCode, uint32_t a = 0, uint32_t b = 0);
bool emitSamplePacket(uint32_t t_us, uint32_t status24,
                      int32_t ch1, int32_t ch2, int32_t ch3, int32_t ch4,
                      uint32_t flags, uint32_t missedDrdyFrame, uint32_t recoveriesTotal);
void emitCsvFrame(uint32_t drdy_t_us, uint32_t proc_t_us, uint32_t drdy_interval_us,
                  uint32_t status24,
                  int32_t ch1, int32_t ch2, int32_t ch3, int32_t ch4,
                  int32_t ch1_uv, int32_t ch2_uv, int32_t ch3_uv, int32_t ch4_uv,
                  uint32_t flags,
                  uint32_t missedDrdyFrame, uint32_t missedDrdyTotal,
                  uint32_t recoveriesTotal);
