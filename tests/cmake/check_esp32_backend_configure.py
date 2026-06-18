#!/usr/bin/env python3
"""
验证 BM_BACKEND=sdk_esp32_idf 能解析到 pack（非 Unknown backend）。

无 IDF_PATH 时应因 SDK 依赖缺失而失败；有 IDF_PATH 但无 Xtensa 工具链时可能后续编译失败。
本脚本仅检查 configure 阶段错误信息是否符合预期。
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    if os.name == "nt":
        print("SKIP: Visual Studio generator is not a supported sdk_esp32_idf smoke path on Windows")
        return 0

    if os.environ.get("IDF_PATH"):
        print("SKIP: IDF_PATH set — full ESP32 configure left to manual/CI with toolchain")
        return 0

    build_dir = Path(tempfile.mkdtemp(prefix="bm_esp32_cfg_"))
    cmd = [
        "cmake",
        "-S",
        str(ROOT),
        "-B",
        str(build_dir),
        "-DBM_BUILD_TESTS=OFF",
        "-DBM_BACKEND=sdk_esp32_idf",
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    combined = (proc.stdout or "") + (proc.stderr or "")

    if "Unknown BM_BACKEND" in combined or "no pack or legacy dir" in combined:
        print("FAIL: pack not resolved for sdk_esp32_idf", file=sys.stderr)
        print(combined, file=sys.stderr)
        return 1

    if "IDF_PATH is required" not in combined:
        print("FAIL: expected IDF_PATH missing error, got:", file=sys.stderr)
        print(combined, file=sys.stderr)
        return 1

    print("PASS: sdk_esp32_idf resolves to pack; IDF_PATH guard works")
    return 0


if __name__ == "__main__":
    sys.exit(main())
