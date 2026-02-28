from __future__ import annotations

import math
import random
import time
from dataclasses import dataclass, field

from .firmware_protocol import ADS_STATUS_HEADER_OK, counts_to_microvolts
from .models import SamplePacket


def microvolts_to_counts(microvolts: float, vref_uv: int = 4_500_000, gain: int = 24) -> int:
    scale = (gain * 8_388_607.0) / float(vref_uv)
    return int(microvolts * scale)


@dataclass(slots=True)
class EEGSimulator:
    sample_rate_hz: int = 250
    amplitude_uv: float = 35.0
    start_monotonic: float = field(default_factory=time.monotonic)
    sample_index: int = 0

    def next_packet(self) -> SamplePacket:
        t = self.sample_index / float(self.sample_rate_hz)
        drift = 1.0 + 0.2 * math.sin(2.0 * math.pi * 0.03 * t)

        # Mistura simples de bandas para parecer EEG.
        alpha = math.sin(2.0 * math.pi * 10.0 * t)
        beta = math.sin(2.0 * math.pi * 19.0 * t + 0.5)
        theta = math.sin(2.0 * math.pi * 6.0 * t + 1.2)
        delta = math.sin(2.0 * math.pi * 2.0 * t + 2.4)
        gamma = math.sin(2.0 * math.pi * 35.0 * t + 0.7)
        noise = random.gauss(0.0, 0.15)

        base_uv = self.amplitude_uv * drift * (
            0.35 * alpha + 0.45 * beta + 0.20 * theta + 0.12 * delta + 0.06 * gamma + noise
        )

        ch_values_uv = [
            base_uv + random.gauss(0.0, 2.0),
            base_uv * 0.95 + random.gauss(0.0, 2.0),
            base_uv * 1.05 + random.gauss(0.0, 2.2),
            base_uv * 1.02 + random.gauss(0.0, 1.8),
        ]
        ch_counts = [microvolts_to_counts(v) for v in ch_values_uv]

        timestamp_us = int((time.monotonic() - self.start_monotonic) * 1_000_000.0)

        packet = SamplePacket(
            version=1,
            sample_index=self.sample_index,
            t_us=timestamp_us,
            status24=ADS_STATUS_HEADER_OK,
            ch1=ch_counts[0],
            ch2=ch_counts[1],
            ch3=ch_counts[2],
            ch4=ch_counts[3],
            flags=1,  # FLAG_STREAMING
            missed_drdy_frame=0,
            recoveries_total=0,
        )
        self.sample_index += 1
        return packet

    def to_microvolts(self, counts: int) -> float:
        return counts_to_microvolts(counts)
