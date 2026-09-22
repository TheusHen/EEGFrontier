#include "fw_protocol.h"

#include <cstdio>

#include "fw_config.h"
#include "fw_state.h"
#include "fw_tx.h"
#include "fw_utils.h"

void printLine(const char* s) {
  if (s) {
    txWriteCString(s);
  }
  txWriteCString("\r\n");
  txService();
}

void printKV(const char* key, const char* value) {
  if (key) {
    txWriteCString(key);
  }
  txWriteCString("=");
  if (value) {
    txWriteCString(value);
  }
  txWriteCString("\r\n");
  txService();
}

void printKVU32(const char* key, uint32_t value) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%s=%lu", key ? key : "", static_cast<unsigned long>(value));
  printLine(buf);
}

void printKVU64(const char* key, uint64_t value) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%s=%llu", key ? key : "",
           static_cast<unsigned long long>(value));
  printLine(buf);
}

bool emitBinaryRawPacket(const uint8_t* raw, size_t rawLen) {
  if (!raw || rawLen < 4 || rawLen > 52) {
    return false;
  }
  uint8_t enc[64];
  size_t encLen = cobsEncode(raw, rawLen, enc);
  if (encLen + 1 > sizeof(enc)) {
    return false;
  }
  if (txFreeBytes() < (encLen + 1)) {
    // Atomic failure: do not enqueue a partial packet.
    g_txBytesDroppedTotal += static_cast<uint32_t>(encLen + 1);
    g_txPacketsDroppedTotal++;
    g_pendingTxOverflowFlag = true;
    return false;
  }
  bool okData = txWriteBytes(enc, encLen);
  bool okTerm = txWriteByte(static_cast<uint8_t>(0x00));
  // High-water notice must not recurse: only data/sample packets trigger it,
  // never the notice itself.
  if (txAboveHighWater() && rawLen >= 2 && raw[0] == PKT_SAMPLE) {
    uint8_t note[2 + 1 + 4 + 4 + 4 + 2];
    size_t n = 0;
    note[n++] = PKT_EVENT;
    note[n++] = PROTO_VER;
    note[n++] = EVT_TX_HIGH_WATER;
    pack_u32_le(&note[n], static_cast<uint32_t>(txQueuedBytes()));
    n += 4;
    pack_u32_le(&note[n], g_txPacketsDroppedTotal);
    n += 4;
    pack_u32_le(&note[n], 0);
    n += 4;
    uint16_t crc = crc16_ccitt(note, n);
    pack_u16_le(&note[n], crc);
    n += 2;
    uint8_t encNote[64];
    size_t encNoteLen = cobsEncode(note, n, encNote);
    if (txFreeBytes() >= encNoteLen + 1) {
      txWriteBytes(encNote, encNoteLen);
      txWriteByte(0x00);
    }
  }
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
  // Layout frozen for Pendulum compat: 2 header + 40 payload + 2 crc = 44.
  uint8_t raw[2 + (4 * 10) + 2];
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
  // Route through the TX ring instead of blocking on Serial.print so CSV
  // mode cannot stall DRDY handling. Callers decimate when the queue grows.
  char buf[208];
  int n = snprintf(buf, sizeof(buf),
                   "%lu,%lu,%lu,%lu,%lu,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%lu,%lu,%lu,%lu",
                   static_cast<unsigned long>(g_sampleIndex++),
                   static_cast<unsigned long>(drdy_t_us),
                   static_cast<unsigned long>(proc_t_us),
                   static_cast<unsigned long>(drdy_interval_us),
                   static_cast<unsigned long>(status24), static_cast<long>(ch1),
                   static_cast<long>(ch2), static_cast<long>(ch3), static_cast<long>(ch4),
                   static_cast<long>(ch1_uv), static_cast<long>(ch2_uv),
                   static_cast<long>(ch3_uv), static_cast<long>(ch4_uv),
                   static_cast<unsigned long>(flags),
                   static_cast<unsigned long>(missedDrdyFrame),
                   static_cast<unsigned long>(missedDrdyTotal),
                   static_cast<unsigned long>(recoveriesTotal));
  if (n > 0) {
    txWriteBytes(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(n));
    txWriteCString("\r\n");
  }
}

void emitHelloPacket() {
  // Compact boot descriptor. Old hosts treat it as a generic event;
  // new hosts use it to learn SPS/gain/VREF without guessing.
  // a = (fw_major << 24) | (fw_minor << 16) | PROTO_VER
  // b = (session_id low 16 << 16) | sps
  // c = (gain << 24) | (vref_mv & 0xFFFFFF)
  uint32_t a = (2UL << 24) | (0UL << 16) | PROTO_VER;
  uint32_t b = (g_sampleRateSps & 0xFFFFUL);
  uint32_t vrefMv = g_adsVrefUv / 1000UL;
  uint32_t c = (static_cast<uint32_t>(g_adsGain) << 24) | (vrefMv & 0x00FFFFFFUL);
  emitEventPacket(EVT_HELLO, a, b, c);
}
