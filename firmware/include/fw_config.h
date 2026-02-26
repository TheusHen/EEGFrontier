#pragma once

#include <Arduino.h>

// EEGFrontier V1
//
// PINOUT:
// D0  -> EEG_RESET
// D1  -> EEG_START
// D2  -> EEG_DRDY
// D5  -> BTN_START
// D6  -> LED_STREAM
// D7  -> SPI_CS
// D8  -> SPI_SCK
// D9  -> SPI_MISO
// D10 -> SPI_MOSI
//
// Serial commands:
//   HELP
//   INFO
//   REGS
//   START
//   STOP
//   MODE BIN
//   MODE CSV
//   REINIT
//   PING
//
// BIN protocol:
//   packet = COBS(raw_packet) + 0x00
//   raw_packet = [type][ver][payload...][crc16_le]
//
// Types:
//   0x01 = sample packet
//   0x02 = event/status packet
//   0x7F = error packet

// Pins
constexpr uint8_t PIN_EEG_RESET  = D0;
constexpr uint8_t PIN_EEG_START  = D1;
constexpr uint8_t PIN_EEG_DRDY   = D2;
constexpr uint8_t PIN_BTN_START  = D5;
constexpr uint8_t PIN_LED_STREAM = D6;

constexpr uint8_t PIN_SPI_CS     = D7;
constexpr uint8_t PIN_SPI_SCK    = D8;
constexpr uint8_t PIN_SPI_MISO   = D9;
constexpr uint8_t PIN_SPI_MOSI   = D10;

// Serial / SPI config
constexpr uint32_t SERIAL_BAUD  = 921600;
constexpr uint32_t SPI_CLOCK_HZ = 1000000;
constexpr bool CSV_DEBUG_ENABLED = true;  // CSV is debug-only (heavier transport)

// ADS1299 scaling / timing defaults (V1 config)
constexpr uint32_t ADS_VREF_UV = 4500000UL;
constexpr uint8_t ADS_DEFAULT_GAIN = 24;
constexpr uint32_t ADS_DEFAULT_SPS = 250;
constexpr uint32_t ADS_DRDY_PERIOD_US = 1000000UL / ADS_DEFAULT_SPS;

// ADS1299 commands
constexpr uint8_t CMD_WAKEUP  = 0x02;
constexpr uint8_t CMD_STANDBY = 0x04;
constexpr uint8_t CMD_RESET   = 0x06;
constexpr uint8_t CMD_START   = 0x08;
constexpr uint8_t CMD_STOP    = 0x0A;
constexpr uint8_t CMD_RDATAC  = 0x10;
constexpr uint8_t CMD_SDATAC  = 0x11;
constexpr uint8_t CMD_RDATA   = 0x12;

// ADS1299 registers
constexpr uint8_t REG_ID          = 0x00;
constexpr uint8_t REG_CONFIG1     = 0x01;
constexpr uint8_t REG_CONFIG2     = 0x02;
constexpr uint8_t REG_CONFIG3     = 0x03;
constexpr uint8_t REG_LOFF        = 0x04;
constexpr uint8_t REG_CH1SET      = 0x05;
constexpr uint8_t REG_CH2SET      = 0x06;
constexpr uint8_t REG_CH3SET      = 0x07;
constexpr uint8_t REG_CH4SET      = 0x08;
constexpr uint8_t REG_BIAS_SENSP  = 0x0D;
constexpr uint8_t REG_BIAS_SENSN  = 0x0E;
constexpr uint8_t REG_LOFF_SENSP  = 0x0F;
constexpr uint8_t REG_LOFF_SENSN  = 0x10;
constexpr uint8_t REG_GPIO        = 0x14;
constexpr uint8_t REG_MISC1       = 0x15;
constexpr uint8_t REG_MISC2       = 0x16;
constexpr uint8_t REG_CONFIG4     = 0x17;

// Protocol constants
constexpr uint8_t PKT_SAMPLE = 0x01;
constexpr uint8_t PKT_EVENT  = 0x02;
constexpr uint8_t PKT_ERROR  = 0x7F;
constexpr uint8_t PROTO_VER  = 0x01;

// Flags
constexpr uint32_t FLAG_STREAMING   = (1u << 0);
constexpr uint32_t FLAG_RECOVERED   = (1u << 1);
constexpr uint32_t FLAG_BTN_TOGGLED = (1u << 2);
constexpr uint32_t FLAG_DRDY_MISSED = (1u << 3);
constexpr uint32_t FLAG_STATUS_INVALID = (1u << 4);
constexpr uint32_t FLAG_ADS_LOFF_ANY   = (1u << 5);
constexpr uint32_t FLAG_TX_OVERFLOW    = (1u << 6);

// ADS status word helpers (ADS1299 RDATAC status bytes)
constexpr uint32_t ADS_STATUS_HEADER_MASK = 0xF00000UL;
constexpr uint32_t ADS_STATUS_HEADER_OK   = 0xC00000UL;

enum OutputMode : uint8_t {
  MODE_BIN = 0,
  MODE_CSV = 1
};
