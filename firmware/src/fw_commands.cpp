#include "fw_commands.h"

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
  Serial.print("# LOFF status24=0x");
  Serial.print(g_lastStatus24, HEX);
  Serial.print(" p=0x");
  Serial.print(g_lastLeadOffStatP, HEX);
  Serial.print(" n=0x");
  Serial.print(g_lastLeadOffStatN, HEX);
  Serial.print(" header_ok=");
  Serial.println(((g_lastStatus24 & ADS_STATUS_HEADER_MASK) == ADS_STATUS_HEADER_OK) ? 1 : 0);
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

  out->ready = true;
  out->drdyTimestampUs = g_lastDrdyTimestampUs;
  out->drdyIntervalUs = g_drdyIntervalLastUs;
  out->missedDrdyFrame = g_missedDrdyFrame;
  out->missedDrdyTotal = g_missedDrdyTotal;
  out->drdyEdgesTotal = g_drdyEdgesTotal;

  g_drdyFlag = false;
  g_missedDrdyFrame = 0;
  interrupts();
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
  Serial.println();
  Serial.println("EEGFrontier V1 commands:");
  Serial.println("  HELP");
  Serial.println("  INFO");
  Serial.println("  STATS");
  Serial.println("  REGS");
  Serial.println("  START");
  Serial.println("  STOP");
  Serial.println("  MODE BIN");
  Serial.println("  MODE CSV   (debug)");
  Serial.println("  REINIT");
  Serial.println("  TEST ON");
  Serial.println("  TEST OFF");
  Serial.println("  SELFTEST");
  Serial.println("  LOFF ON");
  Serial.println("  LOFF OFF");
  Serial.println("  LOFF STATUS");
  Serial.println("  PING");
  Serial.println();
}

void printInfo() {
  Serial.println("# EEGFrontier V1");
  printKV("firmware", "robust+diag");
  printKV("transport", (g_outputMode == MODE_BIN) ? "bin+cobs+crc16" : "csv(debug)");
  printKVU32("serial_baud", SERIAL_BAUD);
  printKVU32("spi_hz", SPI_CLOCK_HZ);
  printKVU32("sample_rate_sps", g_sampleRateSps);
  printKVU32("drdy_expected_period_us", (g_sampleRateSps > 0) ? (1000000UL / g_sampleRateSps) : 0);
  printKVU32("ads_vref_uv", g_adsVrefUv);
  printKVU32("ads_gain", g_adsGain);
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
  missedTotal = g_missedDrdyTotal;
  lastDrdyUs = g_lastDrdyTimestampUs;
  interrupts();
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
  printKVU32("ads_id", adsReadRegister(REG_ID));
}

void printStats() {
  Serial.println("# STATS");
  printKVU32("sample_index", g_sampleIndex);
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
  uint8_t regs[0x18];
  adsReadRegisters(0x00, 0x18, regs);

  Serial.println("# REG_DUMP_BEGIN");
  for (uint8_t i = 0; i < 0x18; i++) {
    Serial.print("0x");
    if (i < 16) {
      Serial.print('0');
    }
    Serial.print(i, HEX);
    Serial.print(",0x");
    if (regs[i] < 16) {
      Serial.print('0');
    }
    Serial.println(regs[i], HEX);
  }
  Serial.println("# REG_DUMP_END");
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

  if (std::strcmp(cmd, "PING") == 0) {
    Serial.println("# PONG");
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
    Serial.println("# OK MODE BIN");
    return;
  }

  if (std::strcmp(cmd, "MODE CSV") == 0) {
    if (!CSV_DEBUG_ENABLED) {
      Serial.println("# ERR CSV_DISABLED");
      return;
    }
    if (g_streaming) {
      adsStopStreaming();
    }
    g_outputMode = MODE_CSV;
    Serial.println("# OK MODE CSV");
    Serial.println("# WARN CSV_DEBUG_ONLY");
    return;
  }

  if (std::strcmp(cmd, "TEST ON") == 0) {
    bool wasStreaming = g_streaming;
    if (g_streaming) {
      adsStopStreaming();
    }
    if (adsSetInternalTestSignal(true)) {
      Serial.println("# OK TEST ON");
      if (wasStreaming) {
        adsStartStreaming();
      }
    } else {
      Serial.println("# ERR TEST_ON_FAIL");
    }
    return;
  }

  if (std::strcmp(cmd, "TEST OFF") == 0) {
    bool wasStreaming = g_streaming;
    if (g_streaming) {
      adsStopStreaming();
    }
    if (adsSetInternalTestSignal(false)) {
      Serial.println("# OK TEST OFF");
      if (wasStreaming) {
        adsStartStreaming();
      }
    } else {
      Serial.println("# ERR TEST_OFF_FAIL");
    }
    return;
  }

  if (std::strcmp(cmd, "SELFTEST") == 0) {
    Serial.println("# SELFTEST RUNNING");
    bool ok = adsRunInternalSelfTest(32);
    Serial.println(ok ? "# SELFTEST PASS" : "# SELFTEST FAIL");
    return;
  }

  if (std::strcmp(cmd, "LOFF ON") == 0) {
    bool wasStreaming = g_streaming;
    if (g_streaming) {
      adsStopStreaming();
    }
    if (adsSetLeadOffDiagnostics(true)) {
      Serial.println("# OK LOFF ON");
      if (wasStreaming) {
        adsStartStreaming();
      }
    } else {
      Serial.println("# ERR LOFF_ON_FAIL");
    }
    return;
  }

  if (std::strcmp(cmd, "LOFF OFF") == 0) {
    bool wasStreaming = g_streaming;
    if (g_streaming) {
      adsStopStreaming();
    }
    if (adsSetLeadOffDiagnostics(false)) {
      Serial.println("# OK LOFF OFF");
      if (wasStreaming) {
        adsStartStreaming();
      }
    } else {
      Serial.println("# ERR LOFF_OFF_FAIL");
    }
    return;
  }

  if (std::strcmp(cmd, "LOFF STATUS") == 0) {
    printLeadOffStatusLine();
    return;
  }

  Serial.print("# ERR UNKNOWN_CMD ");
  Serial.println(cmd);
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
      Serial.println("# ERR CMD_TOO_LONG");
    }
  }
}

void handleButton() {
  bool nowState = digitalRead(PIN_BTN_START);

  if (g_lastBtnState == HIGH && nowState == LOW) {
    uint32_t now = millis();
    if (static_cast<uint32_t>(now - g_lastButtonToggleMs) > 250) {
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
  uint32_t nowUs = micros();
  g_drdyEdgesTotal++;
  g_lastDrdyTimestampUs = nowUs;

  if (g_prevDrdyTimestampUs != 0) {
    uint32_t dt = static_cast<uint32_t>(nowUs - g_prevDrdyTimestampUs);
    uint32_t expectedUs = (g_sampleRateSps > 0) ? (1000000UL / g_sampleRateSps) : ADS_DRDY_PERIOD_US;
    uint32_t jitterAbs = u32AbsDiff(dt, expectedUs);

    g_drdyIntervalLastUs = dt;
    if (dt < g_drdyIntervalMinUs) g_drdyIntervalMinUs = dt;
    if (dt > g_drdyIntervalMaxUs) g_drdyIntervalMaxUs = dt;
    g_drdyIntervalCount++;
    g_drdyIntervalSumUs += dt;

    g_drdyJitterAbsLastUs = jitterAbs;
    if (jitterAbs < g_drdyJitterAbsMinUs) g_drdyJitterAbsMinUs = jitterAbs;
    if (jitterAbs > g_drdyJitterAbsMaxUs) g_drdyJitterAbsMaxUs = jitterAbs;
    g_drdyJitterAbsSumUs += jitterAbs;
  }

  g_prevDrdyTimestampUs = nowUs;

  if (g_drdyFlag) {
    g_missedDrdyTotal++;
    g_missedDrdyFrame++;
  } else {
    g_drdyFlag = true;
  }
}

