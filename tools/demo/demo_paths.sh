#!/usr/bin/env bash
# Shared Demo build output paths. Source from tools/demo/*.sh
# Unified CMake tree per variant: <repo>/build/demo/<variant>/
# Example ELF: <repo>/build/demo/<variant>/<example>/<example>.elf

DEMO_TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$DEMO_TOOLS_DIR/../.." && pwd)"
DEMO_DIR="$ROOT_DIR/Demo"
BUILD_DEMO_ROOT="${BMELOD_DEMO_BUILD_ROOT:-$ROOT_DIR/build/demo}"
EXAMPLES_LIST="$DEMO_TOOLS_DIR/examples.txt"
TOOLCHAIN_ARM_NONE_EABI="$ROOT_DIR/cmake/toolchain-arm-none-eabi.cmake"

bm_demo_variant_root() {
    local variant="$1"
    echo "$BUILD_DEMO_ROOT/$variant"
}

bm_demo_example_out_dir() {
    local variant="$1"
    local example="$2"
    echo "$(bm_demo_variant_root "$variant")/$example"
}

bm_demo_elf_path() {
    local variant="$1"
    local example="$2"
    echo "$(bm_demo_example_out_dir "$variant" "$example")/$example.elf"
}

# Back-compat alias: per-example artifact directory.
bm_demo_build_dir() {
    bm_demo_example_out_dir "$@"
}

bm_demo_ensure_unified_configure() {
    local variant="$1"
    local native_flag="${2:-}"
    local build_root
    build_root="$(bm_demo_variant_root "$variant")"
    if [[ -f "$build_root/CMakeCache.txt" ]]; then
        return 0
    fi
    if [[ "$native_flag" == "native" ]]; then
        cmake -S "$DEMO_DIR" -B "$build_root" -DBMELOD_DEMO_NATIVE=ON
    elif [[ "$variant" == "unix" ]]; then
        cmake -G "Unix Makefiles" -S "$DEMO_DIR" -B "$build_root" \
            -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_ARM_NONE_EABI"
    else
        cmake -G "MinGW Makefiles" -S "$DEMO_DIR" -B "$build_root" \
            -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_ARM_NONE_EABI"
    fi
}
