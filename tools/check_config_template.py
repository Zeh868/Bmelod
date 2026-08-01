#!/usr/bin/env python3
"""校验 bm_config.h.template 与公开默认配置的宏和派生语义。"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        try:
            _stream.reconfigure(encoding="utf-8")
        except (ValueError, OSError):
            pass

ROOT = Path(__file__).resolve().parents[1]
REAL_CONFIG = ROOT / "include" / "bm_config.h"
TEMPLATE_CONFIG = ROOT / "bm_config.h.template"
DEFINE_RE = re.compile(
    r"^\s*#\s*define\s+(BM_CONFIG_[A-Z0-9_]+)\b(.*)$", re.MULTILINE
)
BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)

INTERNAL_ONLY_MACROS = {"BM_CONFIG_H": "include guard，非配置旋钮"}

# 这些宏的默认值由其他旋钮派生。只比较宏名会让模板把表达式写成当前常量而
# 门禁仍然通过，因此必须校验规范化定义表达式。
DERIVED_DEFAULT_MACROS = {
    "BM_CONFIG_CONSOLE_CLI_BACKEND",
    "BM_CONFIG_HRT_DISPATCH_PER_ISR",
    "BM_CONFIG_MAX_SYNC_MEMBERS",
    "BM_CONFIG_IPC_DRAIN_BUDGET",
    "BM_CONFIG_PROFILE_STREAM_GATE_ENFORCED",
}


def _join_continuations(text: str) -> str:
    """合并预处理器反斜杠续行，保留宏定义的逻辑行。"""
    return re.sub(r"\\\s*\r?\n", " ", text)


def _strip_comments(text: str) -> str:
    text = BLOCK_COMMENT_RE.sub(" ", text)
    return re.sub(r"//[^\r\n]*", " ", text)


def collect_definitions_from_text(text: str) -> dict[str, list[str]]:
    """返回宏到规范化定义表达式列表的映射。"""
    logical = _strip_comments(_join_continuations(text))
    definitions: dict[str, list[str]] = {}
    for match in DEFINE_RE.finditer(logical):
        name = match.group(1)
        expression = re.sub(r"\s+", "", match.group(2))
        definitions.setdefault(name, []).append(expression)
    return definitions


def collect_definitions(path: Path) -> dict[str, list[str]]:
    if not path.exists():
        raise FileNotFoundError(path)
    return collect_definitions_from_text(path.read_text(encoding="utf-8"))


def compare_definitions(
    real: dict[str, list[str]],
    template: dict[str, list[str]],
    derived_macros: set[str] = DERIVED_DEFAULT_MACROS,
) -> tuple[list[str], list[str], list[tuple[str, str, str]]]:
    real_names = set(real) - set(INTERNAL_ONLY_MACROS)
    template_names = set(template)
    missing = sorted(real_names - template_names)
    extra = sorted(template_names - real_names)
    drift: list[tuple[str, str, str]] = []

    for name in sorted(derived_macros & real_names & template_names):
        real_values = sorted(set(real[name]))
        template_values = sorted(set(template[name]))
        if len(real_values) != 1 or len(template_values) != 1:
            drift.append((name, " | ".join(real_values), " | ".join(template_values)))
        elif real_values[0] != template_values[0]:
            drift.append((name, real_values[0], template_values[0]))
    return missing, extra, drift


def run_self_test() -> int:
    failures: list[str] = []

    def expect(label: str, condition: bool) -> None:
        if not condition:
            failures.append(label)

    real = collect_definitions_from_text(
        "#define BM_CONFIG_BASE 4u\n"
        "#define BM_CONFIG_DERIVED \\\n             (BM_CONFIG_BASE * 2u)\n"
    )
    equivalent = collect_definitions_from_text(
        "#define BM_CONFIG_BASE 4u /* 注释不同不影响语义 */\n"
        "#define BM_CONFIG_DERIVED ( BM_CONFIG_BASE*2u )\n"
    )
    missing_template = collect_definitions_from_text(
        "#define BM_CONFIG_DERIVED (BM_CONFIG_BASE * 2u)\n"
    )
    fixed_template = collect_definitions_from_text(
        "#define BM_CONFIG_BASE 4u\n#define BM_CONFIG_DERIVED 8u\n"
    )
    conditional_template = collect_definitions_from_text(
        "#define BM_CONFIG_BASE 4u\n"
        "#if BM_CONFIG_SWITCH\n"
        "#define BM_CONFIG_DERIVED BM_CONFIG_BASE\n"
        "#else\n"
        "#define BM_CONFIG_DERIVED 8u\n"
        "#endif\n"
    )

    missing, extra, drift = compare_definitions(
        real, equivalent, {"BM_CONFIG_DERIVED"}
    )
    expect("等价空白、续行和注释应通过", not missing and not extra and not drift)

    missing, _, _ = compare_definitions(
        real, missing_template, {"BM_CONFIG_DERIVED"}
    )
    expect("模板缺宏必须失败", missing == ["BM_CONFIG_BASE"])

    _, _, drift = compare_definitions(real, fixed_template, {"BM_CONFIG_DERIVED"})
    expect("派生表达式被固定常量替换必须失败", len(drift) == 1)

    _, _, drift = compare_definitions(
        real, conditional_template, {"BM_CONFIG_DERIVED"}
    )
    expect("未经建模的条件分支必须 fail-closed", len(drift) == 1)

    if failures:
        print("check_config_template 自测试：FAIL")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("check_config_template 自测试：PASS（4 组语义正反例）")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="运行内置正反例")
    args = parser.parse_args()
    if args.self_test:
        return run_self_test()

    try:
        real = collect_definitions(REAL_CONFIG)
        template = collect_definitions(TEMPLATE_CONFIG)
    except (FileNotFoundError, UnicodeError) as exc:
        print(f"错误：无法读取配置文件：{exc}", file=sys.stderr)
        return 2

    missing, extra, drift = compare_definitions(real, template)
    real_count = len(set(real) - set(INTERNAL_ONLY_MACROS))
    template_count = len(template)

    print("bm_config.h.template 防漂移校验")
    print("=" * 40)
    print(f"真实文件（{REAL_CONFIG.relative_to(ROOT)}）宏数：{real_count}")
    print(f"模板文件（{TEMPLATE_CONFIG.relative_to(ROOT)}）宏数：{template_count}")

    if extra:
        print(f"\n模板多出的宏（{len(extra)}，仅提示，不影响退出码）：")
        for name in extra:
            print(f"  - {name}")
    if missing:
        print(f"\n模板缺失的宏（{len(missing)}）：")
        for name in missing:
            print(f"  - {name}")
    if drift:
        print(f"\n派生默认表达式漂移（{len(drift)}）：")
        for name, real_value, template_value in drift:
            print(f"  - {name}")
            print(f"    真实：{real_value}")
            print(f"    模板：{template_value}")

    if missing or drift:
        print("\n结论：FAIL — 模板缺宏或派生默认语义与 include/bm_config.h 不一致。")
        return 1
    print("\n结论：PASS — 宏覆盖完整，派生默认表达式一致。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
