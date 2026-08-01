#!/usr/bin/env python3
"""比较 bare-metal 公开 API 的规范化函数声明与冻结签名。"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        try:
            _stream.reconfigure(encoding="utf-8")
        except (ValueError, OSError):
            pass

EXPECTED_APIS = {
    "bm_event_reset": "void bm_event_reset(void)",
    "bm_event_register_type": "int bm_event_register_type(bm_event_type_t type, const char *name)",
    "bm_event_subscribe": "int bm_event_subscribe(bm_event_type_t type, bm_event_callback_t cb, void *user_data, bm_event_subscriber_id_t *id)",
    "bm_event_unsubscribe": "int bm_event_unsubscribe(bm_event_type_t type, bm_event_subscriber_id_t id)",
    "bm_event_publish_copy": "int bm_event_publish_copy(bm_event_type_t type, bm_event_priority_t prio, const void *data, size_t len)",
    "bm_event_publish_copy_from_isr": "int bm_event_publish_copy_from_isr(bm_event_type_t type, bm_event_priority_t prio, const void *data, size_t len)",
    "bm_event_publish_event": "int bm_event_publish_event(const bm_event_t *event)",
    "bm_event_publish_event_from_isr": "int bm_event_publish_event_from_isr(const bm_event_t *event)",
    "bm_event_process": "int bm_event_process(uint32_t max_events)",
    "bm_mempool_alloc": "void *bm_mempool_alloc(bm_mempool_t *pool)",
    "bm_mempool_try_free": "int bm_mempool_try_free(bm_mempool_t *pool, void *obj)",
    "bm_mempool_free": "void bm_mempool_free(bm_mempool_t *pool, void *obj)",
    "bm_atomic_load": "uint32_t bm_atomic_load(bm_atomic_t *value)",
    "bm_atomic_store": "void bm_atomic_store(bm_atomic_t *value, uint32_t new_value)",
    "bm_atomic_inc": "uint32_t bm_atomic_inc(bm_atomic_t *value)",
    "bm_hal_critical_enter": "bm_irq_state_t bm_hal_critical_enter(void)",
    "bm_hal_critical_exit": "void bm_hal_critical_exit(bm_irq_state_t state)",
    "bm_module_init_all": "int bm_module_init_all(void)",
    "bm_module_start_all": "int bm_module_start_all(void)",
    "bm_module_stop_all": "int bm_module_stop_all(void)",
    "bm_module_deinit_all": "int bm_module_deinit_all(void)",
    "bm_wdg_register": "int bm_wdg_register(const char *name)",
    "bm_wdg_feed": "void bm_wdg_feed(void)",
    "bm_wdg_feed_module": "void bm_wdg_feed_module(const char *name)",
    "bm_shell_init": "void bm_shell_init(bm_shell_t *shell)",
    "bm_shell_register": "int bm_shell_register(bm_shell_t *shell, const char *name, bm_shell_cmd_fn_t fn, const char *help)",
    "bm_shell_feed": "void bm_shell_feed(bm_shell_t *shell, char c)",
    "bm_shell_poll": "void bm_shell_poll(bm_shell_t *shell)",
    "bm_shell_exec": "int bm_shell_exec(bm_shell_t *shell, char *line)",
    "bm_shell_puts": "void bm_shell_puts(const char *s)",
}

COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\r\n]*", re.DOTALL)


@dataclass(frozen=True)
class Declaration:
    signature: str
    path: str
    line: int
    source: str


def _compact_type(type_text: str) -> str:
    text = re.sub(r"\s+", " ", type_text.strip())
    text = re.sub(r"\s*\*\s*", "*", text)
    return text


def _split_parameters(parameters: str) -> list[str]:
    result: list[str] = []
    start = 0
    depth = 0
    for index, char in enumerate(parameters):
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        elif char == "," and depth == 0:
            result.append(parameters[start:index])
            start = index + 1
    result.append(parameters[start:])
    return result


def _parameter_type(parameter: str) -> str:
    text = parameter.strip()
    if text in {"", "void", "..."}:
        return text or "void"
    # 冻结清单当前只包含普通参数（回调均为 typedef），删除末尾参数名即可；
    # 数组/裸函数指针若未来进入清单必须扩展解析器并补自测，不能静默接受。
    match = re.match(r"^(.*?)([A-Za-z_]\w*)\s*$", text)
    if not match or not match.group(1).strip():
        return _compact_type(text)
    return _compact_type(match.group(1))


def normalize_signature(declaration: str, name: str) -> str:
    text = COMMENT_RE.sub(" ", declaration).strip().rstrip(";").strip()
    match = re.fullmatch(
        rf"(?P<return>.+?)\b{re.escape(name)}\s*\((?P<params>.*)\)\s*",
        text,
        re.DOTALL,
    )
    if not match:
        raise ValueError(f"无法解析 {name} 声明：{declaration!r}")
    return_type = _compact_type(match.group("return"))
    parameters = match.group("params").strip()
    if not parameters or parameters == "void":
        parameter_types = ["void"]
    else:
        parameter_types = [_parameter_type(item) for item in _split_parameters(parameters)]
    return f"{return_type} {name}({','.join(parameter_types)})"


def extract_declarations_from_text(text: str, name: str, path: str) -> list[Declaration]:
    clean = COMMENT_RE.sub(lambda match: "\n" * match.group(0).count("\n"), text)
    pattern = re.compile(
        rf"(?m)^[ \t]*(?!#)(?P<decl>[A-Za-z_][^;{{}}#]*?\b{re.escape(name)}"
        rf"\s*\([^;{{}}#]*?\)\s*;)",
        re.DOTALL,
    )
    declarations: list[Declaration] = []
    for match in pattern.finditer(clean):
        source = match.group("decl").strip()
        try:
            signature = normalize_signature(source, name)
        except ValueError:
            continue
        line = clean.count("\n", 0, match.start("decl")) + 1
        declarations.append(Declaration(signature, path, line, source))
    return declarations


def extract_public_apis(header_dir: Path) -> dict[str, list[Declaration]]:
    found = {name: [] for name in EXPECTED_APIS}
    for header in sorted(header_dir.rglob("*.h")):
        text = header.read_text(encoding="utf-8")
        for name in EXPECTED_APIS:
            found[name].extend(
                extract_declarations_from_text(text, name, str(header))
            )
    return found


def evaluate(found: dict[str, list[Declaration]]) -> list[str]:
    failures: list[str] = []
    for name, expected_source in EXPECTED_APIS.items():
        expected = normalize_signature(expected_source, name)
        candidates = found.get(name, [])
        if len(candidates) != 1:
            failures.append(f"{name}: 预期唯一公开声明，实际 {len(candidates)} 个")
            for candidate in candidates:
                failures.append(
                    f"  {candidate.path}:{candidate.line}: {candidate.signature}"
                )
            continue
        if candidates[0].signature != expected:
            candidate = candidates[0]
            failures.append(
                f"{name}: {candidate.path}:{candidate.line}: "
                f"实际 {candidate.signature}，预期 {expected}"
            )
    return failures


def run_self_test() -> int:
    failures: list[str] = []

    def expect(label: str, condition: bool) -> None:
        if not condition:
            failures.append(label)

    name = "sample_api"
    expected = normalize_signature("int sample_api(const char *name, uint32_t count)", name)
    valid = extract_declarations_from_text(
        "/* int sample_api(void); */\nint\n sample_api(const char *label,\n uint32_t n);\n",
        name,
        "valid.h",
    )
    wrong_return = extract_declarations_from_text(
        "void sample_api(const char *name, uint32_t count);", name, "return.h"
    )
    wrong_const = extract_declarations_from_text(
        "int sample_api(char *name, uint32_t count);", name, "const.h"
    )
    wrong_order = extract_declarations_from_text(
        "int sample_api(uint32_t count, const char *name);", name, "order.h"
    )
    comment_only = extract_declarations_from_text(
        "/* int sample_api(const char *name, uint32_t count); */",
        name,
        "comment.h",
    )

    expect("多行、空白和参数名变化应通过", len(valid) == 1 and valid[0].signature == expected)
    expect("返回类型变化必须失败", len(wrong_return) == 1 and wrong_return[0].signature != expected)
    expect("const 丢失必须失败", len(wrong_const) == 1 and wrong_const[0].signature != expected)
    expect("参数顺序变化必须失败", len(wrong_order) == 1 and wrong_order[0].signature != expected)
    expect("仅注释包含名字不能匹配", not comment_only)

    if failures:
        print("check_api_alignment 自测试：FAIL")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("check_api_alignment 自测试：PASS（5 组声明正反例）")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="运行内置正反例")
    args = parser.parse_args()
    if args.self_test:
        return run_self_test()

    found = extract_public_apis(Path("include"))
    failures = evaluate(found)
    print("Baremetal API Alignment Check")
    print("=" * 40)
    for name in sorted(EXPECTED_APIS):
        name_failures = [item for item in failures if item.startswith(f"{name}:")]
        print(f"  [{'FAIL' if name_failures else 'OK  '}] {name}")
    if failures:
        print("\n签名不一致：")
        for failure in failures:
            print(f"  {failure}")
        return 1
    print("\nAll expected API signatures match.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
