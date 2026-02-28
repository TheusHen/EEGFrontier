from __future__ import annotations

from typing import Sequence

import mne
import numpy as np

from .models import SampleRecord


def samples_to_mne_raw(
    samples: Sequence[SampleRecord],
    sample_rate_hz: float,
    channel_names: Sequence[str] = ("EEG1", "EEG2", "EEG3", "EEG4"),
) -> mne.io.BaseRaw:
    if len(samples) == 0:
        raise ValueError("No samples available to convert to MNE Raw.")

    data_uv = np.array(
        [[s.ch1_uv, s.ch2_uv, s.ch3_uv, s.ch4_uv] for s in samples],
        dtype=np.float64,
    )
    data_v = (data_uv.T) * 1e-6

    info = mne.create_info(
        ch_names=list(channel_names),
        sfreq=float(sample_rate_hz),
        ch_types=["eeg"] * len(channel_names),
    )
    raw = mne.io.RawArray(data_v, info, verbose="ERROR")
    return raw
