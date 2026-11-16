#!/usr/bin/env python3
"""
Generate `provao_unordered`, `provao_ascending`, and `provao_descending` in
`tmp/` from an existing `PROVAO.TXT` dataset.

Situation 3 (`provao_unordered`) preserves the original order of `PROVAO.TXT`,
as specified by the assignment.
"""

from __future__ import annotations

import argparse
from pathlib import Path

DEFAULT_INPUT_CANDIDATES = (
    Path("PROVAO.TXT"),
    Path("PROVAO.txt"),
    Path("/home/luiz/Downloads/ProvaoE/PROVAO.TXT"),
)


def resolve_default_input() -> Path:
    """Return the first existing default PROVAO.TXT path."""
    for candidate in DEFAULT_INPUT_CANDIDATES:
        if candidate.is_file():
            return candidate
    return DEFAULT_INPUT_CANDIDATES[-1]


def load_records(input_path: Path) -> list[tuple[float, int, bytes]]:
    """
    Read non-empty lines from `input_path` and extract `(grade, reg, line_bytes)`.

    Preserves the exact bytes of each record (normalizing CRLF to LF) so
    fixed-width fields and character encoding remain intact.
    """
    if not input_path.is_file():
        raise FileNotFoundError(f"Input file not found: {input_path}")

    records: list[tuple[float, int, bytes]] = []
    with input_path.open("rb") as handle:
        for raw_line in handle:
            line = raw_line.rstrip(b"\r\n")
            if not line.strip():
                continue
            reg = int(line[0:8])
            grade = float(line[9:14])
            records.append((grade, reg, line + b"\n"))

    return records


def write_dataset(path: Path, lines: list[bytes]) -> None:
    """Write record lines to `path`."""
    with path.open("wb") as handle:
        handle.writelines(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate provao_unordered, provao_ascending, and provao_descending from PROVAO.TXT."
    )
    parser.add_argument(
        "-i",
        "--input",
        type=Path,
        default=None,
        help="Path to PROVAO.TXT (auto-detected if omitted).",
    )
    parser.add_argument(
        "-o",
        "--output-dir",
        type=Path,
        default=Path("tmp"),
        help="Output directory for generated files (default: tmp).",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    input_path = args.input if args.input is not None else resolve_default_input()

    print(f"Loading records from {input_path}...")
    records = load_records(input_path)
    print(f"Loaded {len(records)} records.")

    args.output_dir.mkdir(parents=True, exist_ok=True)

    # Situação 3: ordem original desordenada aleatoriamente de PROVAO.TXT.
    unordered_path = args.output_dir / "provao_unordered"
    write_dataset(unordered_path, [rec[2] for rec in records])
    print(f"Wrote {unordered_path}")

    records.sort(key=lambda rec: (rec[0], rec[1]))
    ascending_path = args.output_dir / "provao_ascending"
    write_dataset(ascending_path, [rec[2] for rec in records])
    print(f"Wrote {ascending_path}")

    records.reverse()
    descending_path = args.output_dir / "provao_descending"
    write_dataset(descending_path, [rec[2] for rec in records])
    print(f"Wrote {descending_path}")


if __name__ == "__main__":
    main()
