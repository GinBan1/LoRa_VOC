#!/usr/bin/env python3
"""Validate and split an STM32 serial capture into sensor and event CSV files."""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from dataclasses import dataclass, field
import math
from pathlib import Path
from typing import Iterable


LOGGER_FIELDS = [
    "record_type",
    "session_id",
    "timestamp_ms",
    "uptime_ms",
    "event_id",
    "event_state",
    "event_name",
    "temperature",
    "humidity",
    "pressure",
    "sraw_voc",
    "voc_index",
    "bme680_valid",
    "sgp40_valid",
]

SENSOR_FIELDS = [
    "timestamp_ms",
    "temperature",
    "humidity",
    "pressure",
    "sraw_voc",
    "voc_index",
    "event_id",
    "event_state",
    "uptime_ms",
]

EVENT_FIELDS = [
    "timestamp_ms",
    "uptime_ms",
    "event_id",
    "event_state",
    "event_name",
]


@dataclass
class Session:
    sensor_all: list[dict[str, str]] = field(default_factory=list)
    sensor_valid: list[dict[str, str]] = field(default_factory=list)
    events: list[dict[str, str]] = field(default_factory=list)
    malformed_rows: int = 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Clean STM32 DATA/EVENT records from a raw terminal capture."
    )
    parser.add_argument("capture", type=Path, help="Raw .txt serial capture")
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="Output directory (default: processed/<capture_stem>)",
    )
    return parser.parse_args()


def find_logger_record(line: str) -> str | None:
    positions = [position for marker in ("DATA,", "EVENT,")
                 if (position := line.find(marker)) >= 0]
    if not positions:
        return None
    return line[min(positions):].strip()


def parse_csv_record(text: str) -> dict[str, str] | None:
    try:
        values = next(csv.reader([text]))
    except (csv.Error, StopIteration):
        return None

    if len(values) != len(LOGGER_FIELDS):
        return None
    return dict(zip(LOGGER_FIELDS, values, strict=True))


def sensor_values_are_valid(record: dict[str, str]) -> bool:
    if record["bme680_valid"] != "1" or record["sgp40_valid"] != "1":
        return False

    try:
        numeric_values = (
            float(record["temperature"]),
            float(record["humidity"]),
            float(record["pressure"]),
            float(record["sraw_voc"]),
            float(record["voc_index"]),
        )
    except ValueError:
        return False

    return all(math.isfinite(value) for value in numeric_values)


def select_fields(record: dict[str, str], fields: Iterable[str]) -> dict[str, str]:
    return {name: record[name] for name in fields}


def load_sessions(capture_path: Path) -> tuple[dict[int, Session], int]:
    sessions: dict[int, Session] = defaultdict(Session)
    ignored_lines = 0

    with capture_path.open("r", encoding="utf-8", errors="replace") as source:
        for line in source:
            record_text = find_logger_record(line)
            if record_text is None:
                ignored_lines += 1
                continue

            record = parse_csv_record(record_text)
            if record is None:
                ignored_lines += 1
                continue

            try:
                session_id = int(record["session_id"])
            except ValueError:
                ignored_lines += 1
                continue

            session = sessions[session_id]
            if record["record_type"] == "DATA":
                sensor_row = select_fields(record, SENSOR_FIELDS)
                session.sensor_all.append(sensor_row)
                if sensor_values_are_valid(record):
                    session.sensor_valid.append(sensor_row)
            elif record["record_type"] == "EVENT":
                session.events.append(select_fields(record, EVENT_FIELDS))
            else:
                session.malformed_rows += 1

    return dict(sessions), ignored_lines


def write_csv(path: Path, fields: list[str], rows: list[dict[str, str]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as destination:
        writer = csv.DictWriter(destination, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def count_timestamp_gaps(rows: list[dict[str, str]]) -> int:
    timestamps = [int(row["timestamp_ms"]) for row in rows]
    return sum(
        current - previous != 1000
        for previous, current in zip(timestamps, timestamps[1:])
    )


def main() -> int:
    args = parse_args()
    capture_path = args.capture.resolve()
    if not capture_path.is_file():
        raise SystemExit(f"Capture file not found: {capture_path}")

    output_dir = args.output_dir
    if output_dir is None:
        output_dir = (
            Path(__file__).resolve().parent / "processed" / capture_path.stem
        )
    output_dir.mkdir(parents=True, exist_ok=True)

    sessions, ignored_lines = load_sessions(capture_path)
    if not sessions:
        raise SystemExit(
            "No valid DATA/EVENT records found. Check baud rate and capture file."
        )

    for session_id, session in sorted(sessions.items()):
        prefix = f"session_{session_id:03d}"
        write_csv(
            output_dir / f"{prefix}_sensor_all.csv",
            SENSOR_FIELDS,
            session.sensor_all,
        )
        write_csv(
            output_dir / f"{prefix}_sensor_valid.csv",
            SENSOR_FIELDS,
            session.sensor_valid,
        )
        write_csv(
            output_dir / f"{prefix}_events.csv",
            EVENT_FIELDS,
            session.events,
        )

        invalid_count = len(session.sensor_all) - len(session.sensor_valid)
        timestamp_gaps = count_timestamp_gaps(session.sensor_all)
        print(
            f"session {session_id}: "
            f"{len(session.sensor_all)} samples, "
            f"{len(session.events)} events, "
            f"{invalid_count} invalid samples, "
            f"{timestamp_gaps} timestamp gaps"
        )

    print(f"Ignored non-record/debug lines: {ignored_lines}")
    print(f"Processed files written to: {output_dir.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
