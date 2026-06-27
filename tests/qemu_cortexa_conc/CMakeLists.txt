# core 层并发 QEMU Cortex-A15 SMP 验证构建（合并仓单根布局）
#
# 用法（独立工程，须 Ninja + arm-none-eabi 工具链 + qemu-system-arm）：
#   cmake -B build -S tests/qemu_cortexa_conc -G Ninja \
#       -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchain-arm-none-eabi-a.cmake
#   cmake --build build
#   ctest --test-dir build --output-on-failure
#
# 该测试仅验证 core 层并发（atomic_ipc/critical/mempool/event）在双核下的语义，
# 不使用 bm_mp 调度层，故以 BM_CONFIG_SMP=OFF 构建（不编译 bm_mp），仅借助
# qemu_cortexa_smp 后端提供次核引导与 cpu-local HAL。
cmake_minimum_required(VERSION 3.20)

get_filename_component(BM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "${BM_ROOT}/cmake/toolchain-arm-none-eabi-a.cmake"
        CACHE FILEPATH "Toolchain file" FORCE)
endif()

project(qemu-conc-cortexa-smp-tests C ASM)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(BM_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(BM_CONFIG_SMP    OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_MODULE OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_WDG    OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_HRT    OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_TICKER OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_EXEC   OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_STREAM OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_BUS    OFF CACHE BOOL "" FORCE)
set(BM_BACKEND "qemu_cortexa_smp" CACHE STRING "" FORCE)
set(BM_CONFIG_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/bm_config_cortexa_conc_core.h"
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

add_executable(qemu_conc_core.elf
    ${CMAKE_CURRENT_SOURCE_DIR}/../qemu/test_qemu_conc_core.c
    ${_BM_CORTEXA_BOOT}
)
target_link_libraries(qemu_conc_core.elf PRIVATE
    "-Wl,--start-group"
    bm_backend_qemu_cortexa_smp bm_hal bm_core
    "-Wl,--end-group")
target_include_directories(qemu_conc_core.elf PRIVATE ${_BM_QEMU_INCLUDES})
target_link_options(qemu_conc_core.elf PRIVATE
    -T${BM_ROOT}/portable/boot/qemu_cortexa_smp/linker_smp.ld
    -nostartfiles -Wl,--gc-sections --specs=nosys.specs)

find_program(QEMU_ARM qemu-system-arm)
if(QEMU_ARM)
    enable_testing()
    add_test(NAME qemu_conc_core
        COMMAND ${CMAKE_COMMAND}
            -DQEMU=${QEMU_ARM}
            "-DQEMU_ARGS=-machine;virt;-cpu;cortex-a15;-smp;2;-m;128M;-nographic;-display;none"
            -DELF=$<TARGET_FILE:qemu_conc_core.elf>
            -DSERIAL=${CMAKE_CURRENT_BINARY_DIR}/conc_core_serial.txt
            -P ${BM_ROOT}/tests/qemu/run_qemu_tap.cmake)
endif()
