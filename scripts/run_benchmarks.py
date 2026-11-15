#!/usr/bin/env python3
"""
Benchmark runner for the external sorting program (`ordena`).

Executes `./build/ordena <method> <quantity> <situation>` across the requested
algorithms, input types (situations), and record quantities, enforcing a
per-execution timeout, collecting metrics, computing percentiles (p50, p75, p99),
and writing the aggregated results to a CSV file.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

ALGORITHM_NAMES = {
    1: "2f_fitas",
    2: "f_plus_1_fitas",
    3: "quicksort_externo",
}

SITUATION_NAMES = {
    1: "Ascendente",
    2: "Descendente",
    3: "Desordenado",
}

DEFAULT_QUANTITIES = [100, 200, 2000, 20000, 200000, 471705]

CSV_FIELDS = [
    "algorithm",
    "algorithm_name",
    "situation",
    "situation_name",
    "quantity",
    "status",
    "runs_completed",
    "runs_requested",
    "timeout_s",
    "phase1_time_p50_ms",
    "phase1_time_p75_ms",
    "phase1_time_p99_ms",
    "phase1_reads_p50",
    "phase1_writes_p50",
    "phase2_time_p50_ms",
    "phase2_time_p75_ms",
    "phase2_time_p99_ms",
    "phase2_reads_p50",
    "comparisons_p50",
]


def parse_int_selection(raw: str, valid_values: set[int], label: str) -> list[int]:
    """Parse 'all' or a comma-separated list of integers into a sorted unique list."""
    cleaned = raw.strip().lower()
    if cleaned in {"all", "todos", "*"}:
        return sorted(valid_values)

    result: list[int] = []
    for token in raw.split(","):
        token = token.strip()
        if not token:
            continue
        try:
            value = int(token)
        except ValueError as exc:
            raise ValueError(f"Invalid {label} value '{token}': expected integer or 'all'") from exc
        if value not in valid_values:
            raise ValueError(
                f"Invalid {label} value '{value}': allowed values are {sorted(valid_values)}"
            )
        if value not in result:
            result.append(value)

    if not result:
        raise ValueError(f"At least one {label} must be specified")
    return result


def parse_quantities(raw: str) -> list[int]:
    """Parse a comma-separated list of positive integer quantities."""
    result: list[int] = []
    for token in raw.split(","):
        token = token.strip().replace(".", "").replace("_", "")
        if not token:
            continue
        try:
            value = int(token)
        except ValueError as exc:
            raise ValueError(f"Invalid quantity '{token}': expected positive integer") from exc
        if value <= 0:
            raise ValueError(f"Quantity must be > 0, got {value}")
        if value not in result:
            result.append(value)

    if not result:
        raise ValueError("At least one quantity must be specified")
    return result


def percentile(values: list[float], p: float) -> float | None:
    """Compute linear-interpolated percentile p in [0, 100]."""
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    rank = (p / 100.0) * (len(ordered) - 1)
    lower = math.floor(rank)
    upper = math.ceil(rank)
    if lower == upper:
        return ordered[lower]
    fraction = rank - lower
    return ordered[lower] + fraction * (ordered[upper] - ordered[lower])


def _extract_number(text: str, patterns: list[str]) -> float | None:
    """Return the first numeric match among a list of regex patterns."""
    for pattern in patterns:
        match = re.search(pattern, text, flags=re.IGNORECASE | re.MULTILINE)
        if match:
            try:
                return float(match.group(1))
            except ValueError:
                continue
    return None


def parse_program_output(output: str, elapsed_ms: float) -> dict[str, float | None]:
    """
    Extract benchmark metrics from `ordena` stdout/stderr.

    Supports:
    1. A JSON object line (e.g., {"phase1_time_ms": 1.2, ...}).
    2. Key-value or labeled text lines (e.g., `phase1_time_ms=1.2`,
       `Phase 1 Reads: 100`, `Comparisons: 450`, etc.).
    Falls back to wall-clock `elapsed_ms` for phase2/total time if not printed.
    """
    for line in output.splitlines():
        stripped = line.strip()
        if stripped.startswith("{") and stripped.endswith("}"):
            try:
                data = json.loads(stripped)
                if isinstance(data, dict):
                    return {
                        "phase1_time_ms": _to_float(data.get("phase1_time_ms")),
                        "phase1_reads": _to_float(data.get("phase1_reads")),
                        "phase1_writes": _to_float(data.get("phase1_writes")),
                        "phase2_time_ms": _to_float(data.get("phase2_time_ms")) or elapsed_ms,
                        "phase2_reads": _to_float(data.get("phase2_reads")),
                        "comparisons": _to_float(data.get("comparisons")),
                    }
            except json.JSONDecodeError:
                pass

    p1_time = _extract_number(
        output,
        [
            r"phase1_time_ms\s*[:=]\s*([0-9]+(?:\.[0-9]+)?)",
            r"(?:generation|partition|prep|phase\s*1)\s*time(?:\s*\(ms\))?\s*[:=]\s*([0-9]+(?:\.[0-9]+)?)",
        ],
    )
    p1_reads = _extract_number(
        output,
        [
            r"phase1_reads\s*[:=]\s*([0-9]+)",
            r"(?:generation|partition|prep|phase\s*1)\s*(?:disk\s*)?reads?\s*[:=]\s*([0-9]+)",
        ],
    )
    p1_writes = _extract_number(
        output,
        [
            r"phase1_writes\s*[:=]\s*([0-9]+)",
            r"(?:generation|partition|prep|phase\s*1)\s*(?:disk\s*)?writes?\s*[:=]\s*([0-9]+)",
            r"(?:disk\s*)?writes?\s*[:=]\s*([0-9]+)",
        ],
    )
    p2_time = _extract_number(
        output,
        [
            r"phase2_time_ms\s*[:=]\s*([0-9]+(?:\.[0-9]+)?)",
            r"(?:merge|recursion|total|phase\s*2)\s*time(?:\s*\(ms\))?\s*[:=]\s*([0-9]+(?:\.[0-9]+)?)",
            r"time(?:\s*\(ms\))?\s*[:=]\s*([0-9]+(?:\.[0-9]+)?)",
        ],
    )
    p2_reads = _extract_number(
        output,
        [
            r"phase2_reads\s*[:=]\s*([0-9]+)",
            r"(?:merge|recursion|phase\s*2)\s*(?:disk\s*)?reads?\s*[:=]\s*([0-9]+)",
            r"(?:disk\s*)?reads?\s*[:=]\s*([0-9]+)",
        ],
    )
    comparisons = _extract_number(
        output,
        [
            r"comparisons\s*[:=]\s*([0-9]+)",
            r"(?:key\s*)?comparisons?\s*[:=]\s*([0-9]+)",
        ],
    )

    return {
        "phase1_time_ms": p1_time,
        "phase1_reads": p1_reads,
        "phase1_writes": p1_writes,
        "phase2_time_ms": p2_time if p2_time is not None else elapsed_ms,
        "phase2_reads": p2_reads,
        "comparisons": comparisons,
    }


def _to_float(val: Any) -> float | None:
    if val is None:
        return None
    try:
        return float(val)
    except (TypeError, ValueError):
        return None


def fmt_float(val: float | None) -> str:
    return f"{val:.3f}" if val is not None else ""


def fmt_int(val: float | None) -> str:
    return str(round(val)) if val is not None else ""


def run_single_benchmark(
    binary: Path,
    algorithm: int,
    situation: int,
    quantity: int,
    runs: int,
    timeout_s: float,
    extra_args: list[str],
) -> dict[str, Any]:
    """Run all repetitions for a single (algorithm, situation, quantity) tuple."""
    p1_times: list[float] = []
    p1_reads: list[float] = []
    p1_writes: list[float] = []
    p2_times: list[float] = []
    p2_reads: list[float] = []
    comparisons: list[float] = []

    status = "ok"
    runs_completed = 0

    for run_idx in range(1, runs + 1):
        cmd = [str(binary), str(algorithm), str(quantity), str(situation), *extra_args]
        start = time.perf_counter()
        try:
            proc = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=timeout_s,
                check=False,
            )
        except subprocess.TimeoutExpired:
            status = "timeout"
            print(
                f"  [TIMEOUT] alg={algorithm} sit={situation} n={quantity} "
                f"(run {run_idx}/{runs} exceeded {timeout_s}s)",
                file=sys.stderr,
            )
            break

        elapsed_ms = (time.perf_counter() - start) * 1000.0
        combined_output = f"{proc.stdout}\n{proc.stderr}"

        if proc.returncode != 0:
            status = "error"
            print(
                f"  [ERROR] alg={algorithm} sit={situation} n={quantity} "
                f"exit={proc.returncode}: {combined_output.strip().splitlines()[-1] if combined_output.strip() else 'no output'}",
                file=sys.stderr,
            )
            break

        metrics = parse_program_output(combined_output, elapsed_ms)
        if metrics["phase1_time_ms"] is not None:
            p1_times.append(metrics["phase1_time_ms"])
        if metrics["phase1_reads"] is not None:
            p1_reads.append(metrics["phase1_reads"])
        if metrics["phase1_writes"] is not None:
            p1_writes.append(metrics["phase1_writes"])
        if metrics["phase2_time_ms"] is not None:
            p2_times.append(metrics["phase2_time_ms"])
        if metrics["phase2_reads"] is not None:
            p2_reads.append(metrics["phase2_reads"])
        if metrics["comparisons"] is not None:
            comparisons.append(metrics["comparisons"])

        runs_completed += 1

    return {
        "algorithm": algorithm,
        "algorithm_name": ALGORITHM_NAMES[algorithm],
        "situation": situation,
        "situation_name": SITUATION_NAMES[situation],
        "quantity": quantity,
        "status": status,
        "runs_completed": runs_completed,
        "runs_requested": runs,
        "timeout_s": timeout_s,
        "phase1_time_p50_ms": fmt_float(percentile(p1_times, 50)),
        "phase1_time_p75_ms": fmt_float(percentile(p1_times, 75)),
        "phase1_time_p99_ms": fmt_float(percentile(p1_times, 99)),
        "phase1_reads_p50": fmt_int(percentile(p1_reads, 50)),
        "phase1_writes_p50": fmt_int(percentile(p1_writes, 50)),
        "phase2_time_p50_ms": fmt_float(percentile(p2_times, 50)),
        "phase2_time_p75_ms": fmt_float(percentile(p2_times, 75)),
        "phase2_time_p99_ms": fmt_float(percentile(p2_times, 99)),
        "phase2_reads_p50": fmt_int(percentile(p2_reads, 50)),
        "comparisons_p50": fmt_int(percentile(comparisons, 50)),
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run external sorting benchmarks and export results to CSV."
    )
    parser.add_argument(
        "--binary",
        type=Path,
        default=Path("./build/ordena"),
        help="Path to the compiled `ordena` executable (default: ./build/ordena)",
    )
    parser.add_argument(
        "--algorithms",
        default="all",
        help="Comma-separated algorithm IDs (1,2,3) or 'all' (default: all)",
    )
    parser.add_argument(
        "--situations",
        default="all",
        help="Comma-separated situation IDs (1,2,3) or 'all' (default: all)",
    )
    parser.add_argument(
        "--quantities",
        default=",".join(str(q) for q in DEFAULT_QUANTITIES),
        help="Comma-separated record quantities (default: 100,200,2000,20000,200000,471705)",
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=5,
        help="Number of repetitions per configuration (default: 5)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=60.0,
        help="Timeout in seconds per program execution (default: 60)",
    )
    parser.add_argument(
        "--output-csv",
        type=Path,
        default=Path("results/benchmark.csv"),
        help="Output CSV path (default: results/benchmark.csv)",
    )
    parser.add_argument(
        "--extra-args",
        nargs="*",
        default=[],
        help="Additional arguments passed to `ordena`",
    )
    args = parser.parse_args()

    if not args.binary.exists():
        print(f"Error: binary not found at '{args.binary}'", file=sys.stderr)
        return 1
    if args.runs <= 0:
        print("Error: --runs must be > 0", file=sys.stderr)
        return 1
    if args.timeout <= 0:
        print("Error: --timeout must be > 0", file=sys.stderr)
        return 1

    try:
        algorithms = parse_int_selection(args.algorithms, set(ALGORITHM_NAMES), "algorithm")
        situations = parse_int_selection(args.situations, set(SITUATION_NAMES), "situation")
        quantities = parse_quantities(args.quantities)
    except ValueError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    rows: list[dict[str, Any]] = []
    total = len(algorithms) * len(situations) * len(quantities)
    current = 0

    for alg in algorithms:
        for sit in situations:
            for qty in quantities:
                current += 1
                print(
                    f"[{current}/{total}] Running alg={alg} ({ALGORITHM_NAMES[alg]}), "
                    f"sit={sit} ({SITUATION_NAMES[sit]}), n={qty}, "
                    f"runs={args.runs}, timeout={args.timeout}s"
                )
                row = run_single_benchmark(
                    binary=args.binary,
                    algorithm=alg,
                    situation=sit,
                    quantity=qty,
                    runs=args.runs,
                    timeout_s=args.timeout,
                    extra_args=args.extra_args,
                )
                rows.append(row)

    args.output_csv.parent.mkdir(parents=True, exist_ok=True)
    with args.output_csv.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)

    print(f"CSV saved to {args.output_csv} ({len(rows)} rows)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
