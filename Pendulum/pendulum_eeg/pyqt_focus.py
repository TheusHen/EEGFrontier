from __future__ import annotations

import argparse
import json
import sys

import numpy as np
import pyqtgraph as pg
from pyqtgraph.Qt import QtCore, QtWidgets

from .reflex_bridge import get_engine


class FocusMonitorWindow(QtWidgets.QMainWindow):
    def __init__(self, points_window: int = 1500):
        super().__init__()
        self.setWindowTitle("Pendulum Focus Monitor (pyqtgraph)")
        self.resize(1450, 920)
        self._points_window = points_window
        self._engine = get_engine()

        root = QtWidgets.QWidget()
        self.setCentralWidget(root)
        layout = QtWidgets.QVBoxLayout(root)

        self.status_label = QtWidgets.QLabel("Status: waiting for data...")
        self.status_label.setStyleSheet("font-size: 14px;")
        layout.addWidget(self.status_label)

        scores_layout = QtWidgets.QHBoxLayout()
        self.focus_label = QtWidgets.QLabel("Focus: 0.0")
        self.relax_label = QtWidgets.QLabel("Relax: 0.0")
        self.ratio_label = QtWidgets.QLabel("Engagement Ratio: 0.0")
        for lbl in (self.focus_label, self.relax_label, self.ratio_label):
            lbl.setStyleSheet("font-size: 18px; font-weight: bold;")
            scores_layout.addWidget(lbl)
        layout.addLayout(scores_layout)

        self.raw_plot = pg.PlotWidget(title="EEG Raw (microvolts)")
        self.raw_plot.showGrid(x=True, y=True, alpha=0.2)
        self.raw_plot.addLegend()
        self.raw_plot.setLabel("left", "uV")
        self.raw_plot.setLabel("bottom", "Time (s) - Window")
        layout.addWidget(self.raw_plot, stretch=2)

        colors = ["#D62828", "#F77F00", "#003049", "#2A9D8F"]
        self.curves = {
            "ch1_uv": self.raw_plot.plot(pen=pg.mkPen(colors[0], width=1.6), name="CH1"),
            "ch2_uv": self.raw_plot.plot(pen=pg.mkPen(colors[1], width=1.6), name="CH2"),
            "ch3_uv": self.raw_plot.plot(pen=pg.mkPen(colors[2], width=1.6), name="CH3"),
            "ch4_uv": self.raw_plot.plot(pen=pg.mkPen(colors[3], width=1.6), name="CH4"),
        }

        self.band_plot = pg.PlotWidget(title="Band Power (current window)")
        self.band_plot.showGrid(x=True, y=True, alpha=0.2)
        self.band_plot.setLabel("left", "Power")
        self.band_plot.setYRange(0, 1, padding=0.1)
        self.band_plot.getAxis("bottom").setTicks(
            [[(0, "delta"), (1, "theta"), (2, "alpha"), (3, "beta"), (4, "gamma")]]
        )
        self._bar_item = pg.BarGraphItem(
            x=np.arange(5),
            height=np.zeros(5, dtype=np.float64),
            width=0.6,
            brush="#6A4C93",
        )
        self.band_plot.addItem(self._bar_item)
        layout.addWidget(self.band_plot, stretch=1)

        button_row = QtWidgets.QHBoxLayout()
        self.export_csv_btn = QtWidgets.QPushButton("Export CSV")
        self.export_npz_btn = QtWidgets.QPushButton("Export NPZ")
        self.export_fif_btn = QtWidgets.QPushButton("Export FIF")
        self.export_json_btn = QtWidgets.QPushButton("Export JSON Snapshot")
        self.copy_metrics_btn = QtWidgets.QPushButton("Copy Metrics (JSON)")

        self.export_csv_btn.clicked.connect(lambda: self._export("csv"))
        self.export_npz_btn.clicked.connect(lambda: self._export("npz"))
        self.export_fif_btn.clicked.connect(lambda: self._export("fif"))
        self.export_json_btn.clicked.connect(lambda: self._export("json"))
        self.copy_metrics_btn.clicked.connect(self._copy_metrics_json)

        for button in (
            self.export_csv_btn,
            self.export_npz_btn,
            self.export_fif_btn,
            self.export_json_btn,
            self.copy_metrics_btn,
        ):
            button_row.addWidget(button)
        layout.addLayout(button_row)

        self.message_label = QtWidgets.QLabel("")
        self.message_label.setStyleSheet("font-size: 13px;")
        layout.addWidget(self.message_label)

        self.timer = QtCore.QTimer(self)
        self.timer.timeout.connect(self._refresh)
        self.timer.start(100)

    def _set_message(self, text: str) -> None:
        self.message_label.setText(text)

    def _export(self, kind: str) -> None:
        try:
            if kind == "csv":
                path = self._engine.export_csv()
            elif kind == "npz":
                path = self._engine.export_npz()
            elif kind == "fif":
                path = self._engine.export_fif()
            elif kind == "json":
                path = self._engine.export_json_snapshot()
            else:
                self._set_message(f"Unknown export type: {kind}")
                return
            self._set_message(f"Exported: {path}")
        except Exception as exc:
            self._set_message(f"Export error ({kind}): {exc}")

    def _copy_metrics_json(self) -> None:
        snapshot = self._engine.get_snapshot(max_points=10)
        payload = {
            "metrics": snapshot.get("latest_metrics", {}),
            "latest_sample": snapshot.get("latest_sample", {}),
            "status": snapshot.get("status_message", ""),
            "samples_total": snapshot.get("samples_total", 0),
        }
        as_json = json.dumps(payload, ensure_ascii=False, indent=2)
        QtWidgets.QApplication.clipboard().setText(as_json)
        self._set_message("Metrics copied to clipboard as JSON.")

    def _refresh(self) -> None:
        snapshot = self._engine.get_snapshot(max_points=self._points_window, event_limit=40)

        status = snapshot.get("status_message", "")
        samples_total = snapshot.get("samples_total", 0)
        parse_errors = snapshot.get("parse_error_count", 0)
        self.status_label.setText(
            f"Status: {status} | samples={samples_total} | parse_errors={parse_errors}"
        )

        points = snapshot.get("plot_points", [])
        if points:
            x = np.array([float(p["x"]) for p in points], dtype=np.float64)
            for key, curve in self.curves.items():
                y = np.array([float(p[key]) for p in points], dtype=np.float64)
                curve.setData(x=x, y=y)

        metrics = snapshot.get("latest_metrics", {})
        delta = float(metrics.get("delta", 0.0))
        theta = float(metrics.get("theta", 0.0))
        alpha = float(metrics.get("alpha", 0.0))
        beta = float(metrics.get("beta", 0.0))
        gamma = float(metrics.get("gamma", 0.0))
        bars = np.array([delta, theta, alpha, beta, gamma], dtype=np.float64)

        dynamic_max = max(1.0, float(np.max(bars)) * 1.25)
        self.band_plot.setYRange(0, dynamic_max, padding=0.02)
        if hasattr(self._bar_item, "setOpts"):
            self._bar_item.setOpts(height=bars)

        focus_score = float(metrics.get("focus_score", 0.0))
        relax_score = float(metrics.get("relax_score", 0.0))
        engagement_ratio = float(metrics.get("engagement_ratio", 0.0))
        self.focus_label.setText(f"Focus: {focus_score:05.2f}")
        self.relax_label.setText(f"Relax: {relax_score:05.2f}")
        self.ratio_label.setText(f"Engagement Ratio: {engagement_ratio:.4f}")

    def closeEvent(self, event):  # noqa: N802
        self.timer.stop()
        super().closeEvent(event)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Pendulum Focus Monitor (pyqtgraph).")
    parser.add_argument("--port", default="", help="Serial port (e.g.: COM5).")
    parser.add_argument("--baud", type=int, default=921600, help="Serial baud rate.")
    parser.add_argument("--simulate", action="store_true", help="Use simulator instead of serial.")
    parser.add_argument("--window-points", type=int, default=1500, help="Number of points in chart window.")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    engine = get_engine()
    engine.start(port=args.port, baud=args.baud, simulate=args.simulate, auto_start_stream=True)

    app = QtWidgets.QApplication(sys.argv)
    pg.setConfigOptions(antialias=True)
    window = FocusMonitorWindow(points_window=args.window_points)
    window.show()
    exit_code = app.exec()

    engine.stop()
    return int(exit_code)


if __name__ == "__main__":
    raise SystemExit(main())
