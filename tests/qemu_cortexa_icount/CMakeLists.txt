# QEMU ARM Cortex-A15 virt icount 确定性测试构建
#
# 用法：
#   cmake -B build_icount_arm -S tests/qemu_cortexa_icount -G Ninja \
#       -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchain-arm-none-eabi-a.cmake
#   cmake --build build_icount_arm
#   ctest --test-dir build_icount_arm --output-on-failure -V
#
# 与 qemu_cortexa_bus 的唯一关键区别：QEMU_ARGS 加 -icount shift=0,sleep=off；
# 单核（-smp 1）：CPU1 不存在，startup_qemu_cortexa_smp.S 在 MPIDR 检查后
# 仅 CPU0 继续执行，不影响单核测试语义。
cmake_minimum_required(VERSION 3.20)

get_filename_component(BM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "${BM_ROOT}/cmake/toolchain-arm-none-eabi-a.cmake"
        CACHE FILEPATH "Toolchain file" FORCE)
endif()

project(qemu-det-icount-arm C ASM)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(BM_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(BM_CONFIG_SMP     OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_MODULE  OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_WDG     OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_HRT     OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_TICKER  OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_EXEC    OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_STREAM  OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_BUS     ON  CACHE BOOL "" FORCE)    # bus LATEST Δ==0 测试需要（Task 3）
set(BM_ENABLE_ALGORITHM ON CACHE BOOL "" FORCE)   # clarke Δ==0 需要（Task 2）
set(BM_BACKEND "qemu_cortexa_smp" CACHE STRING "" FORCE)
set(BM_CONFIG_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/bm_config_icount.h"
    CACHE FILEPATH "" FORCE)

add_subdirectory(${BM_ROOT} _bmelod_core)

set(_BM_INCLUDES
    ${BM_ROOT}/include
    ${BM_ROOT}/include/hal
    ${BM_ROOT}/include/bm/common
    ${BM_ROOT}/include/bm/core
    ${BM_ROOT}/include/bm/hybrid
    ${BM_ROOT}/include/bm/algorithm
    ${CMAKE_CURRENT_SOURCE_DIR}/../qemu
)

set(_BM_CORTEXA_BOOT
    ${BM_ROOT}/portable/boot/qemu_cortexa_smp/startup_qemu_cortexa_smp.S
    ${BM_ROOT}/portable/boot/qemu_cortexa_smp/crt0_qemu_cortexa_smp.c
)

add_executable(qemu_det_icount_arm.elf
    ${BM_ROOT}/tests/qemu/test_det_icount_arm.c
    ${_BM_CORTEXA_BOOT}
)
target_link_libraries(qemu_det_icount_arm.elf PRIVATE
    "-Wl,--start-group"
    bm_backend_qemu_cortexa_smp bm_hal bm_core bm_algorithm
    "-Wl,--end-group")
target_include_directories(qemu_det_icount_arm.elf PRIVATE ${_BM_INCLUDES})
target_link_options(qemu_det_icount_arm.elf PRIVATE
    -T${BM_ROOT}/portable/boot/qemu_cortexa_smp/linker_smp.ld
    -nostartfiles -Wl,--gc-sections --specs=nosys.specs)

find_program(QEMU_ARM qemu-system-arm
    PATHS "C:/Program Files/qemu"
    PATH_SUFFIXES "")
if(QEMU_ARM)
    enable_testing()
    add_test(NAME qemu_det_icount_arm
        COMMAND ${CMAKE_COMMAND}
            -DQEMU=${QEMU_ARM}
            "-DQEMU_ARGS=-machine;virt;-cpu;cortex-a15;-smp;1;-m;128M;-nographic;-display;none;-icount;shift=0,sleep=off"
            -DELF=$<TARGET_FILE:qemu_det_icount_arm.elf>
            -DSERIAL=${CMAKE_CURRENT_BINARY_DIR}/det_icount_arm_serial.txt
            -DTIMEOUT=30
            -P ${BM_ROOT}/tests/qemu/run_qemu_tap.cmake)
    set_tests_properties(qemu_det_icount_arm PROPERTIES LABELS "determinism-l2")
else()
    message(WARNING "qemu-system-arm not found; ctest target unavailable")
endif()
