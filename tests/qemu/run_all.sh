#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build_qemu"
TOOLCHAIN="$ROOT/cmake/toolchain-arm-none-eabi.cmake"

cmake -G 'Unix Makefiles' -S "$ROOT/tests/qemu" -B "$BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN"
cmake --build "$BUILD" --target qemu_smoke_cm0.elf qemu_hybrid_smoke_cm0.elf

"$ROOT/tests/qemu/run_hybrid_smoke.sh"

for ex in hrt_servo_stub hrt_bms_coulomb stream_block_rms stream_fft stream_bms_pipeline multi_axis_sync multi_channel_bms; do
    "$ROOT/tools/demo/run_qemu.sh" "$ex"
done
