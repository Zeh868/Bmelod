# bm_bus 多核 QEMU Cortex-A15 SMP 验证构建（合并仓单根布局）
#
# 用法：
#   cmake -B build -S tests/qemu_cortexa_bus -G Ninja \
#       -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchain-arm-none-eabi-a.cmake
#   cmake --build build && ctest --test-dir build --output-on-failure
#   # 期望：TAP 全 ok（# bm_bus SMP test: PASSED）
#
# bm_bus BLOCK 模式经 bm_stream_block_adapter 委托 bm_stream，故需 bm_stream；
# 不使用 bm_mp，BM_CONFIG_SMP=OFF + qemu_cortexa_smp 后端提供次核与 cpu-local。
cmake_minimum_required(VERSION 3.20)

get_filename_component(BM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "${BM_ROOT}/cmake/toolchain-arm-none-eabi-a.cmake"
        CACHE FILEPATH "Toolchain file" FORCE)
endif()

project(qemu-bus-cortexa-smp-tests C ASM)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(BM_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(BM_CONFIG_SMP    OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_MODULE OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_WDG    OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_HRT    OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_TICKER OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_EXEC   OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_STREAM ON  CACHE BOOL "" FORCE)   # BLOCK 模式 adapter 需要 bm_stream
set(BM_ENABLE_BUS    ON  CACHE BOOL "" FORCE)
set(BM_BACKEND "qemu_cortexa_smp" CACHE STRING "" FORCE)
set(BM_CONFIG_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/bm_config_cortexa_bus_smp.h"
    CACHE FILEPATH "" FORCE)

add_subdirectory(${BM_ROOT} _bmelod_core)

set(_BM_QEMU_INCLUDES
    ${BM_ROOT}/include
    ${BM_ROOT}/include/hal
    ${BM_ROOT}/include/bm/common
    ${BM_ROOT}/include/bm/core
    ${BM_ROOT}/include/bm/hybrid
    ${BM_ROOT}/include/drv
    ${BM_ROOT}/include/port
    ${CMAKE_CURRENT_SOURCE_DIR}/../qemu
)

set(_BM_CORTEXA_BOOT
    ${BM_ROOT}/portable/boot/qemu_cortexa_smp/startup_qemu_cortexa_smp.S
    ${BM_ROOT}/portable/boot/qemu_cortexa_smp/crt0_qemu_cortexa_smp.c
)

add_executable(qemu_bus_smp_cortexa.elf
    ${CMAKE_CURRENT_SOURCE_DIR}/../qemu/test_qemu_bus_smp.c
    ${_BM_CORTEXA_BOOT}
)
target_link_libraries(qemu_bus_smp_cortexa.elf PRIVATE
    "-Wl,--start-group"
    bm_backend_qemu_cortexa_smp bm_hal bm_stream bm_core
    "-Wl,--end-group")
target_include_directories(qemu_bus_smp_cortexa.elf PRIVATE ${_BM_QEMU_INCLUDES})
target_link_options(qemu_bus_smp_cortexa.elf PRIVATE
    -T${BM_ROOT}/portable/boot/qemu_cortexa_smp/linker_smp.ld
    -nostartfiles -Wl,--gc-sections --specs=nosys.specs)

find_program(QEMU_ARM qemu-system-arm)
if(QEMU_ARM)
    enable_testing()
    add_test(NAME qemu_bus_smp_cortexa
        COMMAND ${CMAKE_COMMAND}
            -DQEMU=${QEMU_ARM}
            "-DQEMU_ARGS=-machine;virt;-cpu;cortex-a15;-smp;2;-m;128M;-nographic;-display;none"
            -DELF=$<TARGET_FILE:qemu_bus_smp_cortexa.elf>
            -DSERIAL=${CMAKE_CURRENT_BINARY_DIR}/bus_smp_serial.txt
            -P ${BM_ROOT}/tests/qemu/run_qemu_tap.cmake)
endif()
