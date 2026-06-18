#!/usr/bin/env python3
"""Scan float bm_algo_*_step APIs and report missing Q15/Q31 variants in bm_algo_fixed.h."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ALGO_DIR = ROOT / "include" / "bm" / "algorithm"
FIXED_H = ALGO_DIR / "bm_algo_fixed.h"

STEP_RE = re.compile(
    r"\bbm_algo_([a-z0-9_]+)_step\s*\(",
    re.MULTILINE,
)
FIXED_VARIANT_RE = re.compile(
    r"\bbm_algo_([a-z0-9_]+)_(q15|q31)_step\s*\(",
    re.MULTILINE,
)


def collect_float_steps() -> set[str]:
    steps: set[str] = set()
    for path in sorted(ALGO_DIR.glob("*.h")):
        if path.name == "bm_algo_fixed.h":
            continue
        text = path.read_text(encoding="utf-8")
        for match in STEP_RE.finditer(text):
            steps.add(match.group(1))
    return steps


def collect_fixed_variants() -> dict[str, set[str]]:
    text = FIXED_H.read_text(encoding="utf-8")
    variants: dict[str, set[str]] = {}
    for match in FIXED_VARIANT_RE.finditer(text):
        base = match.group(1)
        fmt = match.group(2)
        variants.setdefault(base, set()).add(fmt)
    return variants


def main() -> int:
    if not FIXED_H.is_file():
        print(f"error: missing {FIXED_H}", file=sys.stderr)
        return 1

    float_steps = collect_float_steps()
    fixed = collect_fixed_variants()
    missing: list[tuple[str, str]] = []

    for base in sorted(float_steps):
        have = fixed.get(base, set())
        for fmt in ("q15", "q31"):
            if fmt not in have:
                missing.append((base, fmt))

    print(f"float _step APIs scanned: {len(float_steps)}")
    print(f"fixed variants in bm_algo_fixed.h: {sum(len(v) for v in fixed.values())}")
    if not missing:
        print("No gaps: every float _step has both Q15 and Q31 variants.")
    else:
        print(f"Missing fixed-point variants ({len(missing)}):")
        for base, fmt in missing:
            print(f"  - bm_algo_{base}_{fmt}_step")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
