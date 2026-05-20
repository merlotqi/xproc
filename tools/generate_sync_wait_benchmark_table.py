#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
from pathlib import Path


BENCH_LABELS = {
    "BM_AtomicWaitThreadPingPong": "Thread ping-pong (same process)",
    "BM_AtomicWaitCrossProcessPingPong": "Cross-process ping-pong",
}

COUNTERS = [
    ("win32_wait_calls_per_op", "wait calls/op"),
    ("win32_native_wait_calls_per_op", "native wait calls/op"),
    ("win32_native_timeout_per_op", "native timeout/op"),
    ("win32_spin_iterations_per_op", "spin iterations/op"),
    ("win32_yield_iterations_per_op", "yield iterations/op"),
]


def normalize_name(full_name: str) -> str:
    if "/" in full_name:
        return full_name.split("/", 1)[0]
    return full_name


def format_counter(value: object) -> str:
    if not isinstance(value, (int, float)):
        return "n/a"
    num = float(value)
    if num == 0.0:
        return "0"
    if abs(num) < 1e-3:
        return f"{num:.3e}"
    return f"{num:.3f}"


def build_rows(payload: dict[str, object]) -> list[tuple[str, str, str]]:
    entries = payload.get("benchmarks")
    if not isinstance(entries, list):
        return []

    rows: list[tuple[str, str, str]] = []
    for entry in entries:
        if not isinstance(entry, dict):
            continue

        raw_name = entry.get("name")
        if not isinstance(raw_name, str):
            continue

        base_name = normalize_name(raw_name)
        if base_name not in BENCH_LABELS:
            continue

        for key, display in COUNTERS:
            rows.append((BENCH_LABELS[base_name], display, format_counter(entry.get(key))))

    return rows


def render_table(rows: list[tuple[str, str, str]]) -> str:
    lines = [
        "Generated Results",
        "-----------------",
        "",
        ".. list-table::",
        "   :header-rows: 1",
        "   :widths: 34 26 16",
        "",
        "   * - Scenario",
        "     - Metric",
        "     - Value",
    ]

    if not rows:
        lines.extend(
            [
                "   * - No data",
                "     - n/a",
                "     - Sync benchmark JSON did not include Win32 wait counters.",
            ]
        )
        return "\n".join(lines)

    for scenario, metric, value in rows:
        lines.extend(
            [
                f"   * - {scenario}",
                f"     - {metric}",
                f"     - {value}",
            ]
        )

    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate RST table from sync benchmark Win32 wait counters.")
    parser.add_argument("--input", required=True, help="Path to Google Benchmark JSON output.")
    parser.add_argument("--output", required=True, help="Path to write generated RST fragment.")
    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)

    payload = json.loads(input_path.read_text())
    rows = build_rows(payload)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(render_table(rows) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
