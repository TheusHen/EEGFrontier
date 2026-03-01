from __future__ import annotations

import csv
import json
import threading
import time
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Sequence

import numpy as np

from .analysis import BANDS, SIGNAL_VIEW_ORDER, build_signal_views, compute_band_metrics
from .firmware_protocol import (
    PROTO_VER,
    Packet,
    ProtocolError,
    counts_to_microvolts,
    decode_frame,
)
from .models import ErrorPacket, EventPacket, SamplePacket, SampleRecord
from .simulator import EEGSimulator

try:
    import serial  # type: ignore
except ImportError:  # pragma: no cover - environment without pyserial
    serial = None


EVENT_CODE_NAMES = {
    0x01: "STREAM_STATE",
    0x10: "ADS_INIT_OK",
    0x30: "SELFTEST",
}

ERROR_CODE_NAMES = {
    0xE1: "ADS_INIT_FAIL",
    0xE2: "FRAME_READ_FAIL",
    0xE3: "DRDY_TIMEOUT_RECOVER",
}


@dataclass(slots=True)
class EngineConfig:
    sample_rate_hz: int = 250
    vref_uv: int = 4_500_000
    gain: int = 24
    baud: int = 921_600
    history_seconds: int = 20 * 60
    metrics_window_seconds: float = 8.0
    metrics_update_period_seconds: float = 0.5


class EEGEngine:
    def __init__(self, config: EngineConfig | None = None) -> None:
        self.config = config or EngineConfig()

        self._lock = threading.RLock()
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None
        self._serial_port = None

        max_history = self.config.history_seconds * self.config.sample_rate_hz
        self._history: deque[SampleRecord] = deque(maxlen=max_history)
        self._archive: list[SampleRecord] = []
        self._events: deque[dict[str, Any]] = deque(maxlen=2_000)
        self._parse_errors: deque[str] = deque(maxlen=300)

        self._latest_metrics: dict[str, Any] = self._empty_metrics()
        self._latest_sample: SampleRecord | None = None

        self._running = False
        self._connected = False
        self._simulate = False
        self._port_name = ""
        self._status_message = "Idle"
        self._session_started_monotonic = 0.0

        self._rx_bytes_total = 0
        self._packets_total = 0
        self._samples_total = 0
        self._events_total = 0
        self._errors_total = 0

    @staticmethod
    def _empty_metrics() -> dict[str, Any]:
        return {
            "delta": 0.0,
            "theta": 0.0,
            "alpha": 0.0,
            "beta": 0.0,
            "gamma": 0.0,
            "focus_score": 0.0,
            "relax_score": 0.0,
            "engagement_ratio": 0.0,
            "per_channel": {name: [0.0, 0.0, 0.0, 0.0] for name in BANDS},
        }

    @property
    def running(self) -> bool:
        with self._lock:
            return self._running

    @property
    def connected(self) -> bool:
        with self._lock:
            return self._connected

    @property
    def status_message(self) -> str:
        with self._lock:
            return self._status_message

    def reset_session(self) -> None:
        with self._lock:
            self._history.clear()
            self._archive.clear()
            self._events.clear()
            self._parse_errors.clear()
            self._latest_metrics = self._empty_metrics()
            self._latest_sample = None
            self._rx_bytes_total = 0
            self._packets_total = 0
            self._samples_total = 0
            self._events_total = 0
            self._errors_total = 0

    def start(
        self,
        *,
        port: str | None = None,
        baud: int | None = None,
        simulate: bool = False,
        auto_start_stream: bool = True,
        reset_data: bool = True,
    ) -> bool:
        with self._lock:
            if self._running:
                return True
            if reset_data:
                self.reset_session()

            self._stop_event.clear()
            self._simulate = simulate
            self._port_name = port or ""
            if baud:
                self.config.baud = int(baud)

            self._running = True
            self._connected = False
            self._status_message = "Initializing..."
            self._session_started_monotonic = time.monotonic()

            target = (
                self._run_simulator_loop
                if simulate
                else lambda: self._run_serial_loop(auto_start_stream=auto_start_stream)
            )
            self._thread = threading.Thread(target=target, daemon=True, name="PendulumEEGEngine")
            self._thread.start()
            return True

    def stop(self) -> None:
        with self._lock:
            if not self._running:
                return
            self._stop_event.set()

        thread = self._thread
        if thread and thread.is_alive():
            thread.join(timeout=2.0)

        with self._lock:
            if self._serial_port is not None:
                try:
                    self._serial_port.close()
                except Exception:
                    pass
                self._serial_port = None
            self._running = False
            self._connected = False
            self._status_message = "Stopped."

    def send_command(self, command: str) -> bool:
        cmd = command.strip()
        if not cmd:
            return False
        with self._lock:
            ser = self._serial_port
        if ser is None:
            self._push_event_line(f"CMD ignored (no serial): {cmd}")
            return False
        try:
            ser.write((cmd + "\n").encode("ascii", errors="ignore"))
            ser.flush()
            self._push_event_line(f"CMD -> {cmd}")
            return True
        except Exception as exc:
            self._push_parse_error(f"Failed to send command '{cmd}': {exc}")
            return False

    def get_snapshot(self, max_points: int = 1_500, event_limit: int = 60) -> dict[str, Any]:
        with self._lock:
            history_tail = list(self._history)[-max_points:]
            base_index = history_tail[0].sample_index if history_tail else 0

            if history_tail:
                sample_rate = float(self.config.sample_rate_hz)
                x_values = np.array(
                    [
                        (sample.sample_index - base_index) / sample_rate
                        for sample in history_tail
                    ],
                    dtype=np.float64,
                )
                matrix_uv = np.array(
                    [[s.ch1_uv, s.ch2_uv, s.ch3_uv, s.ch4_uv] for s in history_tail],
                    dtype=np.float64,
                )
                signal_views = build_signal_views(matrix_uv, sample_rate_hz=sample_rate)
            else:
                x_values = np.array([], dtype=np.float64)
                signal_views = {
                    key: np.zeros((0, 4), dtype=np.float64) for key in SIGNAL_VIEW_ORDER
                }

            signal_plot_points = {
                key: self._matrix_to_plot_rows(x_values, signal_views[key])
                for key in SIGNAL_VIEW_ORDER
            }
            plot_points = signal_plot_points["raw"]

            latest_sample = self._latest_sample.as_export_row() if self._latest_sample else {}
            recent_events = list(self._events)[-event_limit:]

            return {
                "running": self._running,
                "connected": self._connected,
                "simulate": self._simulate,
                "port_name": self._port_name,
                "status_message": self._status_message,
                "sample_rate_hz": self.config.sample_rate_hz,
                "proto_ver_expected": PROTO_VER,
                "samples_total": self._samples_total,
                "packets_total": self._packets_total,
                "events_total": self._events_total,
                "errors_total": self._errors_total,
                "rx_bytes_total": self._rx_bytes_total,
                "parse_error_count": len(self._parse_errors),
                "parse_errors": list(self._parse_errors)[-20:],
                "latest_sample": latest_sample,
                "latest_metrics": dict(self._latest_metrics),
                "plot_points": plot_points,
                "signal_plot_points": signal_plot_points,
                "events": recent_events,
            }

    @staticmethod
    def _matrix_to_plot_rows(x_values: np.ndarray, matrix_uv: np.ndarray) -> list[dict[str, float]]:
        if matrix_uv.size == 0 or len(x_values) == 0:
            return []
        return [
            {
                "x": float(x_values[i]),
                "ch1_uv": float(matrix_uv[i, 0]),
                "ch2_uv": float(matrix_uv[i, 1]),
                "ch3_uv": float(matrix_uv[i, 2]),
                "ch4_uv": float(matrix_uv[i, 3]),
            }
            for i in range(min(len(x_values), matrix_uv.shape[0]))
        ]

    def _push_event_line(self, message: str, level: str = "INFO") -> None:
        event = {
            "time_s": time.time(),
            "level": level,
            "message": message,
        }
        with self._lock:
            self._events.append(event)

    def _push_parse_error(self, message: str) -> None:
        with self._lock:
            self._parse_errors.append(message)
        self._push_event_line(message, level="WARN")

    def _finalize_thread(self, status_message: str) -> None:
        with self._lock:
            self._connected = False
            self._running = False
            self._status_message = status_message
            if self._serial_port is not None:
                try:
                    self._serial_port.close()
                except Exception:
                    pass
                self._serial_port = None

    def _run_serial_loop(self, auto_start_stream: bool) -> None:
        if serial is None:
            self._finalize_thread("pyserial is not installed.")
            return
        if not self._port_name:
            self._finalize_thread("Serial port was not provided.")
            return

        try:
            ser = serial.Serial(
                port=self._port_name,
                baudrate=self.config.baud,
                timeout=0.05,
                write_timeout=0.5,
            )
        except Exception as exc:
            self._finalize_thread(f"Failed to open serial port {self._port_name}: {exc}")
            return

        with self._lock:
            self._serial_port = ser
            self._connected = True
            self._status_message = f"Connected to {self._port_name} @ {self.config.baud}"

        self._push_event_line(self._status_message)
        self._configure_firmware(ser, auto_start_stream=auto_start_stream)

        rx_buffer = bytearray()
        next_metrics_at = time.monotonic() + self.config.metrics_update_period_seconds

        try:
            while not self._stop_event.is_set():
                chunk = ser.read(4096)
                if chunk:
                    with self._lock:
                        self._rx_bytes_total += len(chunk)
                    rx_buffer.extend(chunk)
                    self._consume_rx_bytes(rx_buffer)
                else:
                    time.sleep(0.002)

                now = time.monotonic()
                if now >= next_metrics_at:
                    self._update_metrics_from_history()
                    next_metrics_at = now + self.config.metrics_update_period_seconds
        except Exception as exc:
            self._push_parse_error(f"Serial loop interrupted: {exc}")
            self._finalize_thread(f"Serial loop interrupted: {exc}")
            return

        self._finalize_thread("Stopped.")

    def _run_simulator_loop(self) -> None:
        simulator = EEGSimulator(sample_rate_hz=self.config.sample_rate_hz)
        with self._lock:
            self._connected = True
            self._status_message = "Simulation running."
        self._push_event_line("Simulator started.")

        period = 1.0 / float(self.config.sample_rate_hz)
        next_sample = time.perf_counter()
        next_metrics_at = time.monotonic() + self.config.metrics_update_period_seconds

        while not self._stop_event.is_set():
            packet = simulator.next_packet()
            self._handle_packet(packet)

            now = time.monotonic()
            if now >= next_metrics_at:
                self._update_metrics_from_history()
                next_metrics_at = now + self.config.metrics_update_period_seconds

            next_sample += period
            sleep_s = next_sample - time.perf_counter()
            if sleep_s > 0:
                time.sleep(sleep_s)
            else:
                next_sample = time.perf_counter()

        self._finalize_thread("Simulation stopped.")

    def _configure_firmware(self, ser: Any, auto_start_stream: bool) -> None:
        try:
            ser.reset_input_buffer()
            ser.write(b"STOP\n")
            ser.write(b"MODE BIN\n")
            ser.flush()
            time.sleep(0.2)
            ser.reset_input_buffer()
            if auto_start_stream:
                ser.write(b"START\n")
                ser.flush()
                self._push_event_line("CMD auto: START")
        except Exception as exc:
            self._push_parse_error(f"Failed to configure firmware: {exc}")

    def _consume_rx_bytes(self, rx_buffer: bytearray) -> None:
        # ASCII lines may appear during boot before BIN mode is enabled.
        while True:
            delimiter_idx = rx_buffer.find(0)
            if delimiter_idx < 0:
                newline_idx = rx_buffer.find(b"\n")
                if newline_idx >= 0:
                    line = bytes(rx_buffer[:newline_idx]).decode(errors="ignore").strip()
                    del rx_buffer[: newline_idx + 1]
                    if line:
                        self._push_event_line(f"FW: {line}")
                    continue
                if len(rx_buffer) > 8192:
                    self._push_parse_error("RX buffer without 0x00 delimiter. Clearing buffer.")
                    rx_buffer.clear()
                return

            encoded = bytes(rx_buffer[:delimiter_idx])
            del rx_buffer[: delimiter_idx + 1]
            if not encoded:
                continue

            try:
                packet = decode_frame(encoded)
            except ProtocolError as exc:
                self._push_parse_error(f"Invalid frame: {exc}")
                continue
            except Exception as exc:
                self._push_parse_error(f"Unexpected error while decoding frame: {exc}")
                continue

            self._handle_packet(packet)

    def _handle_packet(self, packet: Packet) -> None:
        now_s = time.time()
        with self._lock:
            self._packets_total += 1

        if isinstance(packet, SamplePacket):
            sample = SampleRecord(
                sample_index=packet.sample_index,
                t_us=packet.t_us,
                status24=packet.status24,
                ch1=packet.ch1,
                ch2=packet.ch2,
                ch3=packet.ch3,
                ch4=packet.ch4,
                ch1_uv=counts_to_microvolts(packet.ch1, self.config.vref_uv, self.config.gain),
                ch2_uv=counts_to_microvolts(packet.ch2, self.config.vref_uv, self.config.gain),
                ch3_uv=counts_to_microvolts(packet.ch3, self.config.vref_uv, self.config.gain),
                ch4_uv=counts_to_microvolts(packet.ch4, self.config.vref_uv, self.config.gain),
                flags=packet.flags,
                missed_drdy_frame=packet.missed_drdy_frame,
                recoveries_total=packet.recoveries_total,
                host_timestamp_s=now_s,
            )
            with self._lock:
                self._history.append(sample)
                self._archive.append(sample)
                self._latest_sample = sample
                self._samples_total += 1
            return

        if isinstance(packet, EventPacket):
            label = EVENT_CODE_NAMES.get(packet.event_code, "EVENT")
            msg = f"{label} code=0x{packet.event_code:02X} a={packet.a} b={packet.b} c={packet.c}"
            with self._lock:
                self._events_total += 1
            self._push_event_line(msg)
            return

        if isinstance(packet, ErrorPacket):
            label = ERROR_CODE_NAMES.get(packet.error_code, "ERROR")
            msg = f"{label} code=0x{packet.error_code:02X} a={packet.a} b={packet.b}"
            with self._lock:
                self._errors_total += 1
            self._push_event_line(msg, level="ERROR")
            return

    def _update_metrics_from_history(self) -> None:
        with self._lock:
            window_size = int(self.config.metrics_window_seconds * self.config.sample_rate_hz)
            if window_size <= 0:
                return
            data = list(self._history)[-window_size:]
            if not data:
                self._latest_metrics = self._empty_metrics()
                return

        matrix = np.array(
            [[s.ch1_uv, s.ch2_uv, s.ch3_uv, s.ch4_uv] for s in data],
            dtype=np.float64,
        )
        metrics = compute_band_metrics(matrix, sample_rate_hz=float(self.config.sample_rate_hz))
        with self._lock:
            self._latest_metrics = metrics

    def _ensure_export_dir(self) -> Path:
        export_dir = Path(__file__).resolve().parents[1] / "exports"
        export_dir.mkdir(parents=True, exist_ok=True)
        return export_dir

    @staticmethod
    def _timestamp_slug() -> str:
        return time.strftime("%Y%m%d_%H%M%S")

    def _copy_archive(self) -> Sequence[SampleRecord]:
        with self._lock:
            return list(self._archive)

    def export_csv(self, path: str | Path | None = None) -> Path:
        samples = self._copy_archive()
        if not samples:
            raise ValueError("No samples available to export.")

        if path is None:
            path = self._ensure_export_dir() / f"eeg_samples_{self._timestamp_slug()}.csv"
        else:
            path = Path(path)
            path.parent.mkdir(parents=True, exist_ok=True)

        header = list(samples[0].as_export_row().keys())
        with path.open("w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=header)
            writer.writeheader()
            for sample in samples:
                writer.writerow(sample.as_export_row())
        return path

    def export_npz(self, path: str | Path | None = None) -> Path:
        samples = self._copy_archive()
        if not samples:
            raise ValueError("No samples available to export.")

        if path is None:
            path = self._ensure_export_dir() / f"eeg_samples_{self._timestamp_slug()}.npz"
        else:
            path = Path(path)
            path.parent.mkdir(parents=True, exist_ok=True)

        rows = [s.as_export_row() for s in samples]
        np.savez_compressed(
            path,
            sample_index=np.array([r["sample_index"] for r in rows], dtype=np.int64),
            t_us=np.array([r["t_us"] for r in rows], dtype=np.int64),
            status24=np.array([r["status24"] for r in rows], dtype=np.int64),
            ch1=np.array([r["ch1"] for r in rows], dtype=np.int64),
            ch2=np.array([r["ch2"] for r in rows], dtype=np.int64),
            ch3=np.array([r["ch3"] for r in rows], dtype=np.int64),
            ch4=np.array([r["ch4"] for r in rows], dtype=np.int64),
            ch1_uv=np.array([r["ch1_uv"] for r in rows], dtype=np.float64),
            ch2_uv=np.array([r["ch2_uv"] for r in rows], dtype=np.float64),
            ch3_uv=np.array([r["ch3_uv"] for r in rows], dtype=np.float64),
            ch4_uv=np.array([r["ch4_uv"] for r in rows], dtype=np.float64),
            flags=np.array([r["flags"] for r in rows], dtype=np.int64),
            missed_drdy_frame=np.array([r["missed_drdy_frame"] for r in rows], dtype=np.int64),
            recoveries_total=np.array([r["recoveries_total"] for r in rows], dtype=np.int64),
            host_timestamp_s=np.array([r["host_timestamp_s"] for r in rows], dtype=np.float64),
            sample_rate_hz=np.array([self.config.sample_rate_hz], dtype=np.int64),
        )
        return path

    def export_json_snapshot(self, path: str | Path | None = None) -> Path:
        snapshot = self.get_snapshot(max_points=3_000, event_limit=300)
        if path is None:
            path = self._ensure_export_dir() / f"eeg_snapshot_{self._timestamp_slug()}.json"
        else:
            path = Path(path)
            path.parent.mkdir(parents=True, exist_ok=True)

        with Path(path).open("w", encoding="utf-8") as f:
            json.dump(snapshot, f, ensure_ascii=False, indent=2)
        return Path(path)

    def export_fif(self, path: str | Path | None = None) -> Path:
        samples = self._copy_archive()
        if not samples:
            raise ValueError("No samples available to export.")
        try:
            from .mne_tools import samples_to_mne_raw
        except ImportError as exc:
            raise RuntimeError("mne is not installed. Install dependencies to export FIF.") from exc

        if path is None:
            path = self._ensure_export_dir() / f"eeg_samples_{self._timestamp_slug()}.fif"
        else:
            path = Path(path)
            path.parent.mkdir(parents=True, exist_ok=True)

        raw = samples_to_mne_raw(samples=samples, sample_rate_hz=float(self.config.sample_rate_hz))
        raw.save(str(path), overwrite=True, verbose="ERROR")
        return path
