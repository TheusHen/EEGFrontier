# EEGFrontier Firmware V2 Protocol (same V1 board)

Framing is unchanged from V1 so existing Pendulum builds keep working.
V2 only adds event/error codes and documents the current defaults.

## Framing

```
packet = COBS(raw_packet) + 0x00
raw_packet = [type:u8][ver:u8][payload...][crc16_le:u16]
crc = CRC16-CCITT (poly 0x1021, init 0xFFFF) over everything before crc
```

Max raw sizes: sample 44 B, event 17 B, error 13 B. Encoded form never
contains `0x00` except the delimiter.

## Packet types

| type | name | payload | notes |
|------|------|---------|-------|
| 0x01 | SAMPLE | `sample_index:u32 t_us:u32 status24:u32 ch1..ch4:i32 flags:u32 missed_drdy_frame:u32 recoveries_total:u32` | Layout frozen. `sample_index` is monotonic since boot, never reset by START/STOP/recovery. |
| 0x02 | EVENT | `event_code:u8 a:u32 b:u32 c:u32` | Additive codes below. Old hosts log unknown codes as `EVENT`. |
| 0x7F | ERROR | `error_code:u8 a:u32 b:u32` | Additive codes below. |

`ver` is currently `0x01` for all three.

## Flags (sample.flags)

Bits 0-6 are frozen:

- 0 STREAMING, 1 RECOVERED, 2 BTN_TOGGLED, 3 DRDY_MISSED,
  4 STATUS_INVALID, 5 ADS_LOFF_ANY, 6 TX_OVERFLOW

V2 adds bit 7 CONFIG_CHANGED (SPS/GAIN/VREF changed since last frame).

## Events

| code | name | a | b | c |
|------|------|---|---|---|
| 0x01 | STREAM_STATE | 1=on 0=off | session_id or sample_index | sps |
| 0x02 | HELLO | fw + proto | sps | gain + vref |
| 0x10 | ADS_INIT_OK | ads_id | attempt | sps |
| 0x11 | PONG | seq | micros | 0 |
| 0x30 | SELFTEST | 1=pass 0=fail | good_frames | status_bad |
| 0x31 | CONFIG | sps | gain | vref_mv (or min_p2p in selftest follow-up) |
| 0x32 | TX_HIGH_WATER | queued | dropped_packets | 0 |

HELLO packing: `a = (2<<24)|(0<<16)|PROTO_VER`,
`b = sps`, `c = (gain<<24)|(vref_mv & 0xFFFFFF)`.

On boot the device emits `# BOOT` text, `HELLO`, then `ADS_INIT_OK`.
After a watchdog reboot it also emits `HELLO` with `a = 'RBOT'`.

## Errors

| code | name | meaning |
|------|------|---------|
| 0xE1 | ADS_INIT_FAIL | ADC not responding after retries |
| 0xE2 | FRAME_READ_FAIL | SPI read failed |
| 0xE3 | DRDY_TIMEOUT | no frame for 8x period, re-sync follows |
| 0xE4 | TX_OVERFLOW | (reserved, overflow is flagged in-sample) |
| 0xE5 | BUSY | register command refused while streaming |

`REGS` and `INFO` ID reads refuse while streaming (`BUSY`) instead of
corrupting RDATAC. `STATS` never touches SPI and is always safe.

## Serial commands

```
HELP  INFO  STATS  REGS  START  STOP  MODE BIN  MODE CSV
REINIT  TEST ON|OFF  SELFTEST  LOFF ON|OFF|STATUS
SPS 250|500|1000  GAIN 1|2|4|6|8|12|24  VREF 2400|4500
PING [seq]
```

`MODE CSV` is debug only and throttled: when the TX queue passes the
high-water mark, frames are dropped and counted rather than stalling
acquisition.

## Defaults on the V1 board

- SPS 250, GAIN 24, VREF 4500 (legacy default for capture compat).
- VREF 2400 is the recommended operating point because AVDD is 3.3 V;
  select with `VREF 2400` and use the same value for host uV conversion:
  `uV = counts * vref_uv / (gain * 8388607)`.
- SPI 2 MHz, TX ring 16 KB, watchdog 2 s.
- Button: short press toggles stream, 1.5 s hold runs SELFTEST.

## Register map audit (ADS1299, internal 2.048 MHz clock)

- CONFIG1: 0x96=250, 0x95=500, 0x94=1000 (HR mode).
- CONFIG2: 0xD0 normal, 0xD3 test-signal.
- CONFIG3: 0xEC vref 4.5 V, 0xCC vref 2.4 V. Both enable bias buffer.
- LOFF 0x13 with SENSP/SENSN 0x0F when diagnostics on, else 0x00.
- CHnSET: gain bits in [6:4] (24x=0x60), MUX normal 0x00 / test 0x05.
- BIAS_SENSP/SENSN 0x0F, GPIO 0x0C, MISC1/2 0x00, CONFIG4 0x00.
- ID reset value 0x1E (0x12 seen on some lots); anything else logs a
  warning event but does not block init unless 0x00/0xFF.
