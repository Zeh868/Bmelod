#!/usr/bin/env python3
"""QEMU simulation cycle regression helper (not hardware WCET)."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


def parse_qemu_cycles(output: str) -> int | None:
    match = re.search(r"cycles:\s*(\d+)", output, re.IGNORECASE)
    if match:
        return int(match.group(1))
    match = re.search(r"instruction count:\s*(\d+)", output, re.IGNORECASE)
    if match:
        return int(match.group(1))
    return None


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Measure repeatable QEMU simulation cycles for regression."
    )
    parser.add_argument("command", nargs=argparse.REMAINDER,
                        help="QEMU command to execute")
    parser.add_argument("--label", default="qemu-run",
                        help="Metric label written to the report")
    parser.add_argument("--output", type=Path,
                        help="Optional JSON report path")
    args = parser.parse_args()

    if not args.command:
        parser.error("missing QEMU command")

    completed = subprocess.run(args.command, capture_output=True, text=True)
    combined = completed.stdout + completed.stderr
    cycles = parse_qemu_cycles(combined)

    report = {
        "label": args.label,
        "kind": "qemu_simulation_cycles",
        "hardware_wcet": False,
        "exit_code": completed.returncode,
        "cycles": cycles,
    }

    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(json.dumps(report, indent=2))
    if completed.returncode != 0:
        return completed.returncode
    if cycles is None:
        print("warning: no cycle count found in QEMU output", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
