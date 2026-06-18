#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ELF="$ROOT/build_qemu/qemu_hybrid_smoke_cm0.elf"
TIMEOUT="${QEMU_TIMEOUT_SEC:-30}"
OUT="$(mktemp)"

if [[ ! -f "$ELF" ]]; then
    echo "Missing $ELF — build tests/qemu first" >&2
    exit 1
fi

set +e
timeout "$TIMEOUT" qemu-system-arm -machine microbit -cpu cortex-m0 \
    -kernel "$ELF" --semihosting -display none >"$OUT" 2>&1
RC=$?
set -e

cat "$OUT"
grep -q 'ok 1 - hybrid_hrt_smoke' "$OUT" || { rm -f "$OUT"; exit 1; }
rm -f "$OUT"
