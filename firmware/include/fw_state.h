#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <stdint.h>

#include "fw_config.h"

struct DrdyFrameSnapshot {
  bool ready;
  uint32_t drdyTimestampUs;
  uint32_t drdyIntervalUs;
  uint32_t missedDrdyFrame;
  uint32_t missedDrdyTotal;
  uint32_t drdyEdgesTotal;
};

struct DrdyJitterSnapshot {
  uint32_t intervalLastUs;
  uint32_t intervalMinUs;
  uint32_t intervalMaxUs;
  uint32_t jitterAbsLastUs;
  uint32_t jitterAbsMinUs;
  uint32_t jitterAbsMaxUs;
  uint32_t intervalCount;
  uint64_t intervalSumUs;
  uint64_t jitterAbsSumUs;
};

extern OutputMode g_outputMode;

extern SPISettings g_spiSettings;
// Expected DRDY period in us, derived from the active sample rate so the
// ISR never divides. Updated whenever SPS changes.
extern uint32_t g_expectedPeriodUs;

// Minimal ISR state: the ISR only timestamps and counts. Interval and
// jitter math runs later with interrupts disabled, in snapshot code.
extern volatile bool g_drdyFlag;
extern volatile uint32_t g_lastDrdyTimestampUs;
extern volatile uint32_t g_drdyEdgesTotal;

// Deferred DRDY stats, touched only outside the ISR.
extern uint32_t g_missedDrdyTotal;
extern uint32_t g_missedDrdyFrame;
extern uint32_t g_prevDrdyTimestampUs;
extern uint32_t g_drdyIntervalLastUs;
extern uint32_t g_drdyIntervalMinUs;
extern uint32_t g_drdyIntervalMaxUs;
extern uint32_t g_drdyJitterAbsLastUs;
extern uint32_t g_drdyJitterAbsMinUs;
extern uint32_t g_drdyJitterAbsMaxUs;
extern uint32_t g_drdyIntervalCount;
extern uint64_t g_drdyIntervalSumUs;
extern uint64_t g_drdyJitterAbsSumUs;

extern bool g_streaming;
extern bool g_adsReady;
extern bool g_pendingRecoveredFlag;
extern bool g_pendingBtnFlag;
extern bool g_pendingTxOverflowFlag;
extern bool g_pendingConfigFlag;

// Monotonic counters: sample_index never resets after boot so hosts can
// detect gaps across START/STOP and recovery cycles.
extern uint32_t g_sampleIndex;
extern uint32_t g_sessionId;
extern uint32_t g_recoveriesTotal;
extern uint32_t g_lastGoodFrameUs;
extern uint32_t g_lastButtonToggleMs;
extern uint32_t g_buttonPressStartMs;
extern bool g_buttonLongFired;
extern uint32_t g_lastSampleProcessUs;
extern uint32_t g_lastDrdyToProcessLatencyUs;

extern bool g_lastBtnState;

extern uint8_t g_rawFrame[15];

extern char g_cmdBuf[96];
extern size_t g_cmdLen;

// ADS diagnostic / scale state
extern uint32_t g_sampleRateSps;
extern uint8_t g_adsGain;
extern uint32_t g_adsVrefUv;
extern bool g_internalTestSignalEnabled;
extern bool g_leadOffDiagEnabled;
extern uint8_t g_lastLeadOffStatP;
extern uint8_t g_lastLeadOffStatN;
extern uint32_t g_lastStatus24;
extern uint32_t g_statusInvalidTotal;
extern uint32_t g_leadOffAnyTotal;

// TX diagnostics
extern uint32_t g_txBytesDroppedTotal;
extern uint32_t g_txPacketsDroppedTotal;
extern uint32_t g_txMaxQueuedBytes;

// Watchdog diagnostics
extern bool g_watchdogSupported;
extern bool g_watchdogEnabled;
extern bool g_watchdogRebootDetected;
extern uint32_t g_watchdogTimeoutMs;
extern uint32_t g_watchdogFeedsTotal;
extern uint32_t g_watchdogLastFeedMs;
