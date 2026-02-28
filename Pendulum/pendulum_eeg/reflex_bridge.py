from __future__ import annotations

from .engine import EEGEngine

_ENGINE = EEGEngine()


def get_engine() -> EEGEngine:
    return _ENGINE
