#!/usr/bin/env bash
set -euo pipefail

if (($# != 1)); then
    echo "Usage: $0 <example_name>"
    exit 1
fi

# shellcheck source=demo_paths.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/demo_paths.sh"

EXAMPLE="$1"
SOURCE_DIR="$DEMO_DIR/$EXAMPLE"
BUILD_DIR="$(bm_demo_build_dir unix "$EXAMPLE")"

if ! grep -Fxq "$EXAMPLE" "$EXAMPLES_LIST" || [[ ! -d "$SOURCE_DIR" ]]; then
    echo "Unknown example '$EXAMPLE'"
    exit 1
fi

cmake -G "Unix Makefiles" -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_ARM_NONE_EABI"
cmake --build "$BUILD_DIR"

echo "Press Ctrl+C to stop."
qemu-system-arm -machine microbit -cpu cortex-m0 \
    -kernel "$BUILD_DIR/$EXAMPLE.elf" --semihosting -display none
