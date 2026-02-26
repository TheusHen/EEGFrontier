#include "ads1299_driver.h"

#include <climits>
#include <SPI.h>

#include "fw_commands.h"
#include "fw_config.h"
#include "fw_protocol.h"
#include "fw_state.h"
#include "fw_tx.h"
#include "fw_utils.h"
#include "fw_watchdog.h"

namespace {

constexpr uint8_t ADS_CH_NORMAL_24X = 0x60;
constexpr uint8_t ADS_CH_TEST_24X = 0x65;   // MUX=test signal
constexpr uint8_t ADS_CONFIG2_NORMAL = 0xD0;
constexpr uint8_t ADS_CONFIG2_TEST_FAST = 0xD3;  // internal test enabled (validate on hardware)
constexpr uint8_t ADS_LOFF_DIAG_CFG = 0x13;      // conservative diagnostic preset
constexpr uint8_t ADS_LOFF_ALL_4CH_MASK = 0x0F;

static inline void adsSelect() { digitalWrite(PIN_SPI_CS, LOW); }
static inline void adsDeselect() { digitalWrite(PIN_SPI_CS, HIGH); }

uint8_t adsChannelConfigValue() {
  return g_internalTestSignalEnabled ? ADS_CH_TEST_24X : ADS_CH_NORMAL_24X;
}

uint8_t adsConfig2Value() {
  return g_internalTestSignalEnabled ? ADS_CONFIG2_TEST_FAST : ADS_CONFIG2_NORMAL;
}

uint32_t statusLeadOffP(uint32_t status24) {
  return (status24 >> 8) & 0xFF;
}

uint32_t statusLeadOffN(uint32_t status24) {
  return status24 & 0xFF;
}

bool statusHeaderValid(uint32_t status24) {
  return (status24 & ADS_STATUS_HEADER_MASK) == ADS_STATUS_HEADER_OK;
}

bool waitForDrdyEdgeLow(uint32_t timeoutUs) {
  uint32_t start = micros();
  while (digitalRead(PIN_EEG_DRDY) != LOW) {
    fwWatchdogFeed();
    txService();
    if (static_cast<uint32_t>(micros() - start) > timeoutUs) {
      return false;
    }
  }
  return true;
}

void waitDrdyReturnHigh(uint32_t timeoutUs) {
  uint32_t start = micros();
  while (digitalRead(PIN_EEG_DRDY) == LOW) {
    if (static_cast<uint32_t>(micros() - start) > timeoutUs) {
      break;
    }
  }
}

void parseChannelsFromFrame(const uint8_t* frame, int32_t* ch1, int32_t* ch2, int32_t* ch3, int32_t* ch4) {
  *ch1 = signExtend24((static_cast<uint32_t>(frame[3]) << 16) |
                      (static_cast<uint32_t>(frame[4]) << 8) |
                      frame[5]);
  *ch2 = signExtend24((static_cast<uint32_t>(frame[6]) << 16) |
                      (static_cast<uint32_t>(frame[7]) << 8) |
                      frame[8]);
  *ch3 = signExtend24((static_cast<uint32_t>(frame[9]) << 16) |
                      (static_cast<uint32_t>(frame[10]) << 8) |
                      frame[11]);
  *ch4 = signExtend24((static_cast<uint32_t>(frame[12]) << 16) |
                      (static_cast<uint32_t>(frame[13]) << 8) |
                      frame[14]);
}

void resetStreamEdgeStats() {
  noInterrupts();
  g_drdyFlag = false;
  g_missedDrdyFrame = 0;
  g_prevDrdyTimestampUs = 0;
  g_lastDrdyTimestampUs = 0;
  g_drdyIntervalLastUs = 0;
  g_drdyIntervalMinUs = 0xFFFFFFFFUL;
  g_drdyIntervalMaxUs = 0;
  g_drdyJitterAbsLastUs = 0;
  g_drdyJitterAbsMinUs = 0xFFFFFFFFUL;
  g_drdyJitterAbsMaxUs = 0;
  g_drdyIntervalCount = 0;
  g_drdyIntervalSumUs = 0;
  g_drdyJitterAbsSumUs = 0;
  interrupts();
}

bool writeChannelMuxAll(uint8_t channelRegValue) {
  adsWriteRegister(REG_CH1SET, channelRegValue);
  adsWriteRegister(REG_CH2SET, channelRegValue);
  adsWriteRegister(REG_CH3SET, channelRegValue);
  adsWriteRegister(REG_CH4SET, channelRegValue);

  return (adsReadRegister(REG_CH1SET) == channelRegValue) &&
         (adsReadRegister(REG_CH2SET) == channelRegValue) &&
         (adsReadRegister(REG_CH3SET) == channelRegValue) &&
         (adsReadRegister(REG_CH4SET) == channelRegValue);
}

}  // namespace

int32_t adsCountsToMicrovolts(int32_t counts) {
  // ADS1299 LSB ~= Vref / (gain * (2^23 - 1))
  constexpr int64_t kFullScaleCode = 8388607LL;
  if (g_adsGain == 0) {
    return 0;
  }
  int64_t numerator = static_cast<int64_t>(counts) * static_cast<int64_t>(g_adsVrefUv);
  int64_t denominator = static_cast<int64_t>(g_adsGain) * kFullScaleCode;
  return static_cast<int32_t>(numerator / denominator);
}

void adsSendCommand(uint8_t cmd) {
  SPI.beginTransaction(g_spiSettings);
  adsSelect();
  SPI.transfer(cmd);
  adsDeselect();
  SPI.endTransaction();
  delayMicroseconds(4);
}

uint8_t adsReadRegister(uint8_t reg) {
  SPI.beginTransaction(g_spiSettings);
  adsSelect();
  SPI.transfer(0x20 | (reg & 0x1F));
  SPI.transfer(0x00);
  delayMicroseconds(2);
  uint8_t v = SPI.transfer(0x00);
  adsDeselect();
  SPI.endTransaction();
  delayMicroseconds(2);
  return v;
}

void adsWriteRegister(uint8_t reg, uint8_t value) {
  SPI.beginTransaction(g_spiSettings);
  adsSelect();
  SPI.transfer(0x40 | (reg & 0x1F));
  SPI.transfer(0x00);
  SPI.transfer(value);
  adsDeselect();
  SPI.endTransaction();
  delayMicroseconds(2);
}

void adsReadRegisters(uint8_t startReg, uint8_t count, uint8_t* dest) {
  SPI.beginTransaction(g_spiSettings);
  adsSelect();
  SPI.transfer(0x20 | (startReg & 0x1F));
  SPI.transfer(count - 1);
  delayMicroseconds(2);
  for (uint8_t i = 0; i < count; i++) {
    dest[i] = SPI.transfer(0x00);
  }
  adsDeselect();
  SPI.endTransaction();
  delayMicroseconds(2);
}

void adsHardwareReset() {
  digitalWrite(PIN_EEG_RESET, HIGH);
  delay(5);
  digitalWrite(PIN_EEG_RESET, LOW);
  delay(10);
  digitalWrite(PIN_EEG_RESET, HIGH);
  delay(25);
}

bool adsConfigureRegisters() {
  adsSendCommand(CMD_SDATAC);
  delay(5);

  adsWriteRegister(REG_CONFIG1, 0x96);                 // HR, 250 SPS
  adsWriteRegister(REG_CONFIG2, adsConfig2Value());    // normal or internal test
  adsWriteRegister(REG_CONFIG3, 0xEC);
  adsWriteRegister(REG_LOFF, g_leadOffDiagEnabled ? ADS_LOFF_DIAG_CFG : 0x00);

  if (!writeChannelMuxAll(adsChannelConfigValue())) {
    return false;
  }

  adsWriteRegister(REG_BIAS_SENSP, 0x0F);
  adsWriteRegister(REG_BIAS_SENSN, 0x0F);

  adsWriteRegister(REG_LOFF_SENSP, g_leadOffDiagEnabled ? ADS_LOFF_ALL_4CH_MASK : 0x00);
  adsWriteRegister(REG_LOFF_SENSN, g_leadOffDiagEnabled ? ADS_LOFF_ALL_4CH_MASK : 0x00);

  adsWriteRegister(REG_GPIO, 0x0C);
  adsWriteRegister(REG_MISC1, 0x00);
  adsWriteRegister(REG_MISC2, 0x00);
  adsWriteRegister(REG_CONFIG4, 0x00);

  delay(2);

  if (adsReadRegister(REG_CONFIG1) != 0x96) return false;
  if (adsReadRegister(REG_CONFIG2) != adsConfig2Value()) return false;
  if (adsReadRegister(REG_CONFIG3) != 0xEC) return false;
  if (adsReadRegister(REG_LOFF) != (g_leadOffDiagEnabled ? ADS_LOFF_DIAG_CFG : 0x00)) return false;
  if (adsReadRegister(REG_LOFF_SENSP) != (g_leadOffDiagEnabled ? ADS_LOFF_ALL_4CH_MASK : 0x00)) return false;
  if (adsReadRegister(REG_LOFF_SENSN) != (g_leadOffDiagEnabled ? ADS_LOFF_ALL_4CH_MASK : 0x00)) return false;
  if (!writeChannelMuxAll(adsChannelConfigValue())) return false;

  g_sampleRateSps = ADS_DEFAULT_SPS;
  g_adsGain = ADS_DEFAULT_GAIN;
  g_adsVrefUv = ADS_VREF_UV;
  return true;
}

bool adsInitOnce() {
  digitalWrite(PIN_EEG_START, LOW);
  adsHardwareReset();

  adsSendCommand(CMD_SDATAC);
  delay(5);

  uint8_t id = adsReadRegister(REG_ID);
  if (id == 0x00 || id == 0xFF) {
    return false;
  }

  if (!adsConfigureRegisters()) {
    return false;
  }

  return true;
}

bool adsInitRobust(uint8_t attempts) {
  for (uint8_t i = 0; i < attempts; i++) {
    fwWatchdogFeed();
    if (adsInitOnce()) {
      g_adsReady = true;
      if (g_outputMode == MODE_BIN) {
        emitEventPacket(0x10, adsReadRegister(REG_ID), i + 1, 0);
      } else {
        Serial.print("# ADS_INIT_OK attempt=");
        Serial.println(i + 1);
      }
      return true;
    }
    delay(20);
  }

  g_adsReady = false;
  if (g_outputMode == MODE_BIN) {
    emitErrorPacket(0xE1, 0, 0);
  } else {
    Serial.println("# ERR ADS_INIT_FAIL");
  }
  return false;
}

bool adsSetInternalTestSignal(bool enable) {
  bool old = g_internalTestSignalEnabled;
  g_internalTestSignalEnabled = enable;
  if (!adsConfigureRegisters()) {
    g_internalTestSignalEnabled = old;
    (void)adsConfigureRegisters();
    return false;
  }
  return true;
}

bool adsSetLeadOffDiagnostics(bool enable) {
  bool old = g_leadOffDiagEnabled;
  g_leadOffDiagEnabled = enable;
  if (!adsConfigureRegisters()) {
    g_leadOffDiagEnabled = old;
    (void)adsConfigureRegisters();
    return false;
  }
  return true;
}

void adsStartStreaming() {
  if (!g_adsReady) {
    if (!adsInitRobust()) {
      return;
    }
  }

  resetStreamEdgeStats();
  g_sampleIndex = 0;
  g_lastGoodFrameUs = micros();

  adsSendCommand(CMD_SDATAC);
  delayMicroseconds(10);

  digitalWrite(PIN_EEG_START, HIGH);
  adsSendCommand(CMD_START);
  delayMicroseconds(10);
  adsSendCommand(CMD_RDATAC);
  delayMicroseconds(10);

  g_streaming = true;
  digitalWrite(PIN_LED_STREAM, HIGH);

  if (g_outputMode == MODE_CSV) {
    Serial.println("sample,drdy_t_us,proc_t_us,drdy_interval_us,status,ch1,ch2,ch3,ch4,ch1_uv,ch2_uv,ch3_uv,ch4_uv,flags,missed_drdy_frame,missed_drdy_total,recoveries_total");
    Serial.println("# STREAM_ON");
    Serial.println("# WARN CSV_DEBUG_ONLY");
  } else {
    emitEventPacket(0x01, 1, 0, 0);
  }
}

void adsStopStreaming() {
  adsSendCommand(CMD_SDATAC);
  delayMicroseconds(10);
  adsSendCommand(CMD_STOP);
  digitalWrite(PIN_EEG_START, LOW);

  g_streaming = false;
  digitalWrite(PIN_LED_STREAM, LOW);

  if (g_outputMode == MODE_CSV) {
    Serial.println("# STREAM_OFF");
  } else {
    emitEventPacket(0x01, 0, 0, 0);
  }
}

bool adsReadDataFrame15(uint8_t* frame) {
  if (!frame) {
    return false;
  }

  SPI.beginTransaction(g_spiSettings);
  adsSelect();
  for (uint8_t i = 0; i < 15; i++) {
    frame[i] = SPI.transfer(0x00);
  }
  adsDeselect();
  SPI.endTransaction();
  return true;
}

bool handleOneSampleFrame() {
  DrdyFrameSnapshot snap = {};
  if (!capturePendingDrdySnapshot(&snap)) {
    return false;
  }

  if (!adsReadDataFrame15(g_rawFrame)) {
    if (g_outputMode == MODE_BIN) {
      emitErrorPacket(0xE2, 0, 0);
    } else {
      Serial.println("# ERR FRAME_READ_FAIL");
    }
    return false;
  }

  uint32_t status24 =
      (static_cast<uint32_t>(g_rawFrame[0]) << 16) |
      (static_cast<uint32_t>(g_rawFrame[1]) << 8) |
      static_cast<uint32_t>(g_rawFrame[2]);

  int32_t ch1 = 0, ch2 = 0, ch3 = 0, ch4 = 0;
  parseChannelsFromFrame(g_rawFrame, &ch1, &ch2, &ch3, &ch4);

  uint32_t flags = 0;
  if (g_streaming) flags |= FLAG_STREAMING;
  if (g_pendingRecoveredFlag) flags |= FLAG_RECOVERED;
  if (g_pendingBtnFlag) flags |= FLAG_BTN_TOGGLED;
  if (snap.missedDrdyFrame > 0) flags |= FLAG_DRDY_MISSED;
  if (g_pendingTxOverflowFlag) flags |= FLAG_TX_OVERFLOW;

  bool headerOk = statusHeaderValid(status24);
  uint8_t loffP = static_cast<uint8_t>(statusLeadOffP(status24));
  uint8_t loffN = static_cast<uint8_t>(statusLeadOffN(status24));
  bool loffAny = ((loffP | loffN) != 0);

  g_lastStatus24 = status24;
  g_lastLeadOffStatP = loffP;
  g_lastLeadOffStatN = loffN;

  if (!headerOk) {
    flags |= FLAG_STATUS_INVALID;
    g_statusInvalidTotal++;
  }
  if (loffAny) {
    flags |= FLAG_ADS_LOFF_ANY;
    g_leadOffAnyTotal++;
  }

  uint32_t procUs = micros();
  uint32_t sampleTimestampUs = (snap.drdyTimestampUs != 0) ? snap.drdyTimestampUs : procUs;
  g_lastGoodFrameUs = procUs;
  g_lastSampleProcessUs = procUs;
  g_lastDrdyToProcessLatencyUs = static_cast<uint32_t>(procUs - sampleTimestampUs);

  bool emitted = true;
  if (g_outputMode == MODE_BIN) {
    emitted = emitSamplePacket(sampleTimestampUs, status24, ch1, ch2, ch3, ch4, flags,
                               snap.missedDrdyFrame, g_recoveriesTotal);
  } else {
    emitCsvFrame(sampleTimestampUs, procUs, snap.drdyIntervalUs, status24,
                 ch1, ch2, ch3, ch4,
                 adsCountsToMicrovolts(ch1), adsCountsToMicrovolts(ch2),
                 adsCountsToMicrovolts(ch3), adsCountsToMicrovolts(ch4),
                 flags, snap.missedDrdyFrame, snap.missedDrdyTotal, g_recoveriesTotal);
  }

  g_pendingRecoveredFlag = false;
  g_pendingBtnFlag = false;
  if (emitted) {
    g_pendingTxOverflowFlag = false;
  }
  return true;
}

void recoverAdsIfNeeded() {
  if (!g_streaming) {
    return;
  }

  uint32_t periodUs = (g_sampleRateSps > 0) ? (1000000UL / g_sampleRateSps) : ADS_DRDY_PERIOD_US;
  uint32_t timeoutUs = max(50000UL, periodUs * 8UL);

  uint32_t nowUs = micros();
  if (static_cast<uint32_t>(nowUs - g_lastGoodFrameUs) < timeoutUs) {
    return;
  }

  bool wasStreaming = g_streaming;
  adsStopStreaming();

  if (g_outputMode == MODE_BIN) {
    emitErrorPacket(0xE3, nowUs, g_recoveriesTotal);
  } else {
    Serial.println("# WARN DRDY_TIMEOUT_RECOVER");
  }

  if (adsInitRobust()) {
    g_recoveriesTotal++;
    g_pendingRecoveredFlag = true;
    if (wasStreaming) {
      adsStartStreaming();
    }
  }
}

bool adsRunInternalSelfTest(uint8_t frames) {
  if (frames == 0) {
    frames = 32;
  }

  bool wasStreaming = g_streaming;
  bool oldTest = g_internalTestSignalEnabled;
  bool oldLoff = g_leadOffDiagEnabled;

  if (g_streaming) {
    adsStopStreaming();
  }

  if (!g_adsReady && !adsInitRobust()) {
    return false;
  }

  if (g_leadOffDiagEnabled) {
    if (!adsSetLeadOffDiagnostics(false)) {
      return false;
    }
  }

  if (!adsSetInternalTestSignal(true)) {
    return false;
  }

  resetStreamEdgeStats();
  adsSendCommand(CMD_SDATAC);
  delayMicroseconds(10);
  digitalWrite(PIN_EEG_START, HIGH);
  adsSendCommand(CMD_START);
  delayMicroseconds(10);
  adsSendCommand(CMD_RDATAC);
  delayMicroseconds(10);

  int32_t minCh[4] = {INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX};
  int32_t maxCh[4] = {INT32_MIN, INT32_MIN, INT32_MIN, INT32_MIN};
  uint8_t statusBad = 0;
  uint8_t goodFrames = 0;

  for (uint8_t i = 0; i < frames; i++) {
    fwWatchdogFeed();
    txService();

    if (!waitForDrdyEdgeLow(50000UL)) {
      break;
    }
    if (!adsReadDataFrame15(g_rawFrame)) {
      break;
    }

    uint32_t status24 =
        (static_cast<uint32_t>(g_rawFrame[0]) << 16) |
        (static_cast<uint32_t>(g_rawFrame[1]) << 8) |
        static_cast<uint32_t>(g_rawFrame[2]);
    if (!statusHeaderValid(status24)) {
      statusBad++;
    }

    int32_t c1 = 0, c2 = 0, c3 = 0, c4 = 0;
    parseChannelsFromFrame(g_rawFrame, &c1, &c2, &c3, &c4);
    int32_t vals[4] = {c1, c2, c3, c4};
    for (uint8_t ch = 0; ch < 4; ch++) {
      if (vals[ch] < minCh[ch]) minCh[ch] = vals[ch];
      if (vals[ch] > maxCh[ch]) maxCh[ch] = vals[ch];
    }

    goodFrames++;
    waitDrdyReturnHigh(5000UL);
  }

  adsSendCommand(CMD_SDATAC);
  delayMicroseconds(10);
  adsSendCommand(CMD_STOP);
  digitalWrite(PIN_EEG_START, LOW);

  bool dynamicOk = true;
  for (uint8_t ch = 0; ch < 4; ch++) {
    if (goodFrames == 0) {
      dynamicOk = false;
      break;
    }
    int32_t p2p = maxCh[ch] - minCh[ch];
    if (p2p < 50) {
      dynamicOk = false;
      break;
    }
  }

  bool statusOk = (goodFrames == frames) && (statusBad <= (frames / 4));
  bool overallOk = dynamicOk && statusOk;

  if (g_outputMode == MODE_BIN) {
    emitEventPacket(0x30, overallOk ? 1U : 0U, goodFrames, statusBad);
  } else {
    Serial.print("# SELFTEST good_frames=");
    Serial.print(goodFrames);
    Serial.print(" status_bad=");
    Serial.print(statusBad);
    Serial.print(" result=");
    Serial.println(overallOk ? "PASS" : "FAIL");
  }

  if (oldTest != g_internalTestSignalEnabled) {
    (void)adsSetInternalTestSignal(oldTest);
  } else if (!oldTest) {
    (void)adsSetInternalTestSignal(false);
  }

  if (oldLoff != g_leadOffDiagEnabled) {
    (void)adsSetLeadOffDiagnostics(oldLoff);
  } else if (oldLoff) {
    (void)adsSetLeadOffDiagnostics(true);
  }

  if (wasStreaming) {
    adsStartStreaming();
  }

  return overallOk;
}
