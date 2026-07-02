# QEMU RISC-V64 virt icount 确定性测试构建
#
# 用法（须 Ninja + Zephyr SDK riscv64-zephyr-elf + qemu-system-riscv64）：
#   cmake -B build_icount_rv64 -S tests/qemu_rv64_icount -G Ninja \
#       -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchain-riscv64-none-elf.cmake
#   cmake --build build_icount_rv64
#   ctest --test-dir build_icount_rv64 --output-on-failure
#
# 以 proven 的 qemu_rv64_smp 为参考：同 backend(qemu_riscv64_smp)/boot/linker，
# 单核（BM_CONFIG_SMP=OFF，-smp 1，如 ARM icount 线），QEMU_ARGS 加
# -icount shift=0,sleep=off。周期源=rdcycle（已 de-risk：icount 下随执行推进）。
cmake_minimum_required(VERSION 3.20)

get_filename_component(BM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "${BM_ROOT}/cmake/toolchain-riscv64-none-elf.cmake"
        CACHE FILEPATH "Toolchain file" FORCE)
endif()

project(qemu-det-icount-rv64 C ASM)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(BM_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(BM_CONFIG_SMP     OFF CACHE BOOL "" FORCE)   # 单核 icount（如 ARM 线）
set(BM_ENABLE_MODULE  OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_WDG     OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_HRT     OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_TICKER  OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_EXEC    OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_STREAM  OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_BUS     ON  CACHE BOOL "" FORCE)    # bus LATEST Δ==0（Task 10）
set(BM_ENABLE_ALGORITHM ON CACHE BOOL "" FORCE)   # clarke Δ==0（Task 9）
set(BM_BACKEND "qemu_riscv64_smp" CACHE STRING "" FORCE)
set(BM_CONFIG_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/bm_config_rv64_icount.h"
    CACHE FILEPATH "" FORCE)

add_subdirectory(${BM_ROOT} _bmelod_core)

set(_BM_INCLUDES
    ${BM_ROOT}/include
    ${BM_ROOT}/include/hal
    ${BM_ROOT}/include/bm/common
    ${BM_ROOT}/include/bm/core
    ${BM_ROOT}/include/bm/hybrid
    ${BM_ROOT}/include/bm/algorithm
    ${BM_ROOT}/include/drv
    ${BM_ROOT}/include/port
    ${CMAKE_CURRENT_SOURCE_DIR}/../qemu
)

set(_BM_RV64_BOOT
    ${BM_ROOT}/portable/boot/qemu_riscv64_smp/startup_qemu_rv64_smp.S
    ${BM_ROOT}/portable/boot/qemu_riscv64_smp/crt0_qemu_rv64_smp.c
)

add_executable(qemu_det_icount_rv64.elf
    ${BM_ROOT}/tests/qemu/test_det_icount_rv64.c
    ${_BM_RV64_BOOT}
)
target_link_libraries(qemu_det_icount_rv64.elf PRIVATE
    "-Wl,--start-group"
    "-Wl,--whole-archive"
    bm_backend_qemu_riscv64_smp bm_sim_qemu_riscv64_smp
    "-Wl,--no-whole-archive"
    bm_hal bm_core bm_algorithm
    "-Wl,--end-group")
target_include_directories(qemu_det_icount_rv64.elf PRIVATE ${_BM_INCLUDES})
target_link_options(qemu_det_icount_rv64.elf PRIVATE
    -T${BM_ROOT}/portable/boot/qemu_riscv64_smp/linker_smp.ld
    -nostartfiles -Wl,--gc-sections)

find_program(QEMU_RISCV64 qemu-system-riscv64
    PATHS "C:/Program Files/qemu")
if(QEMU_RISCV64)
    enable_testing()
    add_test(NAME qemu_det_icount_rv64
        COMMAND ${CMAKE_COMMAND}
            -DQEMU=${QEMU_RISCV64}
            "-DQEMU_ARGS=-machine;virt;-cpu;rv64;-smp;1;-bios;none;-nographic;-display;none;-icount;shift=0,sleep=off"
            -DELF=$<TARGET_FILE:qemu_det_icount_rv64.elf>
            -DSERIAL=${CMAKE_CURRENT_BINARY_DIR}/det_icount_rv64_serial.txt
            -DTIMEOUT=30
            -P ${BM_ROOT}/tests/qemu/run_qemu_tap.cmake)
    set_tests_properties(qemu_det_icount_rv64 PROPERTIES LABELS "determinism-l2")
else()
    message(WARNING "qemu-system-riscv64 not found; ctest target unavailable")
endif()
