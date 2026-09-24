#include <Arduino.h>
#include <SPI.h>

#include "ads1299_driver.h"
#include "fw_commands.h"
#include "fw_config.h"
#include "fw_protocol.h"
#include "fw_state.h"
#include "fw_tx.h"
#include "fw_watchdog.h"

namespace {

void blinkCode(uint8_t times, uint16_t onMs = 120) {
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(PIN_LED_STREAM, HIGH);
    delay(onMs);
    digitalWrite(PIN_LED_STREAM, LOW);
    delay(onMs);
  }
}

}  // namespace

void setup() {
  pinMode(PIN_EEG_RESET, OUTPUT);
  pinMode(PIN_EEG_START, OUTPUT);
  pinMode(PIN_SPI_CS, OUTPUT);
  pinMode(PIN_LED_STREAM, OUTPUT);

  pinMode(PIN_EEG_DRDY, INPUT_PULLUP);
  pinMode(PIN_BTN_START, INPUT_PULLUP);

  digitalWrite(PIN_SPI_CS, HIGH);
  digitalWrite(PIN_EEG_RESET, HIGH);
  digitalWrite(PIN_EEG_START, LOW);
  digitalWrite(PIN_LED_STREAM, LOW);

  Serial.begin(SERIAL_BAUD);
  txInit();

  // Give USB CDC a moment to enumerate without stalling forever on
  // battery-powered setups where nobody opens the port.
  uint32_t bootStart = millis();
  while (!Serial && static_cast<uint32_t>(millis() - bootStart) < 400) {
    delay(5);
  }

  SPI.setSCK(PIN_SPI_SCK);
  SPI.setTX(PIN_SPI_MOSI);
  SPI.setRX(PIN_SPI_MISO);
  SPI.begin();

  attachInterrupt(digitalPinToInterrupt(PIN_EEG_DRDY), onDrdyFalling, FALLING);

  fwWatchdogInit(2000);

  printLine("");
  printLine("# BOOT EEGFrontier_V2");
  printLine("# DEFAULT MODE BIN");
  printLine("# hardware_isolation=USB_to_EEG_SPI_and_power");
  printLine("# WARN research hardware; isolation is not IEC 60601-1 certified");
  printHelp();

  if (g_watchdogRebootDetected) {
    emitEventPacket(EVT_HELLO, 0x5242544CUL /*RBOT*/, g_recoveriesTotal, 0);
  }
  emitHelloPacket();
  bool ok = adsInitRobust();
  g_lastGoodFrameUs = micros();
  if (!ok) {
    blinkCode(2);
  }
  txService();
}

void loop() {
  fwWatchdogFeed();
  txService();
  handleSerialCommands();
  handleButton();

  if (g_streaming) {
    // Drain at most one frame per loop so commands stay responsive at
    // 500/1000 SPS. The DRDY flag coalesces overruns into missed counts.
    handleOneSampleFrame();
    txService();
  }

  recoverAdsIfNeeded();

  // Slow status blink while idle with lead-off or errors pending.
  static uint32_t lastIdleBlink = 0;
  if (!g_streaming) {
    uint32_t now = millis();
    if (static_cast<uint32_t>(now - lastIdleBlink) > 2000) {
      lastIdleBlink = now;
      if (!g_adsReady) {
        blinkCode(2, 80);
      }
    }
  }

  txService();
}
