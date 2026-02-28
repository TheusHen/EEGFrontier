from __future__ import annotations

import asyncio
import json

import reflex as rx

from pendulum_eeg.reflex_bridge import get_engine


APP_BG = "linear-gradient(155deg, #eef2ff 0%, #f8fafc 45%, #eef6ff 100%)"
CARD_BG = "rgba(255, 255, 255, 0.88)"
CARD_BORDER = "1px solid #dbe6ff"
CARD_SHADOW = "0 8px 26px rgba(16, 24, 40, 0.08)"


def _clamp_int(value: str | int, min_v: int, max_v: int, fallback: int) -> int:
    try:
        parsed = int(value)
    except (TypeError, ValueError):
        return fallback
    return max(min_v, min(max_v, parsed))


class DashboardState(rx.State):
    # Config
    port: str = "COM5"
    baud: str = "921600"
    simulate: bool = True
    points_window: str = "1500"
    refresh_ms: str = "250"

    # Runtime UI
    connected: bool = False
    status_message: str = "Ready."
    command_text: str = "INFO"
    auto_refresh: bool = True
    poll_running: bool = False

    # View controls
    line_chart_height: int = 430
    band_chart_height: int = 290
    side_panel_height: int = 260

    # Stats
    samples_total: int = 0
    packets_total: int = 0
    events_total: int = 0
    errors_total: int = 0
    parse_error_count: int = 0
    rx_bytes_total: int = 0

    # Metrics
    focus_score: float = 0.0
    relax_score: float = 0.0
    engagement_ratio: float = 0.0
    delta_power: float = 0.0
    theta_power: float = 0.0
    alpha_power: float = 0.0
    beta_power: float = 0.0
    gamma_power: float = 0.0

    # Graph/table data
    plot_points: list[dict[str, float]] = []
    band_points: list[dict[str, float | str]] = [
        {"band": "delta", "power": 0.0},
        {"band": "theta", "power": 0.0},
        {"band": "alpha", "power": 0.0},
        {"band": "beta", "power": 0.0},
        {"band": "gamma", "power": 0.0},
    ]
    latest_sample_json: str = "{}"
    event_lines: list[str] = []
    parse_error_lines: list[str] = []

    def set_port(self, value: str) -> None:
        self.port = value

    def set_baud(self, value: str) -> None:
        self.baud = value

    def toggle_simulate(self) -> None:
        self.simulate = not self.simulate

    def set_points_window(self, value: str) -> None:
        self.points_window = value

    def set_refresh_ms(self, value: str) -> None:
        self.refresh_ms = value

    def set_command_text(self, value: str) -> None:
        self.command_text = value

    def set_line_chart_height(self, value: str) -> None:
        self.line_chart_height = _clamp_int(value, 240, 980, self.line_chart_height)

    def set_band_chart_height(self, value: str) -> None:
        self.band_chart_height = _clamp_int(value, 200, 800, self.band_chart_height)

    def set_side_panel_height(self, value: str) -> None:
        self.side_panel_height = _clamp_int(value, 160, 700, self.side_panel_height)

    def dec_line_chart_height(self) -> None:
        self.line_chart_height = max(240, self.line_chart_height - 40)

    def inc_line_chart_height(self) -> None:
        self.line_chart_height = min(980, self.line_chart_height + 40)

    def dec_band_chart_height(self) -> None:
        self.band_chart_height = max(200, self.band_chart_height - 30)

    def inc_band_chart_height(self) -> None:
        self.band_chart_height = min(800, self.band_chart_height + 30)

    def dec_side_panel_height(self) -> None:
        self.side_panel_height = max(160, self.side_panel_height - 20)

    def inc_side_panel_height(self) -> None:
        self.side_panel_height = min(700, self.side_panel_height + 20)

    def dec_points_window(self) -> None:
        points = _clamp_int(self.points_window, 200, 20_000, 1500)
        self.points_window = str(max(200, points - 200))

    def inc_points_window(self) -> None:
        points = _clamp_int(self.points_window, 200, 20_000, 1500)
        self.points_window = str(min(20_000, points + 200))

    def dec_refresh_ms(self) -> None:
        refresh = _clamp_int(self.refresh_ms, 50, 4000, 250)
        self.refresh_ms = str(max(50, refresh - 50))

    def inc_refresh_ms(self) -> None:
        refresh = _clamp_int(self.refresh_ms, 50, 4000, 250)
        self.refresh_ms = str(min(4000, refresh + 50))

    def toggle_auto_refresh(self):
        self.auto_refresh = not self.auto_refresh
        if not self.auto_refresh:
            self.poll_running = False
        elif self.connected and not self.poll_running:
            self.poll_running = True
            return DashboardState.poll_loop
        return None

    def connect(self):
        engine = get_engine()
        baud = int(self.baud or "921600")
        engine.start(
            port=(self.port.strip() or None),
            baud=baud,
            simulate=self.simulate,
            auto_start_stream=True,
            reset_data=True,
        )
        snapshot = engine.get_snapshot(max_points=10, event_limit=20)
        self.connected = bool(snapshot["connected"])
        self.status_message = str(snapshot["status_message"])
        if bool(snapshot["running"]) and self.auto_refresh and not self.poll_running:
            self.poll_running = True
            return DashboardState.poll_loop
        return None

    def disconnect(self) -> None:
        self.poll_running = False
        engine = get_engine()
        engine.stop()
        snapshot = engine.get_snapshot(max_points=5, event_limit=10)
        self.connected = bool(snapshot["connected"])
        self.status_message = str(snapshot["status_message"])

    def refresh_once(self) -> None:
        self._consume_snapshot(get_engine().get_snapshot(max_points=self._points_window_int(), event_limit=80))

    def send_command(self) -> None:
        cmd = self.command_text.strip()
        if not cmd:
            return
        ok = get_engine().send_command(cmd)
        if ok:
            self.status_message = f"Command sent: {cmd}"
        else:
            self.status_message = f"Failed to send command: {cmd}"

    def export_csv(self) -> None:
        try:
            path = get_engine().export_csv()
            self.status_message = f"CSV saved to: {path}"
        except Exception as exc:
            self.status_message = f"CSV error: {exc}"

    def export_npz(self) -> None:
        try:
            path = get_engine().export_npz()
            self.status_message = f"NPZ saved to: {path}"
        except Exception as exc:
            self.status_message = f"NPZ error: {exc}"

    def export_fif(self) -> None:
        try:
            path = get_engine().export_fif()
            self.status_message = f"FIF saved to: {path}"
        except Exception as exc:
            self.status_message = f"FIF error: {exc}"

    def export_json(self) -> None:
        try:
            path = get_engine().export_json_snapshot()
            self.status_message = f"JSON saved to: {path}"
        except Exception as exc:
            self.status_message = f"JSON error: {exc}"

    @rx.event(background=True)
    async def poll_loop(self):
        while True:
            async with self:
                should_run = self.poll_running and self.auto_refresh
                interval_s = max(0.05, float(self.refresh_ms or "250") / 1000.0)
                points_window = self._points_window_int()
            if not should_run:
                break

            snapshot = get_engine().get_snapshot(max_points=points_window, event_limit=100)

            async with self:
                self._consume_snapshot(snapshot)
                if not bool(snapshot["running"]):
                    self.poll_running = False

            await asyncio.sleep(interval_s)

    def _points_window_int(self) -> int:
        try:
            return max(200, min(20_000, int(self.points_window or "1500")))
        except ValueError:
            return 1500

    def _consume_snapshot(self, snapshot: dict) -> None:
        self.connected = bool(snapshot.get("connected", False))
        self.status_message = str(snapshot.get("status_message", ""))
        self.samples_total = int(snapshot.get("samples_total", 0))
        self.packets_total = int(snapshot.get("packets_total", 0))
        self.events_total = int(snapshot.get("events_total", 0))
        self.errors_total = int(snapshot.get("errors_total", 0))
        self.parse_error_count = int(snapshot.get("parse_error_count", 0))
        self.rx_bytes_total = int(snapshot.get("rx_bytes_total", 0))

        metrics = snapshot.get("latest_metrics", {})
        self.focus_score = float(metrics.get("focus_score", 0.0))
        self.relax_score = float(metrics.get("relax_score", 0.0))
        self.engagement_ratio = float(metrics.get("engagement_ratio", 0.0))
        self.delta_power = float(metrics.get("delta", 0.0))
        self.theta_power = float(metrics.get("theta", 0.0))
        self.alpha_power = float(metrics.get("alpha", 0.0))
        self.beta_power = float(metrics.get("beta", 0.0))
        self.gamma_power = float(metrics.get("gamma", 0.0))
        self.band_points = [
            {"band": "delta", "power": self.delta_power},
            {"band": "theta", "power": self.theta_power},
            {"band": "alpha", "power": self.alpha_power},
            {"band": "beta", "power": self.beta_power},
            {"band": "gamma", "power": self.gamma_power},
        ]

        self.plot_points = list(snapshot.get("plot_points", []))
        self.latest_sample_json = json.dumps(snapshot.get("latest_sample", {}), ensure_ascii=False, indent=2)
        self.event_lines = [
            f"{evt.get('level', 'INFO')}: {evt.get('message', '')}" for evt in snapshot.get("events", [])
        ]
        self.parse_error_lines = [str(x) for x in snapshot.get("parse_errors", [])]


def panel_card(*children, **props) -> rx.Component:
    return rx.box(
        *children,
        background=CARD_BG,
        border=CARD_BORDER,
        border_radius="14px",
        box_shadow=CARD_SHADOW,
        backdrop_filter="blur(6px)",
        padding="1rem",
        **props,
    )


def compact_stepper(
    label: str,
    value,
    on_minus,
    on_plus,
    suffix: str = "",
) -> rx.Component:
    return rx.box(
        rx.text(label, color="#334155", font_weight="600", font_size="0.8rem"),
        rx.hstack(
            rx.button("-", size="1", variant="soft", color_scheme="gray", on_click=on_minus),
            rx.box(
                rx.text(f"{value}{suffix}", font_family="monospace", font_weight="700", color="#1e293b"),
                min_width="72px",
                text_align="center",
                padding="0.25rem 0.4rem",
                border="1px solid #cbd5e1",
                border_radius="8px",
                background="#f8fafc",
            ),
            rx.button("+", size="1", variant="soft", color_scheme="gray", on_click=on_plus),
            spacing="2",
            align_items="center",
        ),
    )


def control_panel() -> rx.Component:
    return panel_card(
        rx.vstack(
            rx.heading("Connection & Controls", size="4", color="#0f172a"),
            rx.hstack(
                rx.input(
                    value=DashboardState.port,
                    on_change=DashboardState.set_port,
                    placeholder="Serial port (COMx)",
                    width="190px",
                ),
                rx.input(
                    value=DashboardState.baud,
                    on_change=DashboardState.set_baud,
                    width="120px",
                ),
                rx.input(
                    value=DashboardState.points_window,
                    on_change=DashboardState.set_points_window,
                    width="130px",
                    placeholder="Window points",
                ),
                rx.input(
                    value=DashboardState.refresh_ms,
                    on_change=DashboardState.set_refresh_ms,
                    width="120px",
                    placeholder="Refresh ms",
                ),
                rx.button(
                    rx.cond(DashboardState.simulate, "Mode: Simulation", "Mode: Serial"),
                    on_click=DashboardState.toggle_simulate,
                    color_scheme="indigo",
                    variant="soft",
                ),
                rx.button(
                    rx.cond(DashboardState.auto_refresh, "Auto Refresh: ON", "Auto Refresh: OFF"),
                    on_click=DashboardState.toggle_auto_refresh,
                    color_scheme="cyan",
                    variant="soft",
                ),
                spacing="3",
                align_items="center",
                wrap="wrap",
            ),
            rx.hstack(
                rx.button("Connect", color_scheme="green", size="2", on_click=DashboardState.connect),
                rx.button("Disconnect", color_scheme="tomato", size="2", on_click=DashboardState.disconnect),
                rx.button("Refresh", color_scheme="gray", size="2", on_click=DashboardState.refresh_once),
                rx.input(
                    value=DashboardState.command_text,
                    on_change=DashboardState.set_command_text,
                    width="180px",
                    placeholder="Firmware command",
                ),
                rx.button("Send CMD", variant="soft", color_scheme="iris", on_click=DashboardState.send_command),
                rx.button("CSV", variant="soft", color_scheme="gray", on_click=DashboardState.export_csv),
                rx.button("NPZ", variant="soft", color_scheme="gray", on_click=DashboardState.export_npz),
                rx.button("FIF", variant="soft", color_scheme="gray", on_click=DashboardState.export_fif),
                rx.button("JSON", variant="soft", color_scheme="gray", on_click=DashboardState.export_json),
                spacing="2",
                wrap="wrap",
            ),
            rx.hstack(
                compact_stepper(
                    "Line Chart Height",
                    DashboardState.line_chart_height,
                    DashboardState.dec_line_chart_height,
                    DashboardState.inc_line_chart_height,
                    " px",
                ),
                compact_stepper(
                    "Band Chart Height",
                    DashboardState.band_chart_height,
                    DashboardState.dec_band_chart_height,
                    DashboardState.inc_band_chart_height,
                    " px",
                ),
                compact_stepper(
                    "Panel Height",
                    DashboardState.side_panel_height,
                    DashboardState.dec_side_panel_height,
                    DashboardState.inc_side_panel_height,
                    " px",
                ),
                compact_stepper(
                    "Window Points",
                    DashboardState.points_window,
                    DashboardState.dec_points_window,
                    DashboardState.inc_points_window,
                ),
                compact_stepper(
                    "Refresh",
                    DashboardState.refresh_ms,
                    DashboardState.dec_refresh_ms,
                    DashboardState.inc_refresh_ms,
                    " ms",
                ),
                spacing="4",
                wrap="wrap",
            ),
            rx.box(
                rx.text(
                    DashboardState.status_message,
                    font_weight="600",
                    color="#0f172a",
                ),
                padding="0.65rem 0.85rem",
                border="1px solid #cbd5e1",
                border_radius="10px",
                background="#f8fafc",
            ),
            spacing="3",
            width="100%",
            align_items="start",
        ),
        width="100%",
    )


def stats_cards() -> rx.Component:
    card_style = {
        "padding": "0.9rem 1rem",
        "border": CARD_BORDER,
        "background": CARD_BG,
        "box_shadow": CARD_SHADOW,
        "border_radius": "14px",
        "min_width": "170px",
        "flex": "1",
        "max_width": "240px",
    }
    return rx.hstack(
        rx.box(rx.text("Samples", color="#475569"), rx.heading(DashboardState.samples_total, size="5"), style=card_style),
        rx.box(rx.text("Packets", color="#475569"), rx.heading(DashboardState.packets_total, size="5"), style=card_style),
        rx.box(rx.text("RX Bytes", color="#475569"), rx.heading(DashboardState.rx_bytes_total, size="5"), style=card_style),
        rx.box(rx.text("Focus", color="#475569"), rx.heading(DashboardState.focus_score, size="5"), style=card_style),
        rx.box(rx.text("Relax", color="#475569"), rx.heading(DashboardState.relax_score, size="5"), style=card_style),
        rx.box(
            rx.text("Engagement", color="#475569"),
            rx.heading(DashboardState.engagement_ratio, size="5"),
            style=card_style,
        ),
        wrap="wrap",
        spacing="4",
        width="100%",
    )


def eeg_chart() -> rx.Component:
    return panel_card(
        rx.vstack(
            rx.hstack(
                rx.heading("Raw EEG (uV)", size="4", color="#0f172a"),
                rx.spacer(),
                rx.text("Use the brush below the graph to zoom and pan.", color="#64748b", font_size="0.85rem"),
                width="100%",
                align_items="center",
            ),
            rx.recharts.line_chart(
                rx.recharts.cartesian_grid(stroke_dasharray="3 3"),
                rx.recharts.x_axis(data_key="x", tick={"fontSize": 11}),
                rx.recharts.y_axis(),
                rx.recharts.tooltip(),
                rx.recharts.legend(),
                rx.recharts.line(data_key="ch1_uv", stroke="#C1121F", type_="monotone", dot=False, stroke_width=2),
                rx.recharts.line(data_key="ch2_uv", stroke="#F77F00", type_="monotone", dot=False, stroke_width=2),
                rx.recharts.line(data_key="ch3_uv", stroke="#1D4ED8", type_="monotone", dot=False, stroke_width=2),
                rx.recharts.line(data_key="ch4_uv", stroke="#0F766E", type_="monotone", dot=False, stroke_width=2),
                rx.recharts.brush(data_key="x", height=22, stroke="#555"),
                data=DashboardState.plot_points,
                height=DashboardState.line_chart_height,
            ),
            width="100%",
            spacing="3",
        ),
        width="100%",
    )


def bands_chart() -> rx.Component:
    return panel_card(
        rx.vstack(
            rx.hstack(
                rx.heading("Band Power (Delta/Theta/Alpha/Beta/Gamma)", size="4", color="#0f172a"),
                rx.spacer(),
                rx.text("Resize with the height controls above.", color="#64748b", font_size="0.82rem"),
                width="100%",
            ),
            rx.recharts.bar_chart(
                rx.recharts.cartesian_grid(stroke_dasharray="3 3"),
                rx.recharts.x_axis(data_key="band", tick={"fontSize": 11}),
                rx.recharts.y_axis(),
                rx.recharts.tooltip(),
                rx.recharts.bar(data_key="power", fill="#4C1D95"),
                data=DashboardState.band_points,
                height=DashboardState.band_chart_height,
            ),
            width="100%",
            spacing="3",
        ),
        width="100%",
    )


def logs_and_sample() -> rx.Component:
    return rx.hstack(
        panel_card(
            rx.vstack(
                rx.heading("Latest Sample (raw + firmware fields)", size="4", color="#0f172a"),
                rx.box(
                    rx.code_block(DashboardState.latest_sample_json, language="json"),
                    max_height=DashboardState.side_panel_height,
                    overflow_y="auto",
                    border="1px solid #dbe3f5",
                    border_radius="10px",
                    background="#f8fafc",
                    padding="0.35rem",
                ),
                spacing="3",
                width="100%",
            ),
            width="100%",
            flex="1 1 560px",
            min_width="320px",
        ),
        panel_card(
            rx.vstack(
                rx.heading("Events", size="4", color="#0f172a"),
                rx.box(
                    rx.foreach(
                        DashboardState.event_lines,
                        lambda line: rx.text(line, font_size="0.82rem", color="#1e293b"),
                    ),
                    max_height=DashboardState.side_panel_height,
                    overflow_y="auto",
                    border="1px solid #dbe3f5",
                    border_radius="10px",
                    background="#f8fafc",
                    padding="0.6rem",
                ),
                rx.heading("Parse Errors", size="4", color="#0f172a"),
                rx.box(
                    rx.foreach(
                        DashboardState.parse_error_lines,
                        lambda line: rx.text(line, font_size="0.82rem", color="#7f1d1d"),
                    ),
                    max_height=DashboardState.side_panel_height,
                    overflow_y="auto",
                    border="1px solid #fecaca",
                    border_radius="10px",
                    background="#fff7f7",
                    padding="0.6rem",
                ),
                spacing="3",
                width="100%",
            ),
            width="100%",
            flex="1 1 560px",
            min_width="320px",
        ),
        spacing="4",
        wrap="wrap",
        align_items="stretch",
        width="100%",
    )


def index() -> rx.Component:
    return rx.container(
        rx.vstack(
            rx.box(
                rx.heading("Pendulum EEG Dashboard", size="8", color="#0b132b"),
                rx.text(
                    "Live raw EEG + diagnostics + exports, aligned with EEGFrontier firmware (BIN/COBS/CRC16).",
                    color="#334155",
                ),
                width="100%",
                background="linear-gradient(130deg, #f8fbff 0%, #e6f0ff 100%)",
                border=CARD_BORDER,
                border_radius="16px",
                box_shadow=CARD_SHADOW,
                padding="1.2rem",
            ),
            control_panel(),
            stats_cards(),
            eeg_chart(),
            bands_chart(),
            logs_and_sample(),
            spacing="4",
            width="100%",
        ),
        max_width="1700px",
        padding="1.2rem",
        background=APP_BG,
        min_height="100vh",
    )


app = rx.App()
app.add_page(index, title="Pendulum EEG")
