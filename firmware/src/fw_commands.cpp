#include "fw_commands.h"

#include <cstdio>
#include <cstring>

#include "ads1299_driver.h"
#include "fw_config.h"
#include "fw_protocol.h"
#include "fw_state.h"
#include "fw_tx.h"

namespace {

uint32_t u32AbsDiff(uint32_t a, uint32_t b) {
  return (a >= b) ? (a - b) : (b - a);
}

void printJitterSummary() {
  DrdyJitterSnapshot js = {};
  captureDrdyJitterSnapshot(&js);

  printKVU32("drdy_interval_last_us", js.intervalLastUs);
  printKVU32("drdy_interval_min_us", (js.intervalMinUs == 0xFFFFFFFFUL) ? 0 : js.intervalMinUs);
  printKVU32("drdy_interval_max_us", js.intervalMaxUs);
  printKVU32("drdy_jitter_abs_last_us", js.jitterAbsLastUs);
  printKVU32("drdy_jitter_abs_min_us", (js.jitterAbsMinUs == 0xFFFFFFFFUL) ? 0 : js.jitterAbsMinUs);
  printKVU32("drdy_jitter_abs_max_us", js.jitterAbsMaxUs);
  printKVU32("drdy_interval_count", js.intervalCount);
  if (js.intervalCount > 0) {
    printKVU32("drdy_interval_avg_us", static_cast<uint32_t>(js.intervalSumUs / js.intervalCount));
    printKVU32("drdy_jitter_abs_avg_us", static_cast<uint32_t>(js.jitterAbsSumUs / js.intervalCount));
  } else {
    printKVU32("drdy_interval_avg_us", 0);
    printKVU32("drdy_jitter_abs_avg_us", 0);
  }
}

void printLeadOffStatusLine() {
  char buf[96];
  snprintf(buf, sizeof(buf), "# LOFF status24=0x%06lX p=0x%02X n=0x%02X header_ok=%d",
           static_cast<unsigned long>(g_lastStatus24), g_lastLeadOffStatP,
           g_lastLeadOffStatN,
           ((g_lastStatus24 & ADS_STATUS_HEADER_MASK) == ADS_STATUS_HEADER_OK) ? 1 : 0);
  printLine(buf);
}

bool busyGuard(const char* what) {
  if (!g_streaming) {
    return false;
  }
  if (g_outputMode == MODE_BIN) {
    emitErrorPacket(ERR_BUSY, 0, 0);
  } else {
    char buf[64];
    snprintf(buf, sizeof(buf), "# ERR BUSY %s STOP first", what ? what : "");
    printLine(buf);
  }
  return true;
}

}  // namespace

bool capturePendingDrdySnapshot(DrdyFrameSnapshot* out) {
  if (!out) {
    return false;
  }

  noInterrupts();
  if (!g_drdyFlag) {
    out->ready = false;
    interrupts();
    return false;
  }
  uint32_t ts = g_lastDrdyTimestampUs;
  uint32_t edges = g_drdyEdgesTotal;
  g_drdyFlag = false;
  interrupts();

  // Deferred interval / jitter math, outside the ISR.
  uint32_t interval = 0;
  uint32_t missedFrame = 0;
  if (g_prevDrdyTimestampUs != 0) {
    interval = static_cast<uint32_t>(ts - g_prevDrdyTimestampUs);
    uint32_t expected = g_expectedPeriodUs ? g_expectedPeriodUs : ADS_DRDY_PERIOD_US;
    if (interval > expected + (expected / 2)) {
      uint32_t missed = (interval / expected);
      if (missed > 1) {
        missedFrame = missed - 1;
        g_missedDrdyTotal += missedFrame;
      }
    }
    uint32_t jitterAbs = u32AbsDiff(interval, expected);
    g_drdyIntervalLastUs = interval;
    if (interval < g_drdyIntervalMinUs) g_drdyIntervalMinUs = interval;
    if (interval > g_drdyIntervalMaxUs) g_drdyIntervalMaxUs = interval;
    g_drdyIntervalCount++;
    g_drdyIntervalSumUs += interval;
    g_drdyJitterAbsLastUs = jitterAbs;
    if (jitterAbs < g_drdyJitterAbsMinUs) g_drdyJitterAbsMinUs = jitterAbs;
    if (jitterAbs > g_drdyJitterAbsMaxUs) g_drdyJitterAbsMaxUs = jitterAbs;
    g_drdyJitterAbsSumUs += jitterAbs;
  }
  g_prevDrdyTimestampUs = ts;
  g_missedDrdyFrame = missedFrame;

  out->ready = true;
  out->drdyTimestampUs = ts;
  out->drdyIntervalUs = interval;
  out->missedDrdyFrame = missedFrame;
  out->missedDrdyTotal = g_missedDrdyTotal;
  out->drdyEdgesTotal = edges;
  g_missedDrdyFrame = 0;
  return true;
}

void captureDrdyJitterSnapshot(DrdyJitterSnapshot* out) {
  if (!out) {
    return;
  }

  noInterrupts();
  out->intervalLastUs = g_drdyIntervalLastUs;
  out->intervalMinUs = g_drdyIntervalMinUs;
  out->intervalMaxUs = g_drdyIntervalMaxUs;
  out->jitterAbsLastUs = g_drdyJitterAbsLastUs;
  out->jitterAbsMinUs = g_drdyJitterAbsMinUs;
  out->jitterAbsMaxUs = g_drdyJitterAbsMaxUs;
  out->intervalCount = g_drdyIntervalCount;
  out->intervalSumUs = g_drdyIntervalSumUs;
  out->jitterAbsSumUs = g_drdyJitterAbsSumUs;
  interrupts();
}

void printHelp() {
  printLine("");
  printLine("EEGFrontier V2 commands:");
  printLine("  HELP");
  printLine("  INFO");
  printLine("  STATS");
  printLine("  REGS            (STOP first when streaming)");
  printLine("  START");
  printLine("  STOP");
  printLine("  MODE BIN");
  printLine("  MODE CSV        (debug, throttled)");
  printLine("  REINIT");
  printLine("  TEST ON | TEST OFF");
  printLine("  SELFTEST");
  printLine("  LOFF ON | LOFF OFF | LOFF STATUS");
  printLine("  SPS 250 | 500 | 1000");
  printLine("  GAIN 1|2|4|6|8|12|24");
  printLine("  VREF 4500 (fixed internal reference)");
  printLine("  PING [seq]");
  printLine("");
}

void printInfo() {
  // In BIN+streaming, ASCII would glue to the next COBS frame on the host
  // (it scans for 0x00 first), so report state as events only. Use STATS
  // for live ASCII, or STOP first for full INFO.
  if (g_streaming && g_outputMode == MODE_BIN) {
    emitEventPacket(EVT_CONFIG, g_sampleRateSps, g_adsGain, g_adsVrefUv / 1000UL);
    emitEventPacket(EVT_STREAM_STATE, 1, g_sessionId, g_sampleIndex);
    emitErrorPacket(ERR_BUSY, 0, 0);
    return;
  }
  // Reading ID touches SPI; refuse while streaming so RDATAC is never
  // corrupted. STATS stays available because it never touches the ADC.
  uint32_t cachedId = 0xFFFFFFFFUL;
  bool wasStreaming = g_streaming;
  bool restore = false;
  if (!wasStreaming && g_adsReady) {
    adsSendCommand(CMD_SDATAC);
    delayMicroseconds(10);
    cachedId = adsReadRegister(REG_ID);
  }

  printLine("# EEGFrontier V2");
  printKV("firmware", FW_VERSION);
  printKV("transport", (g_outputMode == MODE_BIN) ? "bin+cobs+crc16" : "csv(debug,throttled)");
  printKVU32("proto_ver", PROTO_VER);
  printKVU32("serial_baud", SERIAL_BAUD);
  printKVU32("spi_hz", SPI_CLOCK_HZ);
  printKVU32("sample_rate_sps", g_sampleRateSps);
  printKVU32("drdy_expected_period_us", g_expectedPeriodUs);
  printKVU32("ads_vref_uv", g_adsVrefUv);
  printKVU32("ads_gain", g_adsGain);
  printKVU32("session_id", g_sessionId);
  printKVU32("streaming", g_streaming ? 1 : 0);
  printKVU32("ads_ready", g_adsReady ? 1 : 0);
  printKVU32("test_signal", g_internalTestSignalEnabled ? 1 : 0);
  printKVU32("loff_diag", g_leadOffDiagEnabled ? 1 : 0);
  printKVU32("recoveries_total", g_recoveriesTotal);
  printKVU32("status_invalid_total", g_statusInvalidTotal);
  printKVU32("lead_off_any_total", g_leadOffAnyTotal);
  printKVU32("tx_bytes_dropped_total", g_txBytesDroppedTotal);
  printKVU32("tx_packets_dropped_total", g_txPacketsDroppedTotal);
  printKVU32("tx_queued_bytes", static_cast<uint32_t>(txQueuedBytes()));
  printKVU32("tx_max_queued_bytes", g_txMaxQueuedBytes);
  printKVU32("watchdog_supported", g_watchdogSupported ? 1 : 0);
  printKVU32("watchdog_enabled", g_watchdogEnabled ? 1 : 0);
  printKVU32("watchdog_reboot_detected", g_watchdogRebootDetected ? 1 : 0);
  printKVU32("watchdog_timeout_ms", g_watchdogTimeoutMs);
  printKVU32("watchdog_feeds_total", g_watchdogFeedsTotal);
  printKVU32("last_drdy_to_process_latency_us", g_lastDrdyToProcessLatencyUs);

  uint32_t drdyEdgesTotal = 0;
  uint32_t missedTotal = 0;
  uint32_t lastDrdyUs = 0;
  noInterrupts();
  drdyEdgesTotal = g_drdyEdgesTotal;
  lastDrdyUs = g_lastDrdyTimestampUs;
  interrupts();
  missedTotal = g_missedDrdyTotal;
  printKVU32("drdy_edges_total", drdyEdgesTotal);
  printKVU32("missed_drdy_total", missedTotal);
  printKVU32("last_drdy_us", lastDrdyUs);

  printJitterSummary();

  printKVU32("last_status24", g_lastStatus24);
  printKVU32("last_loff_statp", g_lastLeadOffStatP);
  printKVU32("last_loff_statn", g_lastLeadOffStatN);

  printKVU32("pin_reset", PIN_EEG_RESET);
  printKVU32("pin_start", PIN_EEG_START);
  printKVU32("pin_drdy", PIN_EEG_DRDY);
  printKVU32("pin_btn", PIN_BTN_START);
  printKVU32("pin_led", PIN_LED_STREAM);
  printKVU32("pin_cs", PIN_SPI_CS);
  printKVU32("pin_sck", PIN_SPI_SCK);
  printKVU32("pin_miso", PIN_SPI_MISO);
  printKVU32("pin_mosi", PIN_SPI_MOSI);
  if (cachedId != 0xFFFFFFFFUL) {
    printKVU32("ads_id", cachedId);
  } else if (!wasStreaming) {
    printKVU32("ads_id", adsReadRegister(REG_ID));
  } else {
    printLine("# ads_id=busy (STOP to read registers)");
  }
  printLine("# hardware_isolation=USB_to_EEG_SPI_and_power");
  printLine("# WARN research hardware; isolation is not IEC 60601-1 certified");
  (void)restore;
}

void printStats() {
  printLine("# STATS");
  printKVU32("sample_index", g_sampleIndex);
  printKVU32("session_id", g_sessionId);
  printKVU32("recoveries_total", g_recoveriesTotal);
  printKVU32("status_invalid_total", g_statusInvalidTotal);
  printKVU32("lead_off_any_total", g_leadOffAnyTotal);
  printKVU32("tx_bytes_dropped_total", g_txBytesDroppedTotal);
  printKVU32("tx_packets_dropped_total", g_txPacketsDroppedTotal);
  printKVU32("tx_queued_bytes", static_cast<uint32_t>(txQueuedBytes()));
  printKVU32("tx_free_bytes", static_cast<uint32_t>(txFreeBytes()));
  printKVU32("tx_max_queued_bytes", g_txMaxQueuedBytes);
  printKVU32("last_process_us", g_lastSampleProcessUs);
  printKVU32("last_drdy_to_process_latency_us", g_lastDrdyToProcessLatencyUs);
  printJitterSummary();
  printLeadOffStatusLine();
}

void dumpRegisters() {
  if (busyGuard("REGS")) {
    return;
  }
  uint8_t regs[0x18];
  if (g_adsReady) {
    adsSendCommand(CMD_SDATAC);
    delayMicroseconds(10);
  }
  adsReadRegisters(0x00, 0x18, regs);

  printLine("# REG_DUMP_BEGIN");
  for (uint8_t i = 0; i < 0x18; i++) {
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%02X,0x%02X", i, regs[i]);
    printLine(buf);
  }
  printLine("# REG_DUMP_END");
}

static uint32_t parseU32Arg(const char* s, bool* ok) {
  if (ok) {
    *ok = false;
  }
  if (!s || !*s) {
    return 0;
  }
  while (*s == ' ' || *s == '\t') {
    s++;
  }
  uint32_t v = 0;
  bool any = false;
  while (*s >= '0' && *s <= '9') {
    any = true;
    v = v * 10 + static_cast<uint32_t>(*s - '0');
    s++;
  }
  if (any && (*s == '\0' || *s == ' ' || *s == '\t')) {
    if (ok) {
      *ok = true;
    }
    return v;
  }
  return 0;
}

void processCommand(char* cmd) {
  while (*cmd == ' ' || *cmd == '\t') {
    cmd++;
  }
  if (*cmd == '\0') {
    return;
  }

  for (char* p = cmd; *p; ++p) {
    if (*p >= 'a' && *p <= 'z') {
      *p = static_cast<char>(*p - 32);
    }
  }

  if (std::strcmp(cmd, "HELP") == 0 || std::strcmp(cmd, "?") == 0) {
    printHelp();
    return;
  }

  if (std::strncmp(cmd, "PING", 4) == 0 &&
      (cmd[4] == '\0' || cmd[4] == ' ' || cmd[4] == '\t')) {
    bool ok = false;
    uint32_t seq = 0;
    if (cmd[4] != '\0') {
      seq = parseU32Arg(cmd + 5, &ok);
    }
    if (g_outputMode == MODE_BIN) {
      emitEventPacket(EVT_PONG, ok ? seq : 0, micros(), 0);
    } else {
      if (ok) {
        char buf[48];
        snprintf(buf, sizeof(buf), "# PONG seq=%lu", static_cast<unsigned long>(seq));
        printLine(buf);
      } else {
        printLine("# PONG");
      }
    }
    return;
  }

  if (std::strcmp(cmd, "INFO") == 0) {
    printInfo();
    return;
  }

  if (std::strcmp(cmd, "STATS") == 0) {
    printStats();
    return;
  }

  if (std::strcmp(cmd, "REGS") == 0) {
    dumpRegisters();
    return;
  }

  if (std::strcmp(cmd, "START") == 0) {
    adsStartStreaming();
    return;
  }

  if (std::strcmp(cmd, "STOP") == 0) {
    adsStopStreaming();
    return;
  }

  if (std::strcmp(cmd, "REINIT") == 0) {
    bool wasStreaming = g_streaming;
    if (g_streaming) {
      adsStopStreaming();
    }
    adsInitRobust();
    if (wasStreaming) {
      adsStartStreaming();
    }
    return;
  }

  if (std::strcmp(cmd, "MODE BIN") == 0) {
    if (g_streaming) {
      adsStopStreaming();
    }
    g_outputMode = MODE_BIN;
    printLine("# OK MODE BIN");
    return;
  }

  if (std::strcmp(cmd, "MODE CSV") == 0) {
#if !CSV_DEBUG_ENABLED
    printLine("# ERR CSV_DISABLED");
    return;
#else
    if (g_streaming) {
      adsStopStreaming();
    }
    g_outputMode = MODE_CSV;
    printLine("# OK MODE CSV");
    printLine("# WARN CSV_DEBUG_ONLY throttled when USB backs up");
    return;
#endif
  }

  if (std::strcmp(cmd, "TEST ON") == 0) {
    bool wasStreaming = g_streaming;
    if (g_streaming) {
      adsStopStreaming();
    }
    if (adsSetInternalTestSignal(true)) {
      printLine("# OK TEST ON");
      if (wasStreaming) {
        adsStartStreaming();
      }
    } else {
      printLine("# ERR TEST_ON_FAIL");
    }
    return;
  }

  if (std::strcmp(cmd, "TEST OFF") == 0) {
    bool wasStreaming = g_streaming;
    if (g_streaming) {
      adsStopStreaming();
    }
    if (adsSetInternalTestSignal(false)) {
      printLine("# OK TEST OFF");
      if (wasStreaming) {
        adsStartStreaming();
      }
    } else {
      printLine("# ERR TEST_OFF_FAIL");
    }
    return;
  }

  if (std::strcmp(cmd, "SELFTEST") == 0) {
    printLine("# SELFTEST RUNNING");
    bool ok = adsRunInternalSelfTest(32);
    printLine(ok ? "# SELFTEST PASS" : "# SELFTEST FAIL");
    return;
  }

  if (std::strcmp(cmd, "LOFF ON") == 0) {
    bool wasStreaming = g_streaming;
    if (g_streaming) {
      adsStopStreaming();
    }
    if (adsSetLeadOffDiagnostics(true)) {
      printLine("# OK LOFF ON");
      if (wasStreaming) {
        adsStartStreaming();
      }
    } else {
      printLine("# ERR LOFF_ON_FAIL");
    }
    return;
  }

  if (std::strcmp(cmd, "LOFF OFF") == 0) {
    bool wasStreaming = g_streaming;
    if (g_streaming) {
      adsStopStreaming();
    }
    if (adsSetLeadOffDiagnostics(false)) {
      printLine("# OK LOFF OFF");
      if (wasStreaming) {
        adsStartStreaming();
      }
    } else {
      printLine("# ERR LOFF_OFF_FAIL");
    }
    return;
  }

  if (std::strcmp(cmd, "LOFF STATUS") == 0) {
    printLeadOffStatusLine();
    return;
  }

  if (std::strncmp(cmd, "SPS", 3) == 0 && (cmd[3] == ' ' || cmd[3] == '\t')) {
    bool ok = false;
    uint32_t v = parseU32Arg(cmd + 4, &ok);
    if (!ok || !adsSetSampleRate(v)) {
      printLine("# ERR SPS use 250|500|1000");
    } else {
      char buf[40];
      snprintf(buf, sizeof(buf), "# OK SPS %lu", static_cast<unsigned long>(g_sampleRateSps));
      printLine(buf);
    }
    return;
  }

  if (std::strncmp(cmd, "GAIN", 4) == 0 && (cmd[4] == ' ' || cmd[4] == '\t')) {
    bool ok = false;
    uint32_t v = parseU32Arg(cmd + 5, &ok);
    if (!ok || !adsSetGain(static_cast<uint8_t>(v))) {
      printLine("# ERR GAIN use 1|2|4|6|8|12|24");
    } else {
      char buf[40];
      snprintf(buf, sizeof(buf), "# OK GAIN %u", g_adsGain);
      printLine(buf);
    }
    return;
  }

  if (std::strncmp(cmd, "VREF", 4) == 0 && (cmd[4] == ' ' || cmd[4] == '\t')) {
    bool ok = false;
    uint32_t v = parseU32Arg(cmd + 5, &ok);
    // Accept millivolts (4500) or microvolts (4500000).
    uint32_t asUv = v;
    if (ok && v <= 10000UL) {
      asUv = v * 1000UL;
    }
    if (!ok || !adsSetVrefUv(asUv)) {
      printLine("# ERR VREF fixed at 4500 mV by ADS1299 internal reference");
    } else {
      char buf[48];
      snprintf(buf, sizeof(buf), "# OK VREF %lu",
               static_cast<unsigned long>(g_adsVrefUv / 1000UL));
      printLine(buf);
    }
    return;
  }

  if (g_streaming && g_outputMode == MODE_BIN) {
    emitErrorPacket(ERR_BUSY, 0, 0);
    return;
  }
  printLine("# ERR UNKNOWN_CMD");
  printLine(cmd);
}

void handleSerialCommands() {
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      g_cmdBuf[g_cmdLen] = '\0';
      processCommand(g_cmdBuf);
      g_cmdLen = 0;
      continue;
    }

    if (g_cmdLen < sizeof(g_cmdBuf) - 1) {
      g_cmdBuf[g_cmdLen++] = c;
    } else {
      g_cmdLen = 0;
      printLine("# ERR CMD_TOO_LONG");
    }
  }
}

void handleButton() {
  bool nowState = digitalRead(PIN_BTN_START);
  uint32_t now = millis();

  if (g_lastBtnState == HIGH && nowState == LOW) {
    // Press start: debounce, then wait to distinguish short vs long press.
    if (static_cast<uint32_t>(now - g_lastButtonToggleMs) > 60) {
      g_buttonPressStartMs = now;
      g_buttonLongFired = false;
    }
  }

  if (g_lastBtnState == LOW && nowState == LOW && !g_buttonLongFired) {
    if (static_cast<uint32_t>(now - g_buttonPressStartMs) > 1500) {
      g_buttonLongFired = true;
      g_lastButtonToggleMs = now;
      g_pendingBtnFlag = true;
      printLine("# SELFTEST RUNNING (button)");
      bool ok = adsRunInternalSelfTest(32);
      printLine(ok ? "# SELFTEST PASS" : "# SELFTEST FAIL");
    }
  }

  if (g_lastBtnState == LOW && nowState == HIGH) {
    if (!g_buttonLongFired &&
        static_cast<uint32_t>(now - g_lastButtonToggleMs) > 250 &&
        static_cast<uint32_t>(now - g_buttonPressStartMs) > 60) {
      g_lastButtonToggleMs = now;
      g_pendingBtnFlag = true;
      if (g_streaming) {
        adsStopStreaming();
      } else {
        adsStartStreaming();
      }
    }
  }

  g_lastBtnState = nowState;
}

void onDrdyFalling() {
  // Keep this minimal: timestamp and count only. Everything else runs
  // deferred in capturePendingDrdySnapshot().
  uint32_t nowUs = micros();
  g_drdyEdgesTotal++;
  g_lastDrdyTimestampUs = nowUs;
  g_drdyFlag = true;
}
