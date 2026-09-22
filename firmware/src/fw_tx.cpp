#include "fw_tx.h"

#include <cstring>

#include "fw_state.h"

namespace {

constexpr size_t TX_RING_SIZE = 16384;
constexpr size_t TX_HIGH_WATER = (TX_RING_SIZE * 3) / 4;
uint8_t s_txRing[TX_RING_SIZE];
size_t s_head = 0;
size_t s_tail = 0;
size_t s_count = 0;

size_t contiguousReadable() {
  if (s_count == 0) {
    return 0;
  }
  if (s_tail < s_head) {
    return s_head - s_tail;
  }
  return TX_RING_SIZE - s_tail;
}

void noteDrop(size_t bytes) {
  g_txBytesDroppedTotal += static_cast<uint32_t>(bytes);
  g_txPacketsDroppedTotal++;
  g_pendingTxOverflowFlag = true;
}

}  // namespace

void txInit() {
  s_head = 0;
  s_tail = 0;
  s_count = 0;
}

size_t txQueuedBytes() {
  return s_count;
}

size_t txFreeBytes() {
  return TX_RING_SIZE - s_count;
}

bool txWriteBytes(const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0) {
    return true;
  }

  if (len > txFreeBytes()) {
    noteDrop(len);
    return false;
  }

  size_t firstChunk = min(len, TX_RING_SIZE - s_head);
  std::memcpy(&s_txRing[s_head], data, firstChunk);
  if (len > firstChunk) {
    std::memcpy(&s_txRing[0], data + firstChunk, len - firstChunk);
  }

  s_head = (s_head + len) % TX_RING_SIZE;
  s_count += len;
  if (s_count > g_txMaxQueuedBytes) {
    g_txMaxQueuedBytes = static_cast<uint32_t>(s_count);
  }
  return true;
}

bool txWriteByte(uint8_t b) {
  return txWriteBytes(&b, 1);
}

bool txWriteCString(const char* s) {
  if (!s) {
    return true;
  }
  return txWriteBytes(reinterpret_cast<const uint8_t*>(s), std::strlen(s));
}

bool txAboveHighWater() {
  return s_count >= TX_HIGH_WATER;
}

void txService() {
  if (s_count == 0) {
    return;
  }

  // Drain in bounded chunks so a full USB endpoint never starves sampling.
  for (uint8_t round = 0; round < 4 && s_count > 0; round++) {
    int available = Serial.availableForWrite();
    if (available <= 0) {
      return;
    }

    size_t chunk = min(static_cast<size_t>(available), contiguousReadable());
    if (chunk == 0) {
      return;
    }
    // Cap single writes; CDC writes above ~512B tend to block on RP2040.
    if (chunk > 512) {
      chunk = 512;
    }

    size_t written = Serial.write(&s_txRing[s_tail], chunk);
    if (written == 0) {
      return;
    }

    s_tail = (s_tail + written) % TX_RING_SIZE;
    s_count -= written;
  }
}

