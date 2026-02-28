from __future__ import annotations

import struct
from typing import Union

from .models import ErrorPacket, EventPacket, SamplePacket


PKT_SAMPLE = 0x01
PKT_EVENT = 0x02
PKT_ERROR = 0x7F
PROTO_VER = 0x01

FLAG_STREAMING = 1 << 0
FLAG_RECOVERED = 1 << 1
FLAG_BTN_TOGGLED = 1 << 2
FLAG_DRDY_MISSED = 1 << 3
FLAG_STATUS_INVALID = 1 << 4
FLAG_ADS_LOFF_ANY = 1 << 5
FLAG_TX_OVERFLOW = 1 << 6

ADS_STATUS_HEADER_MASK = 0xF00000
ADS_STATUS_HEADER_OK = 0xC00000

VREF_UV_DEFAULT = 4_500_000
GAIN_DEFAULT = 24
FULL_SCALE_CODE = 8_388_607

Packet = Union[SamplePacket, EventPacket, ErrorPacket]

_SAMPLE_STRUCT = struct.Struct("<IIIiiiiIII")
_EVENT_STRUCT = struct.Struct("<BIII")
_ERROR_STRUCT = struct.Struct("<BII")


class ProtocolError(RuntimeError):
    pass


def counts_to_microvolts(counts: int, vref_uv: int = VREF_UV_DEFAULT, gain: int = GAIN_DEFAULT) -> float:
    if gain == 0:
        return 0.0
    return (counts * float(vref_uv)) / (float(gain) * float(FULL_SCALE_CODE))


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


def cobs_decode(encoded: bytes) -> bytes:
    if not encoded:
        return b""

    out = bytearray()
    idx = 0
    size = len(encoded)

    while idx < size:
        code = encoded[idx]
        idx += 1
        if code == 0:
            raise ProtocolError("COBS code 0 found in encoded frame.")

        block_len = code - 1
        if idx + block_len > size:
            raise ProtocolError("Truncated COBS frame.")

        out.extend(encoded[idx : idx + block_len])
        idx += block_len

        if code != 0xFF and idx < size:
            out.append(0)

    return bytes(out)


def parse_raw_packet(raw: bytes) -> Packet:
    if len(raw) < 4:
        raise ProtocolError("Raw packet is smaller than minimum size.")

    packet_type = raw[0]
    version = raw[1]
    payload = raw[2:-2]
    recv_crc = int.from_bytes(raw[-2:], "little")
    calc_crc = crc16_ccitt(raw[:-2])

    if recv_crc != calc_crc:
        raise ProtocolError(
            f"Invalid CRC. expected=0x{calc_crc:04X} received=0x{recv_crc:04X}"
        )

    if packet_type == PKT_SAMPLE:
        if len(payload) != _SAMPLE_STRUCT.size:
            raise ProtocolError(
                f"Invalid SAMPLE payload: {len(payload)} bytes (expected {_SAMPLE_STRUCT.size})."
            )
        unpacked = _SAMPLE_STRUCT.unpack(payload)
        return SamplePacket(
            version=version,
            sample_index=unpacked[0],
            t_us=unpacked[1],
            status24=unpacked[2],
            ch1=unpacked[3],
            ch2=unpacked[4],
            ch3=unpacked[5],
            ch4=unpacked[6],
            flags=unpacked[7],
            missed_drdy_frame=unpacked[8],
            recoveries_total=unpacked[9],
        )

    if packet_type == PKT_EVENT:
        if len(payload) != _EVENT_STRUCT.size:
            raise ProtocolError(
                f"Invalid EVENT payload: {len(payload)} bytes (expected {_EVENT_STRUCT.size})."
            )
        event_code, a, b, c = _EVENT_STRUCT.unpack(payload)
        return EventPacket(version=version, event_code=event_code, a=a, b=b, c=c)

    if packet_type == PKT_ERROR:
        if len(payload) != _ERROR_STRUCT.size:
            raise ProtocolError(
                f"Invalid ERROR payload: {len(payload)} bytes (expected {_ERROR_STRUCT.size})."
            )
        error_code, a, b = _ERROR_STRUCT.unpack(payload)
        return ErrorPacket(version=version, error_code=error_code, a=a, b=b)

    raise ProtocolError(f"Unknown packet type: 0x{packet_type:02X}")


def decode_frame(encoded_without_delimiter: bytes) -> Packet:
    raw = cobs_decode(encoded_without_delimiter)
    return parse_raw_packet(raw)
