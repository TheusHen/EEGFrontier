from __future__ import annotations

from dataclasses import dataclass
from typing import Literal


PacketType = Literal["sample", "event", "error"]


@dataclass(slots=True)
class SamplePacket:
    version: int
    sample_index: int
    t_us: int
    status24: int
    ch1: int
    ch2: int
    ch3: int
    ch4: int
    flags: int
    missed_drdy_frame: int
    recoveries_total: int


@dataclass(slots=True)
class EventPacket:
    version: int
    event_code: int
    a: int
    b: int
    c: int


@dataclass(slots=True)
class ErrorPacket:
    version: int
    error_code: int
    a: int
    b: int


@dataclass(slots=True)
class SampleRecord:
    sample_index: int
    t_us: int
    status24: int
    ch1: int
    ch2: int
    ch3: int
    ch4: int
    ch1_uv: float
    ch2_uv: float
    ch3_uv: float
    ch4_uv: float
    flags: int
    missed_drdy_frame: int
    recoveries_total: int
    host_timestamp_s: float

    def as_plot_row(self, x_value: float) -> dict[str, float]:
        return {
            "x": x_value,
            "ch1_uv": self.ch1_uv,
            "ch2_uv": self.ch2_uv,
            "ch3_uv": self.ch3_uv,
            "ch4_uv": self.ch4_uv,
        }

    def as_export_row(self) -> dict[str, float | int]:
        return {
            "sample_index": self.sample_index,
            "t_us": self.t_us,
            "status24": self.status24,
            "ch1": self.ch1,
            "ch2": self.ch2,
            "ch3": self.ch3,
            "ch4": self.ch4,
            "ch1_uv": self.ch1_uv,
            "ch2_uv": self.ch2_uv,
            "ch3_uv": self.ch3_uv,
            "ch4_uv": self.ch4_uv,
            "flags": self.flags,
            "missed_drdy_frame": self.missed_drdy_frame,
            "recoveries_total": self.recoveries_total,
            "host_timestamp_s": self.host_timestamp_s,
        }
