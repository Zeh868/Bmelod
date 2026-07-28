#!/usr/bin/env python3
"""拒绝 component 层公开头与实现之间的具体组件互含。"""

from __future__ import annotations

import re
import sys
import tempfile
from pathlib import Path


INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^">]+)[">]')
COMMON_HEADER = "bm_component_common.h"


def include_targets(path: Path) -> list[tuple[int, str, bool]]:
    """返回 include 的目标文本、行号及是否为引号形式。"""
    result: list[tuple[int, str, bool]] = []
    for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        match = INCLUDE_RE.match(line)
        if match is not None:
            result.append((line_no, match.group(2).replace("\\", "/"),
                           match.group(1) == '"'))
    return result


def is_component_header(target: str, quoted: bool, include_root: Path) -> bool:
    """判断目标是否以公开 component 路径或相对头形式引用组件头。"""
    return target.startswith("bm/component/") or (quoted and "/" not in target and
                                                   (include_root / target).is_file())


def basename(target: str) -> str:
    """返回 include 目标的文件名。"""
    return target.rsplit("/", 1)[-1]


def scan(include_root: Path, source_root: Path) -> list[str]:
    """扫描公开头和实现，返回所有组件互含违规。"""
    violations: list[str] = []
    for path in sorted(include_root.glob("*.h")):
        for line_no, target, quoted in include_targets(path):
            if (is_component_header(target, quoted, include_root) and
                    basename(target) != COMMON_HEADER):
                violations.append(f"{path}:{line_no}: component 公开头不得包含具体组件头 {target}")

    for path in sorted(source_root.glob("*.c")):
        own_header = f"{path.stem}.h"
        for line_no, target, quoted in include_targets(path):
            if (is_component_header(target, quoted, include_root) and
                    basename(target) not in (own_header, COMMON_HEADER)):
                violations.append(f"{path}:{line_no}: 实现仅可包含自身公开头或 "
                                  f"{COMMON_HEADER}，实际为 {target}")
    return violations


def self_test() -> int:
    """验证正常自身 include 放行、相对路径互含与绝对路径互含均被拒绝。"""
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        include_root = root / "include" / "bm" / "component"
        source_root = root / "Source" / "component"
        include_root.mkdir(parents=True)
        source_root.mkdir(parents=True)
        (include_root / "ok.h").write_text(
            '#include "bm/component/bm_component_common.h"\n', encoding="utf-8")
        (source_root / "ok.c").write_text('#include "ok.h"\n', encoding="utf-8")
        if scan(include_root, source_root):
            return 1
        (include_root / "other.h").write_text("\n", encoding="utf-8")
        (include_root / "bad.h").write_text('#include "other.h"\n', encoding="utf-8")
        (include_root / "bad_absolute.h").write_text(
            '#include "bm/component/other.h"\n', encoding="utf-8")
        (source_root / "bad.c").write_text('#include "other.h"\n', encoding="utf-8")
        return 0 if len(scan(include_root, source_root)) == 3 else 1


def main() -> int:
    """执行仓库扫描，或执行相对 include 防绕过自测。"""
    if "--self-test" in sys.argv:
        if self_test() == 0:
            print("Component include isolation self-test passed.")
            return 0
        print("Component include isolation self-test failed.")
        return 1

    root = Path(__file__).resolve().parents[1]
    violations = scan(root / "include" / "bm" / "component",
                      root / "Source" / "component")
    if violations:
        print("Component include isolation check failed:")
        print("\n".join(violations))
        return 1
    print("Component include isolation check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
