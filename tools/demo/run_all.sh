#!/usr/bin/env bash
set -euo pipefail

# shellcheck source=demo_paths.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/demo_paths.sh"

BUILD_ROOT="$(bm_demo_variant_root unix)"
FAILED=()

bm_demo_ensure_unified_configure unix

while IFS= read -r example; do
    [[ -z "$example" || "$example" == \#* ]] && continue

    echo "=== Building $example ==="
    cmake --build "$BUILD_ROOT" --target "$example.elf"

    echo "=== Running $example in QEMU ==="
    log_file="$(bm_demo_example_out_dir unix "$example")/qemu.log"
    timeout 8s qemu-system-arm -machine microbit -cpu cortex-m0 \
        -kernel "$(bm_demo_elf_path unix "$example")" --semihosting -display none \
        >"$log_file" 2>&1 || true
    head -n 30 "$log_file"

    if grep -q "EXAMPLE_.*: PASS" "$log_file"; then
        echo "$example ... PASS"
    else
        echo "$example ... FAIL"
        FAILED+=("$example")
    fi
done < "$EXAMPLES_LIST"

if ((${#FAILED[@]} > 0)); then
    echo "Failed examples: ${FAILED[*]}"
    exit 1
fi

echo "All examples passed."
