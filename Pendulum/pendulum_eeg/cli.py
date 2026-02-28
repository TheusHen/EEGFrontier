from __future__ import annotations

import argparse
import time

from .reflex_bridge import get_engine


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Pendulum EEG CLI.")
    sub = parser.add_subparsers(dest="cmd", required=True)

    capture = sub.add_parser("capture", help="Capture for N seconds and export.")
    capture.add_argument("--port", default="", help="Serial port, e.g.: COM5.")
    capture.add_argument("--baud", type=int, default=921600)
    capture.add_argument("--seconds", type=int, default=20)
    capture.add_argument("--simulate", action="store_true")
    capture.add_argument("--fif", action="store_true", help="Export FIF as well.")

    sub.add_parser("snapshot", help="Show a quick snapshot of current state.")
    return parser.parse_args(argv)


def run_capture(args: argparse.Namespace) -> int:
    engine = get_engine()
    engine.start(port=args.port, baud=args.baud, simulate=args.simulate)
    print(f"[capture] collecting for {args.seconds}s...")
    time.sleep(max(1, int(args.seconds)))

    snap = engine.get_snapshot(max_points=5)
    print(
        f"[capture] status={snap['status_message']} samples={snap['samples_total']} "
        f"parse_errors={snap['parse_error_count']}"
    )

    csv_path = engine.export_csv()
    npz_path = engine.export_npz()
    json_path = engine.export_json_snapshot()
    print(f"[capture] csv:  {csv_path}")
    print(f"[capture] npz:  {npz_path}")
    print(f"[capture] json: {json_path}")

    if args.fif:
        fif_path = engine.export_fif()
        print(f"[capture] fif:  {fif_path}")

    engine.stop()
    return 0


def run_snapshot() -> int:
    snap = get_engine().get_snapshot(max_points=5)
    print(f"status={snap['status_message']}")
    print(f"running={snap['running']} connected={snap['connected']} simulate={snap['simulate']}")
    print(f"samples_total={snap['samples_total']} packets_total={snap['packets_total']}")
    print(f"parse_error_count={snap['parse_error_count']}")
    return 0


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.cmd == "capture":
        return run_capture(args)
    if args.cmd == "snapshot":
        return run_snapshot()
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
