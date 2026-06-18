#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从 git 中的 bm_config.h 重新生成 bm_config_app.h（UTF-8，无 BOM）。"""

import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

HEADER_TEMPLATE = """/**
 * @file bm_config_app.h
 * @brief {brief}
 * @author zeh (china_qzh@163.com)
 * @version {version}
 * @date {date}
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * {log_line}
 *
 */
/* 由 CMake -include 强制预包含；勿定义 BM_CONFIG_H */

{defines}
"""


def git_show(path: str) -> str:
    return subprocess.check_output(
        ["git", "show", f"HEAD:{path}"], stderr=subprocess.DEVNULL, cwd=ROOT
    ).decode("utf-8")


def extract_defines(text: str) -> str:
    lines = []
    for line in text.splitlines():
        s = line.strip()
        if s in (
            "#ifndef BM_CONFIG_H",
            "#define BM_CONFIG_H",
            "#endif /* BM_CONFIG_H */",
        ) or s == "#endif":
            continue
        if s.startswith("#define") or s.startswith("#if"):
            lines.append(line.rstrip())
    body = "\n".join(lines).rstrip()
    return (body + "\n") if body else ""


def parse_meta(old: str, demo: str) -> dict:
    brief_m = re.search(r"@brief\s+(.+)", old)
    version_m = re.search(r"@version\s+([\d.]+)", old)
    date_m = re.search(r"@date\s+([\d-]+)", old)
    log_m = re.search(r"(\d{4}-\d{2}-\d{2})\s+([\d.]+)\s+\S+\s+(.+)", old)
    brief = brief_m.group(1).strip() if brief_m else f"{demo} 示例运行时容量配置"
    version = version_m.group(1) if version_m else "1.0"
    date = date_m.group(1) if date_m else "2026-06-10"
    if log_m:
        log_line = (
            f" {log_m.group(1)}       {log_m.group(2)}            "
            f"zeh            {log_m.group(3).strip()}"
        )
    else:
        log_line = f" {date}       {version}            zeh            正式发布"
    return {
        "brief": brief,
        "version": version,
        "date": date,
        "log_line": log_line,
    }


def write_app(path: Path, content: str) -> None:
    """写入 UTF-8（带 BOM），便于 Windows 编辑器正确识别中文。"""
    path.write_bytes(b"\xef\xbb\xbf" + content.encode("utf-8"))


def main() -> int:
    os.chdir(ROOT)
    count = 0
    demos = sorted(
        d.name
        for d in (ROOT / "Demo").iterdir()
        if d.is_dir() and (d / "bm_config_app.h").exists()
    )
    for demo in demos:
        old_path = f"Demo/{demo}/bm_config.h"
        try:
            old = git_show(old_path)
        except subprocess.CalledProcessError:
            print(f"skip (no git): {old_path}", file=sys.stderr)
            continue
        meta = parse_meta(old, demo)
        defines = extract_defines(old)
        content = HEADER_TEMPLATE.format(defines=defines, **meta)
        write_app(ROOT / "Demo" / demo / "bm_config_app.h", content)
        count += 1

    old = git_show("tests/qemu/bm_config.h")
    defines = extract_defines(old)
    qemu_header = """/**
 * @file bm_config_app.h
 * @brief QEMU 测试专用 bm_config 覆盖：事件队列与 HRT 参数
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 */

/* 由 CMake -include 强制预包含；勿定义 BM_CONFIG_H */

"""
    write_app(ROOT / "tests/qemu/bm_config_app.h", qemu_header + defines)
    count += 1
    print(f"regenerated {count} bm_config_app.h files (UTF-8 LF)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
