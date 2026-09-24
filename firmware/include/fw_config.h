#pragma once

#include <Arduino.h>

// EEGFrontier V2 isolated board: XIAO RP2040 + ADS1299-4PAGR.
//
// PINOUT (unchanged from V1):
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
// Serial commands (V2):
//   HELP
//   INFO
//   STATS
//   REGS
//   START
//   STOP
//   MODE BIN
//   MODE CSV        (debug only, throttled while streaming)
//   REINIT
//   PING [seq]
//   TEST ON | TEST OFF
//   SELFTEST
//   LOFF ON | LOFF OFF | LOFF STATUS
//   SPS 250 | 500 | 1000
//   GAIN 1 | 2 | 4 | 6 | 8 | 12 | 24
//   VREF 4500 (fixed internal ADS1299 reference)
//
// BIN protocol (unchanged framing, see firmware/PROTOCOL_V2.md):
//   packet = COBS(raw_packet) + 0x00
//   raw_packet = [type][ver][payload...][crc16_le]
//
// Types:
//   0x01 = sample packet (40 bytes raw, layout frozen for Pendulum compat)
//   0x02 = event/status packet
//   0x7F = error packet

// Firmware identity
constexpr char FW_NAME[] = "EEGFrontier";
constexpr char FW_VERSION[] = "2.1.0";
constexpr uint8_t FW_MAJOR = 2;
constexpr uint8_t FW_MINOR = 1;
constexpr uint16_t FW_VERSION_U16 = 0x0201;

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
//
// NOTE: on the RP2040 native USB CDC the baud setting is ignored by the
// host driver; it only matters for real UART bridges. Kept so INFO stays
// honest about what the host asked for, and so serial monitors pick a
// fast default.
constexpr uint32_t SERIAL_BAUD  = 921600;
constexpr uint32_t SPI_CLOCK_HZ = 2000000;
#if !defined(CSV_DEBUG_ENABLED)
#define CSV_DEBUG_ENABLED 1  // CSV stays available but is throttled, debug only
#endif

// ADS1299-4PAGR uses the internal 4.5 V reference on this V2 board.
// AVDD is 5V_A (isolated); DVDD is 3V3_D_ISO. The ADS1299 internal
// reference is fixed at 4.5 V: it is not programmable to 2.4 V.
constexpr uint32_t ADS_VREF_UV_4500 = 4500000UL;
constexpr uint32_t ADS_VREF_UV = ADS_VREF_UV_4500;
constexpr uint8_t ADS_DEFAULT_GAIN = 24;
constexpr uint32_t ADS_DEFAULT_SPS = 250;
constexpr uint32_t ADS_DEFAULT_VREF_UV = ADS_VREF_UV_4500;
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

// Protocol constants (sample layout frozen; events/errors are additive)
constexpr uint8_t PKT_SAMPLE = 0x01;
constexpr uint8_t PKT_EVENT  = 0x02;
constexpr uint8_t PKT_ERROR  = 0x7F;
constexpr uint8_t PROTO_VER  = 0x01;

// Event codes (additive in V2, old hosts ignore unknown ones)
constexpr uint8_t EVT_STREAM_STATE = 0x01;
constexpr uint8_t EVT_HELLO        = 0x02;
constexpr uint8_t EVT_ADS_INIT_OK  = 0x10;
constexpr uint8_t EVT_PONG         = 0x11;
constexpr uint8_t EVT_SELFTEST     = 0x30;
constexpr uint8_t EVT_CONFIG       = 0x31;
constexpr uint8_t EVT_TX_HIGH_WATER = 0x32;

// Error codes (additive in V2)
constexpr uint8_t ERR_ADS_INIT_FAIL   = 0xE1;
constexpr uint8_t ERR_FRAME_READ_FAIL = 0xE2;
constexpr uint8_t ERR_DRDY_TIMEOUT    = 0xE3;
constexpr uint8_t ERR_TX_OVERFLOW     = 0xE4;
constexpr uint8_t ERR_BUSY            = 0xE5;

// Flags (bits 0-6 frozen for Pendulum compat; bit 7+ new in V2)
constexpr uint32_t FLAG_STREAMING   = (1u << 0);
constexpr uint32_t FLAG_RECOVERED   = (1u << 1);
constexpr uint32_t FLAG_BTN_TOGGLED = (1u << 2);
constexpr uint32_t FLAG_DRDY_MISSED = (1u << 3);
constexpr uint32_t FLAG_STATUS_INVALID = (1u << 4);
constexpr uint32_t FLAG_ADS_LOFF_ANY   = (1u << 5);
constexpr uint32_t FLAG_TX_OVERFLOW    = (1u << 6);
constexpr uint32_t FLAG_CONFIG_CHANGED = (1u << 7);

// ADS status word helpers (ADS1299 RDATAC status bytes)
constexpr uint32_t ADS_STATUS_HEADER_MASK = 0xF00000UL;
constexpr uint32_t ADS_STATUS_HEADER_OK   = 0xC00000UL;

enum OutputMode : uint8_t {
  MODE_BIN = 0,
  MODE_CSV = 1
};
