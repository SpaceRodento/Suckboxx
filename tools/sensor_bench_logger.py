#!/usr/bin/env python3
"""Collect raw ADC samples from the Suckboxx ESP32 firmware.

This is the first concrete step for the sensor validation roadmap:
- capture raw channel values from the serial stream
- store them as CSV for later analysis
- help build the sensor truth table and per-channel baseline

Expected firmware output format from firmware/src/main.cpp:
    <raw_ch1>\t<raw_ch2>\t<raw_ch3>

Usage examples:
    python tools/sensor_bench_logger.py --port COM5 --duration 30
    python tools/sensor_bench_logger.py --port /dev/ttyUSB0 --output bench.csv
"""

from __future__ import annotations

import argparse
import csv
import signal
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover - helpful runtime failure
    raise SystemExit(
        "PySerial is required. Install it with: pip install pyserial"
    ) from exc


class GracefulStop:
    def __init__(self) -> None:
        self.stop_requested = False
        signal.signal(signal.SIGINT, self._handle_signal)
        signal.signal(signal.SIGTERM, self._handle_signal)

    def _handle_signal(self, *_args) -> None:
        self.stop_requested = True


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Log raw ADC readings from Suckboxx firmware.")
    parser.add_argument("--port", required=True, help="Serial port, e.g. COM5 or /dev/ttyUSB0")
    parser.add_argument("--baudrate", type=int, default=115200, help="Serial baud rate (default: 115200)")
    parser.add_argument("--duration", type=float, default=30.0, help="Capture duration in seconds (default: 30)")
    parser.add_argument("--output", default="sensor_bench.csv", help="Output CSV path")
    return parser.parse_args()


def append_row(writer: csv.writer, start_ms: int, raw_values: list[int]) -> None:
    ts_ms = int((time.monotonic_ns() / 1_000_000) - start_ms)
    writer.writerow([ts_ms, raw_values[0], raw_values[1], raw_values[2]])


def main() -> int:
    args = parse_args()
    stop = GracefulStop()

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    serial_port = serial.Serial(args.port, args.baudrate, timeout=0.5)
    serial_port.reset_input_buffer()

    start_ms = int(time.monotonic_ns() / 1_000_000)
    sample_count = 0

    with output_path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.writer(fh)
        writer.writerow(["time_ms", "ch1_raw", "ch2_raw", "ch3_raw"])

        deadline = time.monotonic() + args.duration
        while time.monotonic() < deadline and not stop.stop_requested:
            if serial_port.in_waiting <= 0:
                time.sleep(0.01)
                continue

            try:
                line = serial_port.readline().decode("utf-8", errors="replace").strip()
            except UnicodeDecodeError:
                continue

            if not line:
                continue

            parts = line.split()
            if len(parts) != 3:
                print(f"Skipping malformed line: {line}")
                continue

            try:
                values = [int(part) for part in parts]
            except ValueError:
                print(f"Skipping non-integer line: {line}")
                continue

            append_row(writer, start_ms, values)
            sample_count += 1
            print(f"{sample_count}: {values[0]}\t{values[1]}\t{values[2]}")

    serial_port.close()
    print(f"Saved {sample_count} samples to {output_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
