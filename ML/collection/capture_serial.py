#!/usr/bin/env python3
"""Capture STM32 USART1 output and send single-key collection markers."""

from __future__ import annotations

import argparse
from datetime import datetime
from pathlib import Path
import sys

try:
    import msvcrt
except ImportError as exc:  # pragma: no cover - this project is collected on Windows
    raise SystemExit("This capture helper currently requires Windows.") from exc

VALID_COMMANDS = {"S", "P", "R", "C", "E", "H", "?"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture STM32 TinyML sensor data and mark pollution events."
    )
    parser.add_argument("--port", required=True, help="Serial port, for example COM5")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--output",
        type=Path,
        help="Raw capture path. Defaults to ML/collection/raw/capture_<time>.txt",
    )
    return parser.parse_args()


def default_output_path() -> Path:
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return Path(__file__).resolve().parent / "raw" / f"capture_{timestamp}.txt"


def main() -> int:
    args = parse_args()

    try:
        import serial
    except ImportError as exc:
        raise SystemExit(
            "Missing pyserial. Install it once with: "
            "python -m pip install -r ML/collection/requirements.txt"
        ) from exc

    output_path = args.output or default_output_path()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    print(f"Opening {args.port} at {args.baud} baud")
    print(f"Saving raw, irreplaceable data to: {output_path}")
    print("Keys: S=start, P=source placed, R=source removed, C=clean, E=end")
    print("Press Ctrl+C only after sending E and seeing the session_end event.")

    try:
        with serial.Serial(args.port, args.baud, timeout=0.10) as uart:
            with output_path.open("w", encoding="utf-8", newline="") as output_file:
                while True:
                    raw_line = uart.readline()
                    if raw_line:
                        line = raw_line.decode("utf-8", errors="replace")
                        output_file.write(line)
                        output_file.flush()
                        print(line, end="")

                    while msvcrt.kbhit():
                        key = msvcrt.getwch()
                        upper_key = key.upper()
                        if upper_key in VALID_COMMANDS:
                            uart.write(upper_key.encode("ascii"))
                            uart.flush()
                            print(f"\n[PC] sent command: {upper_key}")
    except KeyboardInterrupt:
        print(f"\nCapture stopped. Raw file kept at: {output_path}")
        return 0
    except serial.SerialException as exc:
        print(f"Serial error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
