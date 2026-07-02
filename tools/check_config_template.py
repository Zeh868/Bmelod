#!/usr/bin/env python3
"""tools/check_config_template.py — bm_config.h.template 防漂移校验

背景：bm_config.h.template 是发给应用方的完整配置模板，理应覆盖
include/bm_config.h 中定义的每一个 BM_CONFIG_* 旋钮；否则应用方复制模板
后会静默缺失框架新增的编译期宏（其行为退化为“未定义 → 预处理器按 0 处理”，
不会报错但会跑出与预期不符的容量/行为，且极难排查）。

本脚本解析两个文件里所有形如 `#define BM_CONFIG_XXX` 的顶层宏名，
比较真实文件（真源）与模板文件的集合：
  - 若真实文件存在而模板缺失 → 判定为漂移，非零退出并打印缺口列表；
  - 模板比真实文件多出的宏（例如刚从真实文件删除、模板还没跟进）仅提示，不算失败。

白名单 INTERNAL_ONLY_MACROS：仅用于框架内部派生 / include guard 等不属于
用户可调旋钮的宏名，允许模板不包含。新增此类宏时在此处登记并写明理由，
不要用它来掩盖真正遗漏的用户旋钮。
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

# Windows 控制台默认代码页非 UTF-8，中文输出会乱码；有 reconfigure 时尽量切换。
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        try:
            _stream.reconfigure(encoding="utf-8")
        except (ValueError, OSError):
            pass

ROOT = Path(__file__).resolve().parents[1]
REAL_CONFIG = ROOT / "include" / "bm_config.h"
TEMPLATE_CONFIG = ROOT / "bm_config.h.template"

# 顶层宏定义：`#define BM_CONFIG_XXX ...`（不要求前置 #ifndef，允许两种写法）
MACRO_RE = re.compile(r"^\s*#define\s+(BM_CONFIG_[A-Z0-9_]+)\b", re.MULTILINE)

# 内部派生宏 / include guard：不是用户旋钮，模板允许缺失。
# 登记格式： "宏名": "缺失原因"
INTERNAL_ONLY_MACROS: dict[str, str] = {
    "BM_CONFIG_H": "include guard，非配置旋钮",
}


def collect_macros(path: Path) -> set[str]:
    if not path.exists():
        print(f"错误：找不到文件 {path}", file=sys.stderr)
        sys.exit(2)
    text = path.read_text(encoding="utf-8")
    return set(MACRO_RE.findall(text))


def main() -> int:
    real_macros = collect_macros(REAL_CONFIG) - set(INTERNAL_ONLY_MACROS)
    template_macros = collect_macros(TEMPLATE_CONFIG)

    missing = sorted(real_macros - template_macros)
    extra = sorted(template_macros - real_macros)

    print("bm_config.h.template 防漂移校验")
    print("=" * 40)
    print(f"真实文件（{REAL_CONFIG.relative_to(ROOT)}）宏数：{len(real_macros)}")
    print(f"模板文件（{TEMPLATE_CONFIG.relative_to(ROOT)}）宏数：{len(template_macros)}")

    if extra:
        print(f"\n模板多出的宏（{len(extra)}，仅提示，不影响退出码）：")
        for name in extra:
            print(f"  - {name}")

    if missing:
        print(f"\n模板缺失的宏（{len(missing)}，须同步补齐）：")
        for name in missing:
            print(f"  - {name}")
        print("\n结论：FAIL — 模板落后于 include/bm_config.h，请同步后重跑本脚本。")
        return 1

    print("\n结论：PASS — 模板已覆盖真实文件的全部 BM_CONFIG_* 旋钮。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
