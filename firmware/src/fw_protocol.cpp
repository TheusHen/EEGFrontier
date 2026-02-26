#include "fw_protocol.h"

#include <cstdio>

#include "fw_config.h"
#include "fw_state.h"
#include "fw_tx.h"
#include "fw_utils.h"

void printLine(const char* s) {
  Serial.println(s);
}

void printKV(const char* key, const char* value) {
  Serial.print(key);
  Serial.print('=');
  Serial.println(value);
}

void printKVU32(const char* key, uint32_t value) {
  Serial.print(key);
  Serial.print('=');
  Serial.println(value);
}

void printKVU64(const char* key, uint64_t value) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
  printKV(key, buf);
}

bool emitBinaryRawPacket(const uint8_t* raw, size_t rawLen) {
  uint8_t enc[96];
  size_t encLen = cobsEncode(raw, rawLen, enc);
  if (txFreeBytes() < (encLen + 1)) {
    // Atomic failure: do not enqueue a partial packet.
    g_txBytesDroppedTotal += static_cast<uint32_t>(encLen + 1);
    g_txPacketsDroppedTotal++;
    g_pendingTxOverflowFlag = true;
    return false;
  }
  bool okData = txWriteBytes(enc, encLen);
  bool okTerm = txWriteByte(static_cast<uint8_t>(0x00));
  return okData && okTerm;
}

bool emitEventPacket(uint8_t eventCode, uint32_t a, uint32_t b, uint32_t c) {
  uint8_t raw[2 + 1 + 4 + 4 + 4 + 2];
  size_t idx = 0;

  raw[idx++] = PKT_EVENT;
  raw[idx++] = PROTO_VER;
  raw[idx++] = eventCode;
  pack_u32_le(&raw[idx], a);
  idx += 4;
  pack_u32_le(&raw[idx], b);
  idx += 4;
  pack_u32_le(&raw[idx], c);
  idx += 4;

  uint16_t crc = crc16_ccitt(raw, idx);
  pack_u16_le(&raw[idx], crc);
  idx += 2;

  return emitBinaryRawPacket(raw, idx);
}

bool emitErrorPacket(uint8_t errorCode, uint32_t a, uint32_t b) {
  uint8_t raw[2 + 1 + 4 + 4 + 2];
  size_t idx = 0;

  raw[idx++] = PKT_ERROR;
  raw[idx++] = PROTO_VER;
  raw[idx++] = errorCode;
  pack_u32_le(&raw[idx], a);
  idx += 4;
  pack_u32_le(&raw[idx], b);
  idx += 4;

  uint16_t crc = crc16_ccitt(raw, idx);
  pack_u16_le(&raw[idx], crc);
  idx += 2;

  return emitBinaryRawPacket(raw, idx);
}

bool emitSamplePacket(uint32_t t_us, uint32_t status24,
                      int32_t ch1, int32_t ch2, int32_t ch3, int32_t ch4,
                      uint32_t flags, uint32_t missedDrdyFrame, uint32_t recoveriesTotal) {
  uint8_t raw[2 + (4 * 9) + 2];
  size_t idx = 0;

  raw[idx++] = PKT_SAMPLE;
  raw[idx++] = PROTO_VER;

  pack_u32_le(&raw[idx], g_sampleIndex++);
  idx += 4;
  pack_u32_le(&raw[idx], t_us);
  idx += 4;
  pack_u32_le(&raw[idx], status24);
  idx += 4;
  pack_i32_le(&raw[idx], ch1);
  idx += 4;
  pack_i32_le(&raw[idx], ch2);
  idx += 4;
  pack_i32_le(&raw[idx], ch3);
  idx += 4;
  pack_i32_le(&raw[idx], ch4);
  idx += 4;
  pack_u32_le(&raw[idx], flags);
  idx += 4;
  pack_u32_le(&raw[idx], missedDrdyFrame);
  idx += 4;
  pack_u32_le(&raw[idx], recoveriesTotal);
  idx += 4;

  uint16_t crc = crc16_ccitt(raw, idx);
  pack_u16_le(&raw[idx], crc);
  idx += 2;

  return emitBinaryRawPacket(raw, idx);
}

void emitCsvFrame(uint32_t drdy_t_us, uint32_t proc_t_us, uint32_t drdy_interval_us,
                  uint32_t status24,
                  int32_t ch1, int32_t ch2, int32_t ch3, int32_t ch4,
                  int32_t ch1_uv, int32_t ch2_uv, int32_t ch3_uv, int32_t ch4_uv,
                  uint32_t flags,
                  uint32_t missedDrdyFrame, uint32_t missedDrdyTotal,
                  uint32_t recoveriesTotal) {
  Serial.print(g_sampleIndex++);
  Serial.print(',');
  Serial.print(drdy_t_us);
  Serial.print(',');
  Serial.print(proc_t_us);
  Serial.print(',');
  Serial.print(drdy_interval_us);
  Serial.print(',');
  Serial.print(status24);
  Serial.print(',');
  Serial.print(ch1);
  Serial.print(',');
  Serial.print(ch2);
  Serial.print(',');
  Serial.print(ch3);
  Serial.print(',');
  Serial.print(ch4);
  Serial.print(',');
  Serial.print(ch1_uv);
  Serial.print(',');
  Serial.print(ch2_uv);
  Serial.print(',');
  Serial.print(ch3_uv);
  Serial.print(',');
  Serial.print(ch4_uv);
  Serial.print(',');
  Serial.print(flags);
  Serial.print(',');
  Serial.print(missedDrdyFrame);
  Serial.print(',');
  Serial.print(missedDrdyTotal);
  Serial.print(',');
  Serial.println(recoveriesTotal);
}
