#!/usr/bin/env python3
"""
Phase 3 布局与 BM_BACKEND 解析烟雾检查（无需 Xtensa 工具链）。

验证 sdk_esp32_idf → arch/xtensa + vendor/esp32_idf + packs/sdk_esp32_idf 路径存在，
且 bm_port_resolve.cmake 映射正确。Nordic/NXP 等其它 vendor 暂缓，不要求骨架存在。
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

REQUIRED = [
    "portable/arch/xtensa/CMakeLists.txt",
    "portable/arch/xtensa/bm_arch_critical.c",
    "portable/arch/xtensa/bm_arch_memory.c",
    "portable/arch/xtensa/bm_arch_portmacro.h",
    "portable/vendor/esp32_idf/CMakeLists.txt",
    "portable/vendor/esp32_idf/bm_vendor_singleton_esp32_idf.c",
    "portable/vendor/esp32_idf/idf_component.yml",
    "portable/vendor/esp32_idf/README.md",
    "portable/packs/sdk_esp32_idf/CMakeLists.txt",
]

DEFERRED_VENDOR_DIRS = [
    "portable/vendor/stm32g4",
    "portable/vendor/ch32v003",
    "portable/vendor/nordic_nrf52",
    "portable/vendor/nxp_kinetis",
]

RESOLVE_SNIPPETS = [
    ('backend STREQUAL "sdk_esp32_idf"', "_arch xtensa"),
    ('backend STREQUAL "sdk_esp32_idf"', "_vendor esp32_idf"),
    ('backend STREQUAL "sdk_esp32_idf"', "packs/sdk_esp32_idf"),
]

DOC_DEFERRAL_SNIPPETS = [
    ("docs/03-移植与IDE集成/01-HAL契约与移植要点.md", "暂缓"),
    ("docs/03-移植与IDE集成/08-ESP-IDF与灯哥平衡车集成.md", "ESP32-WROOM-32E"),
]


def main() -> int:
    errors: list[str] = []

    for rel in REQUIRED:
        if not (ROOT / rel).is_file():
            errors.append(f"missing file: {rel}")

    for rel in DEFERRED_VENDOR_DIRS:
        if (ROOT / rel).exists():
            errors.append(f"deferred vendor dir should not exist yet: {rel}")

    resolve = (ROOT / "cmake/bm_port_resolve.cmake").read_text(encoding="utf-8")
    for ctx, needle in RESOLVE_SNIPPETS:
        if needle not in resolve:
            errors.append(f"bm_port_resolve.cmake: expected {needle!r} near {ctx}")

    list_sources = (ROOT / "tools/list_sources.py").read_text(encoding="utf-8")
    for needle in (
        "portable/arch/xtensa/bm_arch_critical.c",
        "portable/vendor/esp32_idf/bm_vendor_singleton_esp32_idf.c",
    ):
        if needle not in list_sources:
            errors.append(f"tools/list_sources.py: missing {needle!r}")

    template = (ROOT / "portable/template/bm_port.c").read_text(encoding="utf-8")
    if "const struct bm_critical_driver_api bm_drv_critical_api" in template:
        errors.append("portable/template/bm_port.c must not define bm_drv_critical_api")
    if "const struct bm_memory_driver_api bm_drv_memory_api" in template:
        errors.append("portable/template/bm_port.c must not define bm_drv_memory_api")
    if "BM_PORT_WEAK static" in template:
        errors.append("portable/template/bm_port.c weak hooks must have external linkage")

    xtensa_critical = (ROOT / "portable/arch/xtensa/bm_arch_critical.c").read_text(
        encoding="utf-8"
    )
    if 'volatile ("rsil' not in xtensa_critical and '__asm__ volatile ("rsil' not in xtensa_critical:
        errors.append("portable/arch/xtensa/bm_arch_critical.c must use atomic rsil enter")

    esp32_vendor = (ROOT / "portable/vendor/esp32_idf/bm_vendor_singleton_esp32_idf.c").read_text(
        encoding="utf-8"
    )
    if "esp_task_wdt_config_t" not in esp32_vendor:
        errors.append("portable/vendor/esp32_idf must support ESP-IDF 5 task WDT API")

    esp32_cmake = (ROOT / "portable/vendor/esp32_idf/CMakeLists.txt").read_text(
        encoding="utf-8"
    )
    for needle in ("include/drv", "include/bm/common", "include/port"):
        if needle not in esp32_cmake:
            errors.append(f"portable/vendor/esp32_idf/CMakeLists.txt missing {needle}")

    esp32_readme = (ROOT / "portable/vendor/esp32_idf/README.md").read_text(encoding="utf-8")
    if "灯哥平衡车" not in esp32_readme:
        errors.append("portable/vendor/esp32_idf/README.md must mention 灯哥平衡车主控板")
    if "电机" not in esp32_readme or "引脚" not in esp32_readme:
        errors.append("portable/vendor/esp32_idf/README.md must note pending motor/IMU pin map")

    for doc_rel, needle in DOC_DEFERRAL_SNIPPETS:
        doc_path = ROOT / doc_rel
        if not doc_path.is_file():
            errors.append(f"missing doc: {doc_rel}")
            continue
        doc_text = doc_path.read_text(encoding="utf-8")
        if needle not in doc_text:
            errors.append(f"{doc_rel}: expected {needle!r} for deferred vendors")

    if errors:
        for err in errors:
            print(f"FAIL: {err}", file=sys.stderr)
        return 1

    print("PASS: Phase 3 layout and resolve checks (ESP32-WROOM-32E only)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
