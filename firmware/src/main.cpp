#include <Arduino.h>
#include <SPI.h>

#include "ads1299_driver.h"
#include "fw_commands.h"
#include "fw_config.h"
#include "fw_state.h"
#include "fw_tx.h"
#include "fw_watchdog.h"

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
  delay(1200);
  txInit();

  SPI.setSCK(PIN_SPI_SCK);
  SPI.setTX(PIN_SPI_MOSI);
  SPI.setRX(PIN_SPI_MISO);
  SPI.begin();

  attachInterrupt(digitalPinToInterrupt(PIN_EEG_DRDY), onDrdyFalling, FALLING);

  Serial.println();
  Serial.println("# BOOT EEGFrontier_V1");
  Serial.println("# DEFAULT MODE BIN");
  printHelp();

  fwWatchdogInit(2000);
  adsInitRobust();
  g_lastGoodFrameUs = micros();
}

void loop() {
  fwWatchdogFeed();
  txService();
  handleSerialCommands();
  handleButton();
  txService();

  if (g_streaming) {
    handleOneSampleFrame();
  }

  recoverAdsIfNeeded();
  txService();
}
