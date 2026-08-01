#!/usr/bin/env python3
"""检查自有 C/H 文件的中文 Doxygen 合规性。

默认扫描 include/、Source/、portable/；也可传入一个或多个文件/目录做分域检查。
检查结果稳定排序为 ``file:line: item``，任何缺口均返回非零退出码。
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        try:
            _stream.reconfigure(encoding="utf-8")
        except (ValueError, OSError):
            pass

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ROOTS = (ROOT / "include", ROOT / "Source", ROOT / "portable")
SOURCE_SUFFIXES = {".c", ".h"}

# 默认扫描根本身不包含 tests/third_party/build；仍保留精确目录规则，防止用户传入
# 仓库根目录时误扫第三方或生成物。禁止以文件名前缀作大范围豁免。
EXCLUDED_PARTS = {
    "third_party",
    "unity",
    "cmakefiles",
    "generated",
    "_generated",
}
EXCLUDED_PREFIXES = ("build", "cmake-build-")

# 例外必须精确到“类别、仓库相对路径、符号”，并写明不可用常规 Doxygen 的理由。
# 当前没有批准的符号例外；宏、typedef、结构体/枚举前置声明由候选提取规则排除。
SYMBOL_EXCEPTIONS: dict[tuple[str, str, str], str] = {}

CHINESE_RE = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")
DOXYGEN_RE = re.compile(r"/\*\*.*?\*/", re.DOTALL)
COMMENT_OR_LITERAL_RE = re.compile(
    r"/\*.*?\*/|//[^\r\n]*|L?\"(?:\\.|[^\"\\])*\"|L?'(?:\\.|[^'\\])*'",
    re.DOTALL,
)

# 仅匹配以声明语句形式出现的函数；typedef 函数指针、宏、结构体成员函数指针、
# 变量初始化中的调用均被前置条件或字符约束排除。
PUBLIC_DECL_RE = re.compile(
    r"(?m)^[ \t]*(?!#)(?!typedef\b)"
    r"(?P<decl>[A-Za-z_][^;{}=#]*?\b(?P<name>[A-Za-z_]\w*)"
    r"\s*\([^;{}#]*?\)\s*;)",
    re.DOTALL,
)
PUBLIC_DEF_RE = re.compile(
    r"(?m)^[ \t]*(?!#)(?!typedef\b)"
    r"(?P<decl>[A-Za-z_][^;{}=#]*?\b(?P<name>[A-Za-z_]\w*)"
    r"\s*\([^;{}#]*?\)\s*)\{",
    re.DOTALL,
)
STATIC_DEF_RE = re.compile(
    r"(?m)^[ \t]*(?P<decl>static\s+[^;{}=]*?\b(?P<name>[A-Za-z_]\w*)"
    r"\s*\([^;{}]*?\)\s*)\{",
    re.DOTALL,
)
CPP_IF_RE = (
    r"#\s*(?:ifdef\s+__cplusplus|if\s+(?:defined\s*\(\s*__cplusplus\s*\)"
    r"|defined\s+__cplusplus))"
)
EXTERN_C_OPEN_RE = re.compile(
    rf"(?m)^[ \t]*{CPP_IF_RE}[^\r\n]*\r?\n"
    rf"[ \t]*extern[ \t]+\"C\"[ \t]*(?P<brace>\{{)[ \t]*\r?\n"
    rf"[ \t]*#\s*endif\b"
)
EXTERN_C_CLOSE_RE = re.compile(
    rf"(?m)^[ \t]*{CPP_IF_RE}[^\r\n]*\r?\n"
    rf"[ \t]*(?P<brace>\}})[ \t]*\r?\n[ \t]*#\s*endif\b"
)

HEADER_FIELDS = (
    ("@file", re.compile(r"@file\b")),
    ("@brief", re.compile(r"@brief\b")),
    ("@maturity", re.compile(r"@maturity\b")),
    ("@author", re.compile(r"@author\b")),
    ("@version", re.compile(r"@version\b")),
    ("@date", re.compile(r"@date\b")),
    ("修改日志", re.compile(r"修改日志")),
    ("SPDX", re.compile(r"SPDX-License-Identifier:")),
)


@dataclass(frozen=True, order=True)
class Issue:
    path: str
    line: int
    category: str
    item: str

    def render(self) -> str:
        return f"{self.path}:{self.line}: {self.category}: {self.item}"


@dataclass(frozen=True)
class Candidate:
    name: str
    start: int
    line: int


def _relative_path(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return path.resolve().as_posix()


def _is_excluded(path: Path) -> bool:
    parts = [part.lower() for part in path.parts]
    if any(part in EXCLUDED_PARTS for part in parts):
        return True
    return any(
        part.startswith(prefix)
        for part in parts
        for prefix in EXCLUDED_PREFIXES
    )


def discover_files(inputs: Iterable[Path]) -> list[Path]:
    files: set[Path] = set()
    for source in inputs:
        path = source if source.is_absolute() else ROOT / source
        if path.is_file():
            if path.suffix.lower() in SOURCE_SUFFIXES and not _is_excluded(path):
                files.add(path.resolve())
            continue
        if path.is_dir():
            for candidate in path.rglob("*"):
                if (
                    candidate.is_file()
                    and candidate.suffix.lower() in SOURCE_SUFFIXES
                    and not _is_excluded(candidate)
                ):
                    files.add(candidate.resolve())
    return sorted(files, key=_relative_path)


def _mask_non_code(text: str) -> str:
    """用空白替换注释和字面量，同时保持字符偏移和行号。"""

    def replace(match: re.Match[str]) -> str:
        return "".join("\n" if char == "\n" else " " for char in match.group(0))

    return COMMENT_OR_LITERAL_RE.sub(replace, text)


def _neutralize_extern_c_wrappers(original: str, masked: str) -> str:
    """仅屏蔽规范 ``__cplusplus``/``extern "C"`` 包装器的一对花括号。"""
    chars = list(masked)
    for pattern in (EXTERN_C_OPEN_RE, EXTERN_C_CLOSE_RE):
        for match in pattern.finditer(original):
            brace = match.start("brace")
            chars[brace] = " "
    return "".join(chars)


def _first_header(text: str) -> tuple[str | None, int]:
    content_start = len(text) - len(text.lstrip("\ufeff \t\r\n"))
    doxygen_start = content_start
    spdx = re.match(
        r"(?:/\*\s*SPDX-License-Identifier:[^*]*\*/|"
        r"//\s*SPDX-License-Identifier:[^\r\n]*)",
        text[doxygen_start:],
    )
    if spdx:
        doxygen_start += spdx.end()
        doxygen_start += len(text[doxygen_start:]) - len(
            text[doxygen_start:].lstrip(" \t\r\n")
        )
    if not text.startswith("/**", doxygen_start):
        return None, 1
    match = DOXYGEN_RE.match(text, doxygen_start)
    if not match:
        return None, 1
    # SPDX 可独立置于 Doxygen 前，也可位于 Doxygen 文件头内部；合并检查范围使
    # 两种仓库既有格式得到相同结果，但不放宽为任意前导注释。
    checked_header = text[content_start:match.end()]
    return checked_header, text.count("\n", 0, doxygen_start) + 1


def _preceding_doxygen(text: str, start: int) -> str | None:
    prefix = text[:start].rstrip()
    if not prefix.endswith("*/"):
        return None
    block_start = prefix.rfind("/**")
    if block_start < 0:
        return None
    block = prefix[block_start:]
    if not DOXYGEN_RE.fullmatch(block):
        return None
    return block


def _valid_chinese_doxygen(block: str | None) -> tuple[bool, str]:
    if block is None:
        return False, "缺少紧邻的 /** ... */ 中文 Doxygen"
    if re.search(r"@file\b", block):
        return False, "文件头 Doxygen 不能替代符号说明"
    if not re.search(r"@brief\b", block):
        return False, "紧邻 Doxygen 缺少 @brief"
    if not CHINESE_RE.search(block):
        return False, "紧邻 Doxygen 的说明不是中文"
    return True, ""


def _public_candidates(masked: str) -> list[Candidate]:
    matches: list[re.Match[str]] = []
    for pattern in (PUBLIC_DECL_RE, PUBLIC_DEF_RE):
        matches.extend(pattern.finditer(masked))

    candidates: list[Candidate] = []
    depth = 0
    cursor = 0
    for match in sorted(matches, key=lambda item: item.start("decl")):
        start = match.start("decl")
        for char in masked[cursor:start]:
            if char == "{":
                depth += 1
            elif char == "}" and depth > 0:
                depth -= 1
        cursor = start
        # 只接受文件作用域声明/定义；这会排除 struct 成员和 header 内 static
        # inline 函数体中的 return/call 语句。仓库当前没有 extern "C" 花括号层。
        if depth != 0 or _inside_preprocessor_continuation(masked, start):
            continue
        declaration = match.group("decl")
        name = match.group("name")
        before_name = declaration[: declaration.rfind(name)]
        if re.search(r"\(\s*\*\s*$", before_name):
            continue
        if name in {"_Static_assert", "sizeof"}:
            continue
        candidates.append(
            Candidate(name, start, masked.count("\n", 0, start) + 1)
        )
    # 声明和定义模式理论上互斥；按位置/符号去重可避免扩展语法后重复报告。
    return sorted(set(candidates), key=lambda item: (item.start, item.name))


def _inside_preprocessor_continuation(masked: str, start: int) -> bool:
    """判断候选行是否属于以反斜杠续行的预处理器指令。"""
    line_start = masked.rfind("\n", 0, start) + 1
    probe = line_start
    while probe > 0:
        previous_end = probe - 1
        previous_start = masked.rfind("\n", 0, previous_end) + 1
        previous = masked[previous_start:previous_end].rstrip()
        if not previous.endswith("\\"):
            break
        probe = previous_start
    logical_start_end = masked.find("\n", probe)
    if logical_start_end < 0:
        logical_start_end = len(masked)
    return masked[probe:logical_start_end].lstrip().startswith("#")


def _static_candidates(masked: str) -> list[Candidate]:
    candidates: list[Candidate] = []
    for match in STATIC_DEF_RE.finditer(masked):
        start = match.start("decl")
        candidates.append(
            Candidate(
                match.group("name"), start, masked.count("\n", 0, start) + 1
            )
        )
    return candidates


def _is_symbol_exception(category: str, path: str, name: str) -> bool:
    reason = SYMBOL_EXCEPTIONS.get((category, path, name))
    return bool(reason and reason.strip())


def scan_text(path: str, text: str, suffix: str) -> list[Issue]:
    issues: list[Issue] = []
    header, header_line = _first_header(text)
    if header is None:
        issues.append(Issue(path, header_line, "文件头", "缺少文件起始 Doxygen 块"))
    else:
        for field, pattern in HEADER_FIELDS:
            if not pattern.search(header):
                issues.append(Issue(path, header_line, "文件头", f"缺少 {field}"))
        brief = re.search(r"@brief\s+([^\r\n*]+)", header)
        if brief is None or not CHINESE_RE.search(brief.group(1)):
            issues.append(Issue(path, header_line, "文件头", "@brief 缺少中文说明"))

    masked = _neutralize_extern_c_wrappers(text, _mask_non_code(text))
    if suffix == ".h":
        for candidate in _public_candidates(masked):
            category = "公共API"
            if _is_symbol_exception(category, path, candidate.name):
                continue
            valid, item = _valid_chinese_doxygen(
                _preceding_doxygen(text, candidate.start)
            )
            if not valid:
                issues.append(
                    Issue(path, candidate.line, category, f"{candidate.name}: {item}")
                )
    elif suffix == ".c":
        for candidate in _static_candidates(masked):
            category = "static辅助函数"
            if _is_symbol_exception(category, path, candidate.name):
                continue
            valid, item = _valid_chinese_doxygen(
                _preceding_doxygen(text, candidate.start)
            )
            if not valid:
                issues.append(
                    Issue(path, candidate.line, category, f"{candidate.name}: {item}")
                )
    return sorted(issues)


def scan_files(files: Iterable[Path]) -> list[Issue]:
    issues: list[Issue] = []
    for path in files:
        relative = _relative_path(path)
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as exc:
            issues.append(Issue(relative, 1, "文件读取", str(exc)))
            continue
        issues.extend(scan_text(relative, text, path.suffix.lower()))
    return sorted(issues)


def run_self_test() -> int:
    header = """/**
 * @file good.h
 * @brief 中文公开接口
 * @maturity E1
 * @author test
 * @version 1.0
 * @date 2026-08-01
 * @par 修改日志:
 * 2026-08-01 1.0 test 初始版本
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/** @brief 执行中文操作 */
int bm_good(int value);
/** @brief 执行中文内联操作 */
static inline int bm_inline_good(int value) { return bm_good(value); }
typedef void (*bm_callback_t)(int value);
struct bm_forward;
#define BM_CALL(value) bm_good(value)
"""
    source = """/**
 * @file good.c
 * @brief 中文实现
 * @maturity E1
 * @author test
 * @version 1.0
 * @date 2026-08-01
 * @par 修改日志:
 * 2026-08-01 1.0 test 初始版本
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/** @brief 执行中文辅助操作 */
static int helper(int value)
{
    return value;
}
"""
    extern_c_documented = """/**
 * @file extern_good.h
 * @brief 中文 C 接口包装
 * @maturity E1
 * @author test
 * @version 1.0
 * @date 2026-08-01
 * @par 修改日志:
 * 2026-08-01 1.0 test 初始版本
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifdef __cplusplus
extern "C" {
#endif
/** @brief 已文档化的包装内接口 */
int bm_extern_good(void);
#ifdef __cplusplus
}
#endif
"""
    extern_c_undocumented = extern_c_documented.replace(
        "/** @brief 已文档化的包装内接口 */\n", ""
    ).replace("bm_extern_good", "bm_extern_bad")
    missing_header_field = header.replace(" * @maturity E1\n", "")
    undocumented_public = header.replace(
        "/** @brief 执行中文操作 */\nint bm_good", "int bm_good"
    )
    english_static = source.replace("执行中文辅助操作", "Run helper")
    separate_spdx = (
        "/* SPDX-License-Identifier: GPL-3.0-or-later */\n"
        + header.replace(
            " * SPDX-License-Identifier: GPL-3.0-or-later\n", ""
        )
    )

    failures: list[str] = []

    def expect(label: str, condition: bool) -> None:
        if not condition:
            failures.append(label)

    expect("完整头文件及 public API 应通过", not scan_text("good.h", header, ".h"))
    expect("完整源文件及 static helper 应通过", not scan_text("good.c", source, ".c"))
    expect(
        "独立 SPDX 块可位于文件 Doxygen 前",
        not scan_text("spdx.h", separate_spdx, ".h"),
    )
    expect(
        "extern C 包装内已文档 API 应通过",
        not scan_text("extern_good.h", extern_c_documented, ".h"),
    )
    extern_issues = scan_text("extern_bad.h", extern_c_undocumented, ".h")
    expect(
        "extern C 包装内未文档 API 必须失败",
        any(
            issue.category == "公共API" and "bm_extern_bad" in issue.item
            for issue in extern_issues
        ),
    )

    issues = scan_text("missing.h", missing_header_field, ".h")
    expect("文件头缺 @maturity 必须失败", any("@maturity" in issue.item for issue in issues))

    issues = scan_text("public.h", undocumented_public, ".h")
    expect("未文档化 public API 必须失败", any(issue.category == "公共API" for issue in issues))

    issues = scan_text("static.c", english_static, ".c")
    expect("英文 static Doxygen 必须失败", any(issue.category == "static辅助函数" for issue in issues))

    # 宏、typedef 函数指针和结构体前置声明均不应被当成公共函数声明。
    public_names = [candidate.name for candidate in _public_candidates(_mask_non_code(header))]
    expect("合理例外不得误报", public_names == ["bm_good", "bm_inline_good"])

    sample = scan_text("stable.h", undocumented_public, ".h")
    expect("输出必须稳定排序", sample == sorted(sample) and sample == scan_text("stable.h", undocumented_public, ".h"))

    if failures:
        print("check_doxygen_compliance 自测试：FAIL")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("check_doxygen_compliance 自测试：PASS（10 组规则正反例）")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "paths",
        nargs="*",
        type=Path,
        help="可选文件或目录；省略时扫描 include/、Source/、portable/",
    )
    parser.add_argument("--self-test", action="store_true", help="运行内置正反例")
    args = parser.parse_args()
    if args.self_test:
        return run_self_test()

    roots = args.paths or list(DEFAULT_ROOTS)
    files = discover_files(roots)
    issues = scan_files(files)
    counts = {"文件头": 0, "公共API": 0, "static辅助函数": 0, "文件读取": 0}
    for issue in issues:
        counts[issue.category] = counts.get(issue.category, 0) + 1

    print("Bmelod Doxygen 合规检查")
    print("=" * 40)
    print(f"扫描文件：{len(files)}")
    print(f"缺口总数：{len(issues)}")
    for category in ("文件头", "公共API", "static辅助函数", "文件读取"):
        print(f"  {category}：{counts.get(category, 0)}")

    if issues:
        print("\n缺口清单：")
        for issue in issues:
            print(issue.render())
        print("\n结论：FAIL — 请按清单补齐中文 Doxygen。")
        return 1

    print("\n结论：PASS — 扫描范围内无 Doxygen 合规缺口。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
