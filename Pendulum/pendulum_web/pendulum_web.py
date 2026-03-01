from __future__ import annotations

import asyncio
import json

import reflex as rx

from pendulum_eeg.reflex_bridge import get_engine

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

    # Graph / table data
    plot_points: list[dict[str, float]] = []
    raw_signal_points: list[dict[str, float]] = []
    gamma_signal_points: list[dict[str, float]] = []
    beta_signal_points: list[dict[str, float]] = []
    alpha_signal_points: list[dict[str, float]] = []
    theta_signal_points: list[dict[str, float]] = []
    delta_signal_points: list[dict[str, float]] = []
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

    # Sidebar active tab
    sidebar_tab: str = "connection"

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
        self._consume_snapshot(
            get_engine().get_snapshot(
                max_points=self._points_window_int(), event_limit=80
            )
        )

    def send_command(self) -> None:
        cmd = self.command_text.strip()
        if not cmd:
            return
        ok = get_engine().send_command(cmd)
        self.status_message = (
            f"Command sent: {cmd}" if ok else f"Failed to send command: {cmd}"
        )

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
            snapshot = get_engine().get_snapshot(
                max_points=points_window, event_limit=100
            )
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
            {"band": "Delta", "power": self.delta_power},
            {"band": "Theta", "power": self.theta_power},
            {"band": "Alpha", "power": self.alpha_power},
            {"band": "Beta", "power": self.beta_power},
            {"band": "Gamma", "power": self.gamma_power},
        ]

        signal_plot_points = snapshot.get("signal_plot_points", {})
        self.raw_signal_points = list(
            signal_plot_points.get("raw", snapshot.get("plot_points", []))
        )
        self.gamma_signal_points = list(signal_plot_points.get("gamma", []))
        self.beta_signal_points = list(signal_plot_points.get("beta", []))
        self.alpha_signal_points = list(signal_plot_points.get("alpha", []))
        self.theta_signal_points = list(signal_plot_points.get("theta", []))
        self.delta_signal_points = list(signal_plot_points.get("delta", []))
        self.plot_points = list(self.raw_signal_points)
        self.latest_sample_json = json.dumps(
            snapshot.get("latest_sample", {}), ensure_ascii=False, indent=2
        )
        self.event_lines = [
            f"{evt.get('level', 'INFO')}: {evt.get('message', '')}"
            for evt in snapshot.get("events", [])
        ]
        self.parse_error_lines = [str(x) for x in snapshot.get("parse_errors", [])]


def _section_label(text: str, icon_tag: str) -> rx.Component:
    """Small section label with icon used inside the sidebar."""
    return rx.hstack(
        rx.icon(tag=icon_tag, size=14, color=rx.color("accent", 10)),
        rx.text(
            text,
            size="1",
            weight="bold",
            color=rx.color("gray", 11),
            text_transform="uppercase",
            letter_spacing="0.05em",
        ),
        spacing="2",
        align_items="center",
        padding_bottom="0.25rem",
    )


def _compact_stepper(
    label: str, value, on_minus, on_plus, suffix: str = ""
) -> rx.Component:
    return rx.vstack(
        rx.text(label, size="1", weight="medium", color=rx.color("gray", 11)),
        rx.hstack(
            rx.icon_button(
                rx.icon(tag="minus", size=12),
                size="1",
                variant="soft",
                color_scheme="gray",
                on_click=on_minus,
                radius="full",
            ),
            rx.badge(
                rx.text(f"{value}{suffix}", font_family="monospace", weight="bold"),
                variant="surface",
                size="2",
                min_width="70px",
                text_align="center",
            ),
            rx.icon_button(
                rx.icon(tag="plus", size=12),
                size="1",
                variant="soft",
                color_scheme="gray",
                on_click=on_plus,
                radius="full",
            ),
            spacing="1",
            align_items="center",
        ),
        spacing="1",
    )

def _sidebar_connection() -> rx.Component:
    return rx.vstack(
        _section_label("Serial Port", "cable"),
        rx.input(
            value=DashboardState.port,
            on_change=DashboardState.set_port,
            placeholder="COMx or /dev/ttyUSB0",
            width="100%",
            size="2",
        ),
        _section_label("Baud Rate", "gauge"),
        rx.input(
            value=DashboardState.baud,
            on_change=DashboardState.set_baud,
            width="100%",
            size="2",
        ),
        rx.separator(size="4"),
        rx.hstack(
            rx.switch(
                checked=DashboardState.simulate,
                on_change=lambda _: DashboardState.toggle_simulate(),
                color_scheme="iris",
            ),
            rx.text(
                rx.cond(DashboardState.simulate, "Simulation", "Serial"),
                size="2",
                weight="medium",
            ),
            spacing="2",
            align_items="center",
        ),
        rx.hstack(
            rx.switch(
                checked=DashboardState.auto_refresh,
                on_change=lambda _: DashboardState.toggle_auto_refresh(),
                color_scheme="cyan",
            ),
            rx.text("Auto Refresh", size="2", weight="medium"),
            spacing="2",
            align_items="center",
        ),
        rx.separator(size="4"),
        rx.vstack(
            rx.tooltip(
                rx.button(
                    rx.icon(tag="plug", size=14),
                    "Connect",
                    color_scheme="green",
                    size="2",
                    width="100%",
                    on_click=DashboardState.connect,
                ),
                content="Start EEG stream",
            ),
            rx.tooltip(
                rx.button(
                    rx.icon(tag="unplug", size=14),
                    "Disconnect",
                    color_scheme="tomato",
                    size="2",
                    width="100%",
                    on_click=DashboardState.disconnect,
                ),
                content="Stop EEG stream",
            ),
            spacing="2",
            width="100%",
        ),
        rx.tooltip(
            rx.button(
                rx.icon(tag="refresh_cw", size=14),
                "Refresh Once",
                variant="soft",
                color_scheme="gray",
                size="2",
                width="100%",
                on_click=DashboardState.refresh_once,
            ),
            content="Fetch a single snapshot",
        ),
        spacing="3",
        width="100%",
    )

def _sidebar_commands() -> rx.Component:
    return rx.vstack(
        _section_label("Firmware Command", "terminal"),
        rx.hstack(
            rx.input(
                value=DashboardState.command_text,
                on_change=DashboardState.set_command_text,
                placeholder="e.g. INFO",
                size="2",
                width="100%",
            ),
            rx.icon_button(
                rx.icon(tag="send", size=14),
                size="2",
                color_scheme="iris",
                on_click=DashboardState.send_command,
            ),
            spacing="2",
            width="100%",
        ),
        rx.separator(size="4"),
        _section_label("Export Data", "download"),
        rx.hstack(
            rx.button("CSV", variant="surface", size="1", on_click=DashboardState.export_csv),
            rx.button("NPZ", variant="surface", size="1", on_click=DashboardState.export_npz),
            rx.button("FIF", variant="surface", size="1", on_click=DashboardState.export_fif),
            rx.button("JSON", variant="surface", size="1", on_click=DashboardState.export_json),
            spacing="2",
            wrap="wrap",
        ),
        spacing="3",
        width="100%",
    )

def _sidebar_display() -> rx.Component:
    return rx.vstack(
        _section_label("Chart Sizes", "sliders_horizontal"),
        _compact_stepper(
            "EEG Chart Height",
            DashboardState.line_chart_height,
            DashboardState.dec_line_chart_height,
            DashboardState.inc_line_chart_height,
            " px",
        ),
        _compact_stepper(
            "Band Chart Height",
            DashboardState.band_chart_height,
            DashboardState.dec_band_chart_height,
            DashboardState.inc_band_chart_height,
            " px",
        ),
        _compact_stepper(
            "Panel Height",
            DashboardState.side_panel_height,
            DashboardState.dec_side_panel_height,
            DashboardState.inc_side_panel_height,
            " px",
        ),
        rx.separator(size="4"),
        _section_label("Stream Tuning", "settings_2"),
        _compact_stepper(
            "Window Points",
            DashboardState.points_window,
            DashboardState.dec_points_window,
            DashboardState.inc_points_window,
        ),
        _compact_stepper(
            "Refresh Interval",
            DashboardState.refresh_ms,
            DashboardState.dec_refresh_ms,
            DashboardState.inc_refresh_ms,
            " ms",
        ),
        spacing="3",
        width="100%",
    )

def sidebar() -> rx.Component:
    return rx.box(
        rx.vstack(
            # Logo / title
            rx.hstack(
                rx.icon(tag="brain", size=22, color=rx.color("accent", 10)),
                rx.heading("Pendulum EEG", size="4", weight="bold"),
                spacing="2",
                align_items="center",
            ),
            rx.separator(size="4"),
            # Tabs inside sidebar
            rx.tabs.root(
                rx.tabs.list(
                    rx.tabs.trigger(
                        rx.hstack(
                            rx.icon(tag="plug", size=12),
                            rx.text("Connect", size="1"),
                            spacing="1",
                            align_items="center",
                        ),
                        value="connection",
                    ),
                    rx.tabs.trigger(
                        rx.hstack(
                            rx.icon(tag="terminal", size=12),
                            rx.text("Commands", size="1"),
                            spacing="1",
                            align_items="center",
                        ),
                        value="commands",
                    ),
                    rx.tabs.trigger(
                        rx.hstack(
                            rx.icon(tag="sliders_horizontal", size=12),
                            rx.text("Display", size="1"),
                            spacing="1",
                            align_items="center",
                        ),
                        value="display",
                    ),
                    size="1",
                ),
                rx.tabs.content(_sidebar_connection(), value="connection", padding_top="0.75rem"),
                rx.tabs.content(_sidebar_commands(), value="commands", padding_top="0.75rem"),
                rx.tabs.content(_sidebar_display(), value="display", padding_top="0.75rem"),
                default_value="connection",
                width="100%",
            ),
            # Status bar at the bottom
            rx.spacer(),
            rx.separator(size="4"),
            rx.hstack(
                rx.badge(
                    rx.cond(DashboardState.connected, "ONLINE", "OFFLINE"),
                    color_scheme=rx.cond(DashboardState.connected, "green", "gray"),
                    variant="solid",
                    size="1",
                ),
                rx.text(
                    DashboardState.status_message,
                    size="1",
                    color=rx.color("gray", 11),
                    trim="both",
                    style={"overflow": "hidden", "text_overflow": "ellipsis", "white_space": "nowrap", "max_width": "180px"},
                ),
                spacing="2",
                align_items="center",
                width="100%",
            ),
            spacing="3",
            height="100%",
            width="100%",
        ),
        width="280px",
        min_width="280px",
        height="100vh",
        position="sticky",
        top="0",
        padding="1rem",
        border_right=f"1px solid {rx.color('gray', 5)}",
        background=rx.color("gray", 2),
        overflow_y="auto",
    )

def _stat_card(label: str, value, icon_tag: str, color_scheme: str) -> rx.Component:
    return rx.card(
        rx.hstack(
            rx.box(
                rx.icon(tag=icon_tag, size=20, color=rx.color(color_scheme, 10)),
                padding="0.5rem",
                border_radius="var(--radius-3)",
                background=rx.color(color_scheme, 3),
            ),
            rx.vstack(
                rx.text(label, size="1", color=rx.color("gray", 11), weight="medium"),
                rx.heading(value, size="5", weight="bold", trim="both"),
                spacing="0",
            ),
            spacing="3",
            align_items="center",
        ),
        variant="surface",
        width="100%",
    )


def stats_row() -> rx.Component:
    return rx.grid(
        _stat_card("Samples", DashboardState.samples_total, "activity", "blue"),
        _stat_card("Packets", DashboardState.packets_total, "package", "iris"),
        _stat_card("RX Bytes", DashboardState.rx_bytes_total, "hard_drive_download", "cyan"),
        _stat_card("Focus", DashboardState.focus_score, "crosshair", "orange"),
        _stat_card("Relax", DashboardState.relax_score, "cloud", "green"),
        _stat_card("Engagement", DashboardState.engagement_ratio, "flame", "crimson"),
        columns=rx.breakpoints(initial="2", sm="3", lg="6"),
        spacing="3",
        width="100%",
    )


def eeg_chart(title: str, subtitle: str, data_points) -> rx.Component:
    return rx.card(
        rx.vstack(
            rx.hstack(
                rx.hstack(
                    rx.icon(tag="activity", size=16, color=rx.color("accent", 10)),
                    rx.heading(f"{title} (uV)", size="4", weight="bold"),
                    spacing="2",
                    align_items="center",
                ),
                rx.spacer(),
                rx.badge(subtitle, variant="soft", size="1"),
                width="100%",
                align_items="center",
            ),
            rx.recharts.line_chart(
                rx.recharts.cartesian_grid(stroke_dasharray="3 3", opacity=0.3),
                rx.recharts.x_axis(data_key="x", tick={"fontSize": 10}),
                rx.recharts.y_axis(tick={"fontSize": 10}),
                rx.recharts.tooltip(),
                rx.recharts.legend(icon_size=10),
                rx.recharts.line(data_key="ch1_uv", stroke="#ef4444", type_="monotone", dot=False, stroke_width=1.5, name="CH1 Left Eyebrow"),
                rx.recharts.line(data_key="ch2_uv", stroke="#f59e0b", type_="monotone", dot=False, stroke_width=1.5, name="CH2 Right Eyebrow"),
                rx.recharts.line(data_key="ch3_uv", stroke="#3b82f6", type_="monotone", dot=False, stroke_width=1.5, name="C3 Back Left"),
                rx.recharts.line(data_key="ch4_uv", stroke="#10b981", type_="monotone", dot=False, stroke_width=1.5, name="C4 Back Right"),
                rx.recharts.brush(data_key="x", height=20, stroke="#888"),
                data=data_points,
                height=DashboardState.line_chart_height,
            ),
            width="100%",
            spacing="3",
        ),
        variant="surface",
        width="100%",
    )


def electrode_positions_card() -> rx.Component:
    return rx.card(
        rx.vstack(
            rx.hstack(
                rx.icon(tag="map_pin", size=16, color=rx.color("accent", 10)),
                rx.heading("Electrode Positions", size="3", weight="bold"),
                spacing="2",
                align_items="center",
            ),
            rx.grid(
                rx.badge("CH1: above left eyebrow", variant="soft", color_scheme="red"),
                rx.badge("CH2: above right eyebrow", variant="soft", color_scheme="orange"),
                rx.badge("C3: back left, above nape", variant="soft", color_scheme="blue"),
                rx.badge("C4: back right, above nape", variant="soft", color_scheme="green"),
                rx.badge("REF: behind right ear", variant="soft", color_scheme="gray"),
                rx.badge("BIAS: left coronal sulcus, mid-ear height", variant="soft", color_scheme="gray"),
                columns=rx.breakpoints(initial="1", sm="2", lg="3"),
                spacing="2",
                width="100%",
            ),
            spacing="3",
            width="100%",
        ),
        variant="surface",
        width="100%",
    )


def eeg_signal_tabs() -> rx.Component:
    return rx.tabs.root(
        rx.tabs.list(
            rx.tabs.trigger("Raw EEG", value="raw"),
            rx.tabs.trigger("Gamma", value="gamma"),
            rx.tabs.trigger("Beta", value="beta"),
            rx.tabs.trigger("Alpha", value="alpha"),
            rx.tabs.trigger("Theta", value="theta"),
            rx.tabs.trigger("Delta", value="delta"),
            size="1",
            style={"flexWrap": "wrap", "rowGap": "0.4rem"},
        ),
        rx.tabs.content(
            rx.vstack(
                electrode_positions_card(),
                eeg_chart("Raw EEG", "Unfiltered microvolts", DashboardState.raw_signal_points),
                spacing="3",
                width="100%",
            ),
            value="raw",
            padding_top="0.75rem",
        ),
        rx.tabs.content(
            eeg_chart("Gamma", "Band-pass 30-45 Hz", DashboardState.gamma_signal_points),
            value="gamma",
            padding_top="0.75rem",
        ),
        rx.tabs.content(
            eeg_chart("Beta", "Band-pass 12-30 Hz", DashboardState.beta_signal_points),
            value="beta",
            padding_top="0.75rem",
        ),
        rx.tabs.content(
            eeg_chart("Alpha", "Band-pass 8-12 Hz", DashboardState.alpha_signal_points),
            value="alpha",
            padding_top="0.75rem",
        ),
        rx.tabs.content(
            eeg_chart("Theta", "Band-pass 4-8 Hz", DashboardState.theta_signal_points),
            value="theta",
            padding_top="0.75rem",
        ),
        rx.tabs.content(
            eeg_chart("Delta", "Band-pass 1-4 Hz", DashboardState.delta_signal_points),
            value="delta",
            padding_top="0.75rem",
        ),
        default_value="raw",
        width="100%",
    )


def bands_chart() -> rx.Component:
    return rx.card(
        rx.vstack(
            rx.hstack(
                rx.hstack(
                    rx.icon(tag="bar_chart_3", size=16, color=rx.color("accent", 10)),
                    rx.heading("Band Power", size="4", weight="bold"),
                    spacing="2",
                    align_items="center",
                ),
                rx.spacer(),
                rx.hstack(
                    rx.badge("DELTA", color_scheme="purple", variant="solid", size="1"),
                    rx.badge("THETA", color_scheme="blue", variant="solid", size="1"),
                    rx.badge("ALPHA", color_scheme="green", variant="solid", size="1"),
                    rx.badge("BETA", color_scheme="orange", variant="solid", size="1"),
                    rx.badge("GAMMA", color_scheme="red", variant="solid", size="1"),
                    spacing="1",
                ),
                width="100%",
                align_items="center",
            ),
            rx.recharts.bar_chart(
                rx.recharts.cartesian_grid(stroke_dasharray="3 3", opacity=0.3),
                rx.recharts.x_axis(data_key="band", tick={"fontSize": 10}),
                rx.recharts.y_axis(tick={"fontSize": 10}),
                rx.recharts.tooltip(),
                rx.recharts.bar(
                    data_key="power",
                    fill=rx.color("accent", 9),
                    radius=[4, 4, 0, 0],
                ),
                data=DashboardState.band_points,
                height=DashboardState.band_chart_height,
            ),
            width="100%",
            spacing="3",
        ),
        variant="surface",
        width="100%",
    )


def logs_panel() -> rx.Component:
    return rx.grid(
        rx.card(
            rx.vstack(
                rx.hstack(
                    rx.icon(tag="file_json", size=16, color=rx.color("accent", 10)),
                    rx.heading("Latest Sample", size="3", weight="bold"),
                    spacing="2",
                    align_items="center",
                ),
                rx.scroll_area(
                    rx.code_block(
                        DashboardState.latest_sample_json,
                        language="json",
                    ),
                    style={"height": DashboardState.side_panel_height},
                    scrollbars="vertical",
                ),
                spacing="3",
                width="100%",
            ),
            variant="surface",
            width="100%",
        ),
        rx.card(
            rx.vstack(
                rx.hstack(
                    rx.icon(tag="list", size=16, color=rx.color("accent", 10)),
                    rx.heading("Events", size="3", weight="bold"),
                    rx.spacer(),
                    rx.badge(DashboardState.events_total, variant="soft", size="1"),
                    spacing="2",
                    align_items="center",
                    width="100%",
                ),
                rx.scroll_area(
                    rx.vstack(
                        rx.foreach(
                            DashboardState.event_lines,
                            lambda line: rx.text(line, size="1", color=rx.color("gray", 12)),
                        ),
                        spacing="1",
                        width="100%",
                    ),
                    style={"height": DashboardState.side_panel_height},
                    scrollbars="vertical",
                ),
                rx.separator(size="4"),
                rx.hstack(
                    rx.icon(tag="alert_triangle", size=16, color=rx.color("tomato", 10)),
                    rx.heading("Parse Errors", size="3", weight="bold"),
                    rx.spacer(),
                    rx.badge(
                        DashboardState.parse_error_count,
                        variant="soft",
                        color_scheme="tomato",
                        size="1",
                    ),
                    spacing="2",
                    align_items="center",
                    width="100%",
                ),
                rx.scroll_area(
                    rx.vstack(
                        rx.foreach(
                            DashboardState.parse_error_lines,
                            lambda line: rx.text(line, size="1", color=rx.color("tomato", 11)),
                        ),
                        spacing="1",
                        width="100%",
                    ),
                    style={"height": DashboardState.side_panel_height},
                    scrollbars="vertical",
                ),
                spacing="3",
                width="100%",
            ),
            variant="surface",
            width="100%",
        ),
        columns=rx.breakpoints(initial="1", md="2"),
        spacing="3",
        width="100%",
    )

def main_content() -> rx.Component:
    return rx.vstack(
        # Top bar
        rx.hstack(
            rx.vstack(
                rx.heading("Dashboard", size="6", weight="bold"),
                rx.text(
                    "Live raw EEG | diagnostics | exports | EEGFrontier firmware (BIN/COBS/CRC16)",
                    size="2",
                    color=rx.color("gray", 11),
                ),
                spacing="1",
            ),
            rx.spacer(),
            rx.tooltip(
                rx.color_mode.button(size="2"),
                content="Toggle dark / light mode",
            ),
            width="100%",
            align_items="center",
            padding_bottom="0.5rem",
        ),
        # Stats
        stats_row(),
        # Main tabs: Charts / Logs
        rx.tabs.root(
            rx.tabs.list(
                rx.tabs.trigger(
                    rx.hstack(
                        rx.icon(tag="activity", size=14),
                        rx.text("EEG Signal"),
                        spacing="2",
                        align_items="center",
                    ),
                    value="eeg",
                ),
                rx.tabs.trigger(
                    rx.hstack(
                        rx.icon(tag="bar_chart_3", size=14),
                        rx.text("Band Power"),
                        spacing="2",
                        align_items="center",
                    ),
                    value="bands",
                ),
                rx.tabs.trigger(
                    rx.hstack(
                        rx.icon(tag="scroll_text", size=14),
                        rx.text("Logs & Data"),
                        spacing="2",
                        align_items="center",
                    ),
                    value="logs",
                ),
                rx.tabs.trigger(
                    rx.hstack(
                        rx.icon(tag="layout_dashboard", size=14),
                        rx.text("All"),
                        spacing="2",
                        align_items="center",
                    ),
                    value="all",
                ),
            ),
            rx.tabs.content(
                rx.box(eeg_signal_tabs(), padding_top="0.75rem"),
                value="eeg",
            ),
            rx.tabs.content(
                rx.box(bands_chart(), padding_top="0.75rem"),
                value="bands",
            ),
            rx.tabs.content(
                rx.box(logs_panel(), padding_top="0.75rem"),
                value="logs",
            ),
            rx.tabs.content(
                rx.vstack(
                    eeg_signal_tabs(),
                    bands_chart(),
                    logs_panel(),
                    spacing="3",
                    padding_top="0.75rem",
                ),
                value="all",
            ),
            default_value="all",
            width="100%",
        ),
        spacing="4",
        width="100%",
        padding="1.25rem",
        flex="1",
        overflow_y="auto",
    )

def index() -> rx.Component:
    return rx.hstack(
        sidebar(),
        main_content(),
        spacing="0",
        width="100%",
        min_height="100vh",
        align_items="stretch",
    )


app = rx.App(
    theme=rx.theme(
        appearance="dark",
        accent_color="iris",
        gray_color="slate",
        radius="medium",
        has_background=True,
    ),
)
app.add_page(index, title="Pendulum EEG Dashboard")




