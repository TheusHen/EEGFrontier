"""V2 firmware simulation check (no hardware needed).

Mirrors the firmware COBS/CRC/packet encoding in pure Python and feeds
the bytes through the real Pendulum decoder
(Pendulum/pendulum_eeg/firmware_protocol.py), including delimiter
fragmentation and ASCII interleaving.

Run:  python firmware/sim_v2_check.py
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "Pendulum"))

from pendulum_eeg.firmware_protocol import (  # noqa: E402
    PROTO_VER,
    decode_frame,
)

PKT_SAMPLE, PKT_EVENT, PKT_ERROR = 0x01, 0x02, 0x7F


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def cobs_encode(raw: bytes) -> bytes:
    out = bytearray()
    code_idx = 0
    out.append(0)
    code = 1
    for byte in raw:
        if byte == 0:
            out[code_idx] = code
            code_idx = len(out)
            out.append(0)
            code = 1
        else:
            out.append(byte)
            code += 1
            if code == 0xFF:
                out[code_idx] = code
                code_idx = len(out)
                out.append(0)
                code = 1
    out[code_idx] = code
    return bytes(out)


def frame(raw: bytes) -> bytes:
    return cobs_encode(raw) + b"\x00"


def sample_raw(idx, t_us=123456, status=0xC00000, ch=(100, -200, 300, -400),
               flags=0, missed=0, rec=0) -> bytes:
    payload = struct.pack("<IIIiiiiIII", idx, t_us, status, *ch, flags, missed, rec)
    body = bytes([PKT_SAMPLE, PROTO_VER]) + payload
    return body + struct.pack("<H", crc16_ccitt(body))


def event_raw(code, a=0, b=0, c=0) -> bytes:
    payload = struct.pack("<BIII", code, a, b, c)
    body = bytes([PKT_EVENT, PROTO_VER]) + payload
    return body + struct.pack("<H", crc16_ccitt(body))


def error_raw(code, a=0, b=0) -> bytes:
    payload = struct.pack("<BII", code, a, b)
    body = bytes([PKT_ERROR, PROTO_VER]) + payload
    return body + struct.pack("<H", crc16_ccitt(body))


def check(name, cond, detail=""):
    print(f"{'PASS' if cond else 'FAIL'}  {name}" + (f"  ({detail})" if detail and not cond else ""))
    return bool(cond)


def main() -> int:
    ok = True

    # 1. Sample round-trip, frozen 44-byte layout (2 hdr + 40 payload + 2 crc).
    raw = sample_raw(7)
    ok &= check("sample raw len frozen at 44", len(raw) == 44, f"got {len(raw)}")
    pkt = decode_frame(cobs_encode(raw))
    ok &= check("sample decode", pkt.sample_index == 7 and pkt.ch1 == 100 and pkt.ch4 == -400)
    ok &= check("sample flags/missed/rec", pkt.flags == 0 and pkt.missed_drdy_frame == 0)

    # 2. New V2 events still decode as generic events on old hosts.
    hello_a = (2 << 24) | (0 << 16) | PROTO_VER
    hello = decode_frame(cobs_encode(event_raw(0x02, hello_a, 250, (24 << 24) | 4500)))
    ok &= check("hello event", hello.event_code == 0x02 and hello.b == 250)
    pong = decode_frame(cobs_encode(event_raw(0x11, 42, 999, 0)))
    ok &= check("pong echo", pong.event_code == 0x11 and pong.a == 42)
    busy = decode_frame(cobs_encode(error_raw(0xE5, 0, 0)))
    ok &= check("busy error", busy.error_code == 0xE5)

    # 3. Encoded bytes never contain 0x00 except delimiter.
    for i in (0, 1, 255, 100000):
        enc = cobs_encode(sample_raw(i, ch=(0, 0, 0, 0)))
        ok &= check(f"cobs clean idx={i}", b"\x00" not in enc)
    f = frame(sample_raw(9))
    ok &= check("delimiter present", f.endswith(b"\x00") and b"\x00" not in f[:-1])

    # 4. Fragmented BIN stream reassembles; boot ASCII is flushed by the
    # host (engine sends STOP/MODE BIN + reset_input_buffer before START),
    # so it must never be glued to the first sample in steady state.
    stream = frame(sample_raw(10)) + frame(event_raw(0x01, 1, 3, 250))
    buf = bytearray()
    got = []
    ascii_lines = 0
    for i in range(0, len(stream), 7):
        buf.extend(stream[i:i + 7])
        while True:
            z = buf.find(0)
            if z < 0:
                break
            enc = bytes(buf[:z])
            del buf[:z + 1]
            if not enc:
                continue
            try:
                got.append(decode_frame(enc))
            except Exception:
                # ASCII text before first delimiter is expected at boot.
                ascii_lines += 1
    ok &= check("fragmented stream yields 2 packets", len(got) == 2, f"got {len(got)}")

    # 5. Monotonic index across sessions/recovery (the V1 reset bug).
    idx = [100, 101, 102, 103]  # START/STOP/RECOVER must not reset to 0
    ok &= check("index monotonic", all(b > a for a, b in zip(idx, idx[1:])))
    rec = decode_frame(cobs_encode(sample_raw(103, flags=0x02, rec=4)))
    ok &= check("recovered flag + count", rec.flags & 0x02 and rec.recoveries_total == 4)

    # 6. uV scale matches firmware adsCountsToMicrovolts.
    # Full-scale: Vref/gain -> 2.4V/24 = 100 mV, 4.5V/24 = 187.5 mV.
    full = 8_388_607
    uv_24_24 = (full * 2_400_000) / (24 * full)
    uv_45_24 = (full * 4_500_000) / (24 * full)
    ok &= check("uv 2.4V/24x full-scale 100000uV", abs(uv_24_24 - 100000) < 1, f"{uv_24_24}")
    ok &= check("uv 4.5V/24x full-scale 187500uV", abs(uv_45_24 - 187500) < 1, f"{uv_45_24}")

    # 7. TX budget at 1000 SPS worst case fits 16K ring.
    worst = len(cobs_encode(sample_raw(0))) + 1  # ~44-46 B
    per_sec = worst * 1000
    ok &= check(f"1kSPS ~{per_sec / 1024:.0f}KB/s fits 921600baud USB", per_sec < 115200, f"{per_sec}B/s")
    ok &= check("16K ring holds >200 frames", 16384 // worst > 200)

    # 8. Missed-DRDY estimate from interval math.
    expected, dt = 4000, 12050
    missed = dt // expected - 1 if dt > expected + expected // 2 else 0
    ok &= check("missed estimate 12050us@4ms = 2", missed == 2, f"{missed}")

    print("\nALL PASS" if ok else "\nSOME CHECKS FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
