#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TOOLCHAIN_RV32="$ROOT/cmake/toolchain-riscv-none-elf.cmake"
TOOLCHAIN_RV64="$ROOT/cmake/toolchain-riscv64-none-elf.cmake"

build_rv32() {
    if ! command -v riscv-none-elf-gcc >/dev/null 2>&1 &&
       ! command -v riscv32-unknown-elf-gcc >/dev/null 2>&1; then
        echo "skip riscv32: toolchain not installed"
        return 0
    fi
    cmake -G 'Unix Makefiles' -S "$ROOT/tests/arch" -B "$ROOT/build_arch_rv32" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_RV32"
    cmake --build "$ROOT/build_arch_rv32" --target arch_smoke
    echo "riscv32 arch smoke: link OK"
}

build_rv64() {
    if ! command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then
        echo "skip riscv64: toolchain not installed"
        return 0
    fi
    cmake -G 'Unix Makefiles' -S "$ROOT/tests/arch" -B "$ROOT/build_arch_rv64" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_RV64"
    cmake --build "$ROOT/build_arch_rv64" --target arch_smoke
    echo "riscv64 arch smoke: link OK"
}

run_rv64_qemu() {
    local elf="$ROOT/build_arch_rv64/arch_smoke_riscv64.elf"
    if [[ ! -f "$elf" ]]; then
        echo "skip qemu rv64: firmware not built"
        return 0
    fi
    if ! command -v qemu-system-riscv64 >/dev/null 2>&1; then
        echo "skip qemu rv64: qemu-system-riscv64 not installed"
        return 0
    fi
    qemu-system-riscv64 -machine virt -cpu rv64 -nographic \
        -kernel "$elf"
    echo "riscv64 QEMU virt smoke: PASS"
}

build_rv32
build_rv64
run_rv64_qemu
