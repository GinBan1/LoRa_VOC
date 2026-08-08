"""Prepare a leakage-resistant Edge Impulse warning-state dataset.

Raw session CSV files are never modified. Headerless normal captures receive
canonical derived copies, and only audited 30-second samples are exported.

Labels:
  normal  - source absent, DATA state is clean, and the surrounding audited
            interval contains a strict stable five-minute window.
  warning - the input lies fully inside a source_placed -> source_removed
            pollution_present interval.

This is deliberately a current warning-state classifier. It does not claim to
predict an external pollution source before it is introduced.
"""

from __future__ import annotations

import csv
import shutil
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pandas as pd


ROOT = Path(__file__).resolve().parents[1]
RAW_DATA = ROOT / "ML" / "raw_sessions"
OUTPUT = ROOT / "ML" / "edge_impulse_warning_state_v2"
PROCESSED = OUTPUT / "processed_sources"
TRAINING = OUTPUT / "training"
TESTING = OUTPUT / "testing"

SOURCE_FILES = [
    *(RAW_DATA / f"session{i}.csv" for i in range(1, 9)),
    *(RAW_DATA / f"normal{i}.csv" for i in range(1, 4)),
]

# Whole source sessions are assigned to only one category. This is the primary
# protection against adjacent-window leakage.
SUBSET = {
    "session1.csv": "training",
    "session2.csv": "training",
    "session3.csv": "training",
    "session5.csv": "training",
    "session6.csv": "training",
    "normal1.csv": "training",
    "normal2.csv": "training",
    "session4.csv": "testing",
    "session7.csv": "testing",
    "session8.csv": "testing",
    "normal3.csv": "testing",
}

ENVIRONMENT_NOTE = {
    "session1.csv": "空调房（用户说明）",
    "session2.csv": "空调房（用户说明）",
    "session3.csv": "空调房（用户说明）",
    "session4.csv": "空调房（用户说明）",
    "session5.csv": "客厅（用户说明）",
    "session6.csv": "未记录",
    "session7.csv": "未记录",
    "session8.csv": "未记录",
    "normal1.csv": "无空调环境（用户说明）",
    "normal2.csv": "无空调环境（用户说明）",
    "normal3.csv": "无空调环境（用户说明）",
}

COLS = [
    "record_type", "session_id", "timestamp_ms", "uptime_ms", "event_id",
    "event_state", "event_name", "temperature", "humidity", "pressure",
    "sraw_voc", "voc_index", "bme680_valid", "sgp40_valid",
]
NUMERIC = [
    "session_id", "timestamp_ms", "uptime_ms", "event_id", "temperature",
    "humidity", "pressure", "sraw_voc", "voc_index", "bme680_valid",
    "sgp40_valid",
]
INPUTS = ["temperature", "humidity", "pressure", "sraw_voc"]
LABEL = "warning_state"
METADATA = "source_session"
UPLOAD_COLUMNS = ["timestamp_ms", *INPUTS, LABEL, METADATA]
SAMPLE_ROWS = 31  # 0..30,000 ms, matching the prior CSV Wizard behavior.
STRICT_HORIZON_ROWS = 300


@dataclass(frozen=True)
class LoadedSource:
    path: Path
    frame: pd.DataFrame
    header_count: int
    comments_ignored: int
    malformed_rows: tuple[int, ...]


def load_source(path: Path) -> LoadedSource:
    records: list[list[str]] = []
    malformed: list[int] = []
    headers = 0
    comments = 0
    with path.open("r", encoding="utf-8-sig", errors="replace", newline="") as handle:
        for line_no, row in enumerate(csv.reader(handle), start=1):
            if not row or not any(cell.strip() for cell in row):
                continue
            first = row[0].strip()
            if first == "record_type":
                headers += 1
                continue
            if first.startswith("#") or first.startswith("20"):
                comments += 1
                continue
            if len(row) != len(COLS) or first not in {"DATA", "EVENT"}:
                malformed.append(line_no)
                continue
            records.append(row)

    frame = pd.DataFrame(records, columns=COLS)
    for col in NUMERIC:
        frame[col] = pd.to_numeric(frame[col], errors="coerce")
    if frame.empty:
        raise ValueError(f"{path.name}: no DATA/EVENT records")
    return LoadedSource(path, frame, headers, comments, tuple(malformed))


def canonical_value(value: object) -> object:
    if pd.isna(value):
        return ""
    if isinstance(value, (np.integer, int)):
        return int(value)
    if isinstance(value, (np.floating, float)):
        number = float(value)
        return int(number) if number.is_integer() else number
    return value


def write_canonical_source(source: LoadedSource) -> Path:
    path = PROCESSED / f"{source.path.stem}_canonical.csv"
    output = source.frame[COLS].copy()
    for col in COLS:
        output[col] = output[col].map(canonical_value)
    output.to_csv(path, index=False, encoding="utf-8", lineterminator="\n")
    return path


def data_for_session(frame: pd.DataFrame, session_id: int) -> pd.DataFrame:
    data = frame.loc[
        (frame["record_type"] == "DATA")
        & (frame["session_id"] == session_id)
        & (frame["bme680_valid"] == 1)
        & (frame["sgp40_valid"] == 1)
    ].copy()
    data = data.dropna(subset=["timestamp_ms", *INPUTS, "voc_index"])
    return data.sort_values("timestamp_ms").drop_duplicates("timestamp_ms").reset_index(drop=True)


def events_for_session(frame: pd.DataFrame, session_id: int) -> pd.DataFrame:
    events = frame.loc[
        (frame["record_type"] == "EVENT")
        & (frame["session_id"] == session_id)
    ].copy()
    return events.sort_values("timestamp_ms").reset_index(drop=True)


def strict_normal_intervals(data: pd.DataFrame) -> tuple[list[tuple[int, int]], int]:
    if len(data) < STRICT_HORIZON_ROWS:
        return [], 0
    sraw = data["sraw_voc"].to_numpy(dtype=float)
    voc = data["voc_index"].to_numpy(dtype=float)
    ts = data["timestamp_ms"].to_numpy(dtype=np.int64)
    starts: list[int] = []
    for i in range(len(data) - STRICT_HORIZON_ROWS + 1):
        segment_sraw = sraw[i:i + STRICT_HORIZON_ROWS]
        segment_voc = voc[i:i + STRICT_HORIZON_ROWS]
        if (
            float(segment_sraw.max() - segment_sraw.min()) < 80
            and abs(float(segment_voc[-1] - segment_voc[0])) < 5
        ):
            starts.append(i)
    if not starts:
        return [], 0
    groups = np.split(np.asarray(starts), np.flatnonzero(np.diff(starts) != 1) + 1)
    intervals = [
        (int(ts[int(group[0])]), int(ts[int(group[-1]) + STRICT_HORIZON_ROWS - 1]))
        for group in groups
    ]
    return merge_intervals(intervals), len(starts)


def warning_intervals(events: pd.DataFrame) -> list[tuple[int, int]]:
    intervals: list[tuple[int, int]] = []
    event_rows = list(events.itertuples())
    for index, row in enumerate(event_rows):
        if row.event_name != "source_placed" or pd.isna(row.timestamp_ms):
            continue
        for later in event_rows[index + 1:]:
            if later.event_name == "source_removed" and not pd.isna(later.timestamp_ms):
                intervals.append((int(row.timestamp_ms), int(later.timestamp_ms)))
                break
    return merge_intervals(intervals)


def merge_intervals(intervals: list[tuple[int, int]]) -> list[tuple[int, int]]:
    merged: list[list[int]] = []
    for start, end in sorted(intervals):
        if not merged or start > merged[-1][1] + 1000:
            merged.append([start, end])
        else:
            merged[-1][1] = max(merged[-1][1], end)
    return [(start, end) for start, end in merged]


def continuous_parts(frame: pd.DataFrame) -> list[pd.DataFrame]:
    if frame.empty:
        return []
    timestamps = frame["timestamp_ms"].to_numpy(dtype=np.int64)
    boundaries = np.flatnonzero(np.diff(timestamps) != 1000) + 1
    starts = np.r_[0, boundaries]
    stops = np.r_[boundaries, len(frame)]
    return [
        frame.iloc[int(start):int(stop)].reset_index(drop=True)
        for start, stop in zip(starts, stops, strict=True)
        if stop > start
    ]


def rows_in_intervals(
    data: pd.DataFrame,
    intervals: list[tuple[int, int]],
    required_state: str,
) -> list[pd.DataFrame]:
    output: list[pd.DataFrame] = []
    for start, end in intervals:
        chosen = data.loc[
            (data["timestamp_ms"] >= start)
            & (data["timestamp_ms"] <= end)
            & (data["event_state"] == required_state)
        ].copy()
        output.extend(continuous_parts(chosen))
    return output


def export_samples(
    parts: list[pd.DataFrame],
    label: str,
    source_key: str,
    source_file: str,
    session_id: int,
    subset: str,
    rule: str,
    manifest: list[dict],
) -> int:
    directory = TRAINING if subset == "training" else TESTING
    count = 0
    for part_index, part in enumerate(parts, start=1):
        for start in range(0, len(part) - SAMPLE_ROWS + 1, SAMPLE_ROWS):
            sample = part.iloc[start:start + SAMPLE_ROWS].copy()
            source_start = int(sample["timestamp_ms"].iloc[0])
            source_end = int(sample["timestamp_ms"].iloc[-1])
            expected = source_start + np.arange(SAMPLE_ROWS, dtype=np.int64) * 1000
            if not np.array_equal(sample["timestamp_ms"].to_numpy(dtype=np.int64), expected):
                continue
            count += 1
            name = f"{source_key}_{label}_p{part_index:02d}_{count:03d}.csv"
            upload = sample[INPUTS].copy()
            upload.insert(0, "timestamp_ms", np.arange(SAMPLE_ROWS, dtype=np.int64) * 1000)
            upload[LABEL] = label
            upload[METADATA] = source_key
            upload = upload[UPLOAD_COLUMNS]
            output_path = directory / name
            upload.to_csv(output_path, index=False, encoding="utf-8", lineterminator="\n")
            manifest.append({
                "subset": subset,
                "sample_file": name,
                "label": label,
                "source_file": source_file,
                "source_session": source_key,
                "session_id": session_id,
                "source_start_ms": source_start,
                "source_end_ms": source_end,
                "selection_rule": rule,
            })
    return count


def validate_outputs(manifest: pd.DataFrame) -> None:
    if manifest.empty:
        raise ValueError("no samples generated")
    for row in manifest.itertuples():
        path = (TRAINING if row.subset == "training" else TESTING) / row.sample_file
        frame = pd.read_csv(path)
        if list(frame.columns) != UPLOAD_COLUMNS:
            raise ValueError(f"{path.name}: incorrect columns")
        if len(frame) != SAMPLE_ROWS:
            raise ValueError(f"{path.name}: incorrect row count")
        if not np.array_equal(frame["timestamp_ms"].to_numpy(), np.arange(SAMPLE_ROWS) * 1000):
            raise ValueError(f"{path.name}: timestamp validation failed")
        if frame[LABEL].nunique() != 1 or frame[LABEL].iloc[0] != row.label:
            raise ValueError(f"{path.name}: label validation failed")
        if frame[INPUTS].isna().any().any():
            raise ValueError(f"{path.name}: missing sensor value")
    duplicate_sources = set(
        manifest.loc[manifest["subset"] == "training", "source_session"]
    ).intersection(
        manifest.loc[manifest["subset"] == "testing", "source_session"]
    )
    if duplicate_sources:
        raise ValueError(f"session leakage: {sorted(duplicate_sources)}")


def write_readme(manifest: pd.DataFrame, source_summary: pd.DataFrame) -> None:
    counts = manifest.groupby(["subset", "label"]).size().unstack(fill_value=0)
    train_normal = int(counts.loc["training"].get("normal", 0))
    train_warning = int(counts.loc["training"].get("warning", 0))
    test_normal = int(counts.loc["testing"].get("normal", 0))
    test_warning = int(counts.loc["testing"].get("warning", 0))
    excluded = source_summary.loc[source_summary["normal_samples"] + source_summary["warning_samples"] == 0, "source_file"].tolist()
    readme = f"""# Edge Impulse warning_state v2 数据集

本目录由 `ML/prepare_edge_impulse_sessions_v2.py` 从根目录原始 session CSV 可重复生成。原始文件不会被修改。

## 模型语义

- `normal`：污染源不在场、记录状态为 `clean`，并通过严格稳定性检查。
- `warning`：完整 30 秒输入位于 `source_placed` 到 `source_removed` 之间。
- 恢复阶段、漂移阶段、输入跨事件边界以及有采样缺口的窗口全部排除。

这是“当前污染事件预警”分类器，不是“提前知道未来会有人放入污染源”的模型。等补齐多污染源、跨日期数据后，才能继续研究真正的未来趋势预测。

## 严格 normal 条件

连续 5 分钟（300 个 1 Hz 采样点）同时满足：

```text
max(sraw_voc) - min(sraw_voc) < 80
abs(voc_index[end] - voc_index[start]) < 5
event_state == clean
```

## 独立 session 划分

| 数据集 | normal | warning | 合计 |
|:---|---:|---:|---:|
| Training | {train_normal} | {train_warning} | {train_normal + train_warning} |
| Test | {test_normal} | {test_warning} | {test_normal + test_warning} |

同一个 `source_session` 不会同时出现在 Training 和 Test。`normal3.csv` 已生成标准表头副本，但因没有合格的严格 5 分钟窗口而没有进入模型样本。

未生成任何样本的源文件：{', '.join(excluded) if excluded else '无'}。

## Edge Impulse 上传

分别把 `training/` 上传为 **Training**，把 `testing/` 上传为 **Test**。选择文件夹上传即可。

CSV Wizard：

```text
Time series: Yes
Format: Each row contains a reading, sensor values are columns
Time column: timestamp_ms
Timestamp type: Time elapsed in milliseconds
Label column: warning_state
Value columns: temperature, humidity, pressure, sraw_voc
Metadata column: source_session
Sample length: Unlimited（每个 CSV 已经是一个完整 30 秒样本）
```

Create Impulse：

```text
Frequency: 1 Hz
Window size: 30000 ms
Window increase: 30000 ms
Zero-pad data: Off
Processing block: Raw Data
Learning block: Classification
Normalize features: StandardScaler
```

训练设置建议：30 cycles、learning rate 0.0005、validation 20%、Profile int8 开启。Advanced training settings 中将 `Split train/validation set on metadata key` 设置为 `source_session`，避免同一 session 的相邻样本同时出现在训练和验证。

## 其他文件

- `manifest.csv`：每个上传样本的原始文件、原始时间区间、标签和划分。
- `source_summary.csv`：每个原始记录的质量、合格窗口和生成样本数。
- `processed_sources/normal2_canonical.csv`、`normal3_canonical.csv`：为无表头原始文件生成的标准表头副本。
"""
    (OUTPUT / "README_CN.md").write_text(readme, encoding="utf-8")


def reset_output() -> None:
    if OUTPUT.exists():
        shutil.rmtree(OUTPUT)
    for directory in (PROCESSED, TRAINING, TESTING):
        directory.mkdir(parents=True, exist_ok=True)


def main() -> None:
    reset_output()
    manifest_rows: list[dict] = []
    summary_rows: list[dict] = []

    for path in SOURCE_FILES:
        source = load_source(path)
        if path.name in {"normal2.csv", "normal3.csv"}:
            write_canonical_source(source)

        session_ids = source.frame["session_id"].dropna().astype(int).unique().tolist()
        for session_id in session_ids:
            source_key = f"{path.stem}_s{session_id}"
            subset = SUBSET[path.name]
            data = data_for_session(source.frame, session_id)
            events = events_for_session(source.frame, session_id)
            normal_intervals, strict_count = strict_normal_intervals(data)
            warning_ranges = warning_intervals(events)
            normal_parts = rows_in_intervals(data, normal_intervals, "clean")
            warning_parts = rows_in_intervals(data, warning_ranges, "pollution_present")

            normal_count = export_samples(
                normal_parts, "normal", source_key, path.name, session_id,
                subset, "strict_5m_stable_and_clean", manifest_rows,
            )
            warning_count = export_samples(
                warning_parts, "warning", source_key, path.name, session_id,
                subset, "inside_source_placed_to_source_removed", manifest_rows,
            )
            data_ts = data["timestamp_ms"].to_numpy(dtype=np.int64)
            gap_count = int(np.sum(np.diff(data_ts) != 1000)) if len(data_ts) > 1 else 0
            notes: list[str] = []
            if source.header_count == 0:
                notes.append("原始文件无表头；已生成 canonical 副本")
            if source.malformed_rows:
                notes.append(f"忽略损坏行 {list(source.malformed_rows)}")
            if strict_count == 0:
                notes.append("无严格 normal 5 分钟窗口")
            if not events.loc[events["event_name"] == "session_end"].shape[0]:
                notes.append("缺少 session_end 事件")
            summary_rows.append({
                "source_file": path.name,
                "source_session": source_key,
                "subset": subset,
                "environment_note": ENVIRONMENT_NOTE[path.name],
                "header_count": source.header_count,
                "malformed_row_count": len(source.malformed_rows),
                "data_rows": len(data),
                "valid_sensor_rows": len(data),
                "sampling_gap_count": gap_count,
                "strict_normal_window_count": strict_count,
                "normal_samples": normal_count,
                "warning_samples": warning_count,
                "notes": "；".join(notes) if notes else "通过",
            })

    manifest = pd.DataFrame(manifest_rows)
    source_summary = pd.DataFrame(summary_rows)
    validate_outputs(manifest)
    manifest.to_csv(OUTPUT / "manifest.csv", index=False, encoding="utf-8-sig", lineterminator="\n")
    source_summary.to_csv(OUTPUT / "source_summary.csv", index=False, encoding="utf-8-sig", lineterminator="\n")
    write_readme(manifest, source_summary)

    print(manifest.groupby(["subset", "label"]).size().unstack(fill_value=0).to_string())
    print("\nSource summary:")
    print(source_summary[["source_file", "subset", "strict_normal_window_count", "normal_samples", "warning_samples", "notes"]].to_string(index=False))
    print(f"\nOutput: {OUTPUT}")


if __name__ == "__main__":
    main()
