"""Host-side checks for the V2 firmware protocol.

These mirror what the firmware sends (see firmware/PROTOCOL_V2.md and
firmware/include/fw_config.h) and run the bytes through the real decoder,
so a framing change on either side breaks loudly here first.
"""
from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Pendulum"))

from pendulum_eeg.firmware_protocol import (  # noqa: E402
    FLAG_ADS_LOFF_ANY,
    FLAG_BTN_TOGGLED,
    FLAG_DRDY_MISSED,
    FLAG_RECOVERED,
    FLAG_STATUS_INVALID,
    FLAG_STREAMING,
    FLAG_TX_OVERFLOW,
    PKT_ERROR,
    PKT_EVENT,
    PKT_SAMPLE,
    PROTO_VER,
    ProtocolError,
    counts_to_microvolts,
    crc16_ccitt,
    decode_frame,
)

FULL_SCALE = 8_388_607


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


def make_sample(idx=1, t_us=1000, status=0xC00000, ch=(10, -20, 30, -40),
                flags=0, missed=0, rec=0) -> bytes:
    payload = struct.pack("<IIIiiiiIII", idx, t_us, status, *ch, flags, missed, rec)
    body = bytes([PKT_SAMPLE, PROTO_VER]) + payload
    return body + struct.pack("<H", crc16_ccitt(body))


def make_event(code, a=0, b=0, c=0) -> bytes:
    body = bytes([PKT_EVENT, PROTO_VER]) + struct.pack("<BIII", code, a, b, c)
    return body + struct.pack("<H", crc16_ccitt(body))


def make_error(code, a=0, b=0) -> bytes:
    body = bytes([PKT_ERROR, PROTO_VER]) + struct.pack("<BII", code, a, b)
    return body + struct.pack("<H", crc16_ccitt(body))


def test_crc_known_vector():
    assert crc16_ccitt(b"123456789") == 0x29B1


def test_sample_layout_frozen_at_44_bytes():
    assert len(make_sample()) == 44


def test_sample_roundtrip():
    pkt = decode_frame(cobs_encode(make_sample(idx=7, ch=(100, -200, 300, -400))))
    assert pkt.sample_index == 7
    assert (pkt.ch1, pkt.ch2, pkt.ch3, pkt.ch4) == (100, -200, 300, -400)
    assert pkt.version == PROTO_VER


def test_sample_negative_extremes():
    pkt = decode_frame(cobs_encode(make_sample(ch=(-FULL_SCALE, FULL_SCALE, -1, 0))))
    assert pkt.ch1 == -FULL_SCALE
    assert pkt.ch2 == FULL_SCALE


def test_event_and_error_roundtrip():
    evt = decode_frame(cobs_encode(make_event(0x02, 1, 250, 4500)))
    assert evt.event_code == 0x02
    assert (evt.a, evt.b, evt.c) == (1, 250, 4500)
    err = decode_frame(cobs_encode(make_error(0xE5, 0, 0)))
    assert err.error_code == 0xE5


def test_bad_crc_rejected():
    raw = bytearray(make_sample())
    raw[-1] ^= 0xFF
    with pytest.raises(ProtocolError):
        decode_frame(cobs_encode(bytes(raw)))


def test_truncated_frame_rejected():
    with pytest.raises(ProtocolError):
        decode_frame(cobs_encode(make_sample())[:10])


def test_unknown_type_rejected():
    body = bytes([0x55, PROTO_VER, 0x00])
    raw = body + struct.pack("<H", crc16_ccitt(body))
    with pytest.raises(ProtocolError):
        decode_frame(cobs_encode(raw))


def test_encoded_sample_has_no_zeros():
    enc = cobs_encode(make_sample(idx=3, ch=(0, 0, 0, 0)))
    assert b"\x00" not in enc


def test_uv_scale_matches_firmware():
    assert counts_to_microvolts(FULL_SCALE, 2_400_000, 24) == pytest.approx(100000, abs=1)
    assert counts_to_microvolts(FULL_SCALE, 4_500_000, 24) == pytest.approx(187500, abs=1)
    assert counts_to_microvolts(0, 2_400_000, 24) == 0
    assert counts_to_microvolts(123, 2_400_000, 0) == 0


def test_flag_bits_match_firmware_header():
    text = (ROOT / "firmware" / "include" / "fw_config.h").read_text()
    expected = {}
    for m in re.finditer(r"FLAG_(\w+)\s*=\s*\(1u << (\d+)\)", text):
        expected[m.group(1)] = 1 << int(m.group(2))
    host = {
        "STREAMING": FLAG_STREAMING,
        "RECOVERED": FLAG_RECOVERED,
        "BTN_TOGGLED": FLAG_BTN_TOGGLED,
        "DRDY_MISSED": FLAG_DRDY_MISSED,
        "STATUS_INVALID": FLAG_STATUS_INVALID,
        "ADS_LOFF_ANY": FLAG_ADS_LOFF_ANY,
        "TX_OVERFLOW": FLAG_TX_OVERFLOW,
    }
    for name, value in host.items():
        assert expected[name] == value, f"FLAG_{name} diverged from fw_config.h"
