#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
from pathlib import Path


FAIR_LABELS = {
    "BM_xproc_shm_slot": "xproc",
    "BM_qt_shm_slot": "Qt",
    "BM_poco_shm_slot": "Poco",
}

NATIVE_LABELS = {
    "BM_xproc_native_fixed": "xproc fixed",
    "BM_xproc_native_varlen": "xproc varlen",
}


def classify_benchmark(name: str) -> tuple[str, str, int] | None:
    if "/" not in name:
      return None

    family, payload = name.split("/", 1)
    if not payload.isdigit():
      return None

    size = int(payload)
    if family in FAIR_LABELS:
      return ("fair", FAIR_LABELS[family], size)
    if family in NATIVE_LABELS:
      return ("native", NATIVE_LABELS[family], size)
    return None


def format_latency(entry: dict[str, object]) -> str:
    real_time = entry.get("real_time")
    time_unit = entry.get("time_unit", "ns")
    if not isinstance(real_time, (int, float)):
        return "n/a"
    return f"{real_time:.3f} {time_unit}"


def render_table(title: str, rows: list[tuple[str, int, str]]) -> str:
    lines = [
        title,
        "^" * len(title),
        "",
        ".. list-table::",
        "   :header-rows: 1",
        "   :widths: 24 14 18",
        "",
        "   * - Framework",
        "     - Payload",
        "     - Latency",
    ]

    if rows:
        for label, payload, latency in rows:
            lines.extend(
                [
                    f"   * - {label}",
                    f"     - {payload} B",
                    f"     - {latency}",
                ]
            )
    else:
        lines.extend(
            [
                "   * - No data",
                "     - n/a",
                "     - This benchmark family was not built in the current run.",
            ]
        )

    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate an RST table from IPC benchmark JSON output.")
    parser.add_argument("--input", required=True, help="Path to Google Benchmark JSON output.")
    parser.add_argument("--output", required=True, help="Path to write the generated RST fragment.")
    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)

    payload = json.loads(input_path.read_text())
    entries = payload.get("benchmarks")
    if not isinstance(entries, list):
        raise SystemExit("Input JSON does not contain a 'benchmarks' list.")

    fair_rows: list[tuple[str, int, str]] = []
    native_rows: list[tuple[str, int, str]] = []

    for entry in entries:
        if not isinstance(entry, dict):
            continue
        classified = classify_benchmark(str(entry.get("name", "")))
        if classified is None:
            continue

        family, label, payload_size = classified
        row = (label, payload_size, format_latency(entry))
        if family == "fair":
            fair_rows.append(row)
        else:
            native_rows.append(row)

    fair_rows.sort(key=lambda item: (item[1], item[0]))
    native_rows.sort(key=lambda item: (item[1], item[0]))

    lines = [
        "Generated Results",
        "-----------------",
        "",
        ".. note::",
        "",
        "   The table below is generated from the most recent local benchmark run.",
        "   Missing frameworks mean the corresponding optional dependency was not",
        "   enabled or not available when the benchmark executable was built.",
        "",
        render_table("Fair Shared-Memory Baseline", fair_rows),
        "",
        render_table("xproc Native Channel", native_rows),
        "",
    ]

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
