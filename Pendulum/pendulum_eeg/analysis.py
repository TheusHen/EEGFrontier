from __future__ import annotations

import math
from typing import Any

import numpy as np

try:
    from scipy import signal as scipy_signal
except ImportError:  # pragma: no cover - fallback for environments without scipy
    scipy_signal = None


BANDS = {
    "delta": (1.0, 4.0),
    "theta": (4.0, 8.0),
    "alpha": (8.0, 12.0),
    "beta": (12.0, 30.0),
    "gamma": (30.0, 45.0),
}


def _integrate_band(freqs: np.ndarray, psd: np.ndarray, low: float, high: float) -> np.ndarray:
    mask = (freqs >= low) & (freqs < high)
    if mask.sum() < 2:
        return np.zeros(psd.shape[1], dtype=np.float64)
    return np.trapezoid(psd[mask], x=freqs[mask], axis=0)


def compute_band_metrics(window_uv: np.ndarray, sample_rate_hz: float) -> dict[str, Any]:
    """
    window_uv:
      shape = (n_samples, n_channels)
      unit in microvolts.
    """
    metrics: dict[str, Any] = {
        "delta": 0.0,
        "theta": 0.0,
        "alpha": 0.0,
        "beta": 0.0,
        "gamma": 0.0,
        "focus_score": 0.0,
        "relax_score": 0.0,
        "engagement_ratio": 0.0,
        "per_channel": {},
    }

    if window_uv.ndim != 2 or window_uv.shape[0] < 16:
        return metrics

    n_samples = window_uv.shape[0]
    if scipy_signal is not None:
        nperseg = min(512, n_samples)
        freqs, psd = scipy_signal.welch(
            window_uv,
            fs=sample_rate_hz,
            nperseg=nperseg,
            axis=0,
            detrend="constant",
        )
    else:
        centered = window_uv - np.mean(window_uv, axis=0, keepdims=True)
        fft = np.fft.rfft(centered, axis=0)
        freqs = np.fft.rfftfreq(n_samples, d=1.0 / float(sample_rate_hz))
        psd = (np.abs(fft) ** 2) / (float(sample_rate_hz) * float(n_samples))

    per_channel: dict[str, list[float]] = {}
    averages: dict[str, float] = {}

    for name, (low, high) in BANDS.items():
        channel_power = _integrate_band(freqs, psd, low, high)
        per_channel[name] = [float(v) for v in channel_power]
        averages[name] = float(np.mean(channel_power))
        metrics[name] = averages[name]

    # Real-time focus heuristic:
    # more beta with lower alpha/theta/delta tends to indicate higher engagement.
    denom = averages["alpha"] + averages["theta"] + averages["delta"] + 1e-9
    engagement_ratio = averages["beta"] / denom
    focus_score = 100.0 * (1.0 - math.exp(-1.8 * engagement_ratio))
    focus_score = float(np.clip(focus_score, 0.0, 100.0))

    relax_ratio = averages["alpha"] / (averages["beta"] + averages["theta"] + 1e-9)
    relax_score = 100.0 * (1.0 - math.exp(-2.0 * relax_ratio))
    relax_score = float(np.clip(relax_score, 0.0, 100.0))

    metrics["engagement_ratio"] = float(engagement_ratio)
    metrics["focus_score"] = focus_score
    metrics["relax_score"] = relax_score
    metrics["per_channel"] = per_channel
    return metrics
