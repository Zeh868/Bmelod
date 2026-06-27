# bm_bus 多核 QEMU Cortex-A15 SMP 验证构建
# 用法（从 Bmelod-mp 仓库根目录；需 Ninja 生成器 + 绝对 toolchain 路径）：
#
#   cmake -B build_cortexa_bus -S tests/qemu_cortexa_bus -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=$PWD/framework/Bmelod/cmake/toolchain-arm-none-eabi-a.cmake
#   cmake --build build_cortexa_bus --target qemu_bus_smp_cortexa.elf
#   qemu-system-arm -machine virt -cpu cortex-a15 -smp 2 -m 128M -nographic \
#     -serial file:bus_smp_serial.txt -kernel build_cortexa_bus/qemu_bus_smp_cortexa.elf
#   # 期望：TAP 18/18 PASS（# bm_bus SMP test: PASSED）

cmake_minimum_required(VERSION 3.20)

get_filename_component(BMEFLOD_MP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
list(APPEND CMAKE_MODULE_PATH "${BMEFLOD_MP_ROOT}/cmake")

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "${BMEFLOD_MP_ROOT}/framework/Bmelod/cmake/toolchain-arm-none-eabi-a.cmake"
        CACHE FILEPATH "Toolchain file" FORCE)
endif()

project(qemu-bus-cortexa-smp-tests C ASM)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(bmeflod_mp)

set(_BM_QEMU_INCLUDES
    ${BMELOD_CORE_ROOT}/include
    ${BMELOD_CORE_ROOT}/include/hal
    ${BMELOD_CORE_ROOT}/include/bm/common
    ${BMELOD_CORE_ROOT}/include/bm/core
    ${BMELOD_CORE_ROOT}/include/bm/hybrid
    ${BMELOD_CORE_ROOT}/include/drv
    ${BMELOD_CORE_ROOT}/include/port
    ${BMEFLOD_MP_ROOT}/include/bm/mp
)

function(bm_cortexa_target_includes target)
    target_include_directories(${target} PRIVATE ${_BM_QEMU_INCLUDES})
endfunction()

set(BM_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_MODULE OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_WDG    OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_HRT    OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_TICKER OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_EXEC   OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_STREAM ON  CACHE BOOL "" FORCE)   # BLOCK 模式 adapter 需要 bm_stream
set(BM_ENABLE_IPC    OFF CACHE BOOL "" FORCE)
set(BM_BACKEND "qemu_cortexa_smp" CACHE STRING "" FORCE)
set(BM_CONFIG_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/bm_config_cortexa_bus_smp.h"
    CACHE FILEPATH "" FORCE)

bmeflod_mp_init(CORE_ROOT "${BMELOD_CORE_ROOT}" MP_ROOT "${BMEFLOD_MP_ROOT}")

set(_BM_CORTEXA_BOOT
    ${BMEFLOD_MP_ROOT}/portable/boot/qemu_cortexa_smp/startup_qemu_cortexa_smp.S
    ${BMEFLOD_MP_ROOT}/portable/boot/qemu_cortexa_smp/crt0_qemu_cortexa_smp.c
)

add_executable(qemu_bus_smp_cortexa.elf
    ${CMAKE_CURRENT_SOURCE_DIR}/../qemu/test_qemu_bus_smp.c
    ${_BM_CORTEXA_BOOT}
)

target_link_libraries(qemu_bus_smp_cortexa.elf PRIVATE
    "-Wl,--start-group"
    bm_backend_qemu_cortexa_smp
    bm_hal
    bm_stream    # bm_stream_block_adapter + bm_stream（BLOCK 模式 adapter 依赖）
    bm_core
    "-Wl,--end-group"
)
bm_cortexa_target_includes(qemu_bus_smp_cortexa.elf)
target_link_options(qemu_bus_smp_cortexa.elf PRIVATE
    -T${BMEFLOD_MP_ROOT}/portable/boot/qemu_cortexa_smp/linker_smp.ld
    -nostartfiles
    -Wl,--gc-sections
    --specs=nosys.specs
)

find_program(QEMU_ARM qemu-system-arm)
if(QEMU_ARM)
    message(STATUS "Found qemu-system-arm: ${QEMU_ARM}")
    add_custom_target(run-bus-smp-cortexa
        COMMAND ${QEMU_ARM}
            -machine virt
            -cpu cortex-a15
            -smp 2
            -m 128M
            -nographic
            -serial file:${CMAKE_CURRENT_BINARY_DIR}/bus_smp_serial.txt
            -kernel $<TARGET_FILE:qemu_bus_smp_cortexa.elf>
        DEPENDS qemu_bus_smp_cortexa.elf
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Running bm_bus SMP test on QEMU Cortex-A15 (2 cores)"
    )
else()
    message(WARNING "qemu-system-arm not found; run-bus-smp-cortexa target unavailable")
endif()
