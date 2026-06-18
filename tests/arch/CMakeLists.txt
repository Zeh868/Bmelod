# 架构层交叉编译烟雾测试（riscv32 / riscv64）
# 需安装 riscv-none-elf-gcc 与/或 riscv64-unknown-elf-gcc

cmake_minimum_required(VERSION 3.20)

if(NOT CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR "tests/arch requires -DCMAKE_TOOLCHAIN_FILE=...")
endif()

project(bm_arch_smoke C ASM)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(BM_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

set(BM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_MODULE OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_CHANNEL OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_SHELL OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_WDG OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_HRT OFF CACHE BOOL "" FORCE)
set(BM_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/bm_config.h" CACHE FILEPATH "" FORCE)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
    set(_BM_ARCH_BACKEND qemu_riscv64)
    set(_BM_BOOT_DIR ${BM_ROOT}/portable/boot/riscv64)
    set(_BM_STARTUP ${_BM_BOOT_DIR}/startup_qemu_rv64.S)
    set(_BM_CRT0 ${_BM_BOOT_DIR}/crt0_qemu_rv64.c)
    set(_BM_LINKER ${_BM_BOOT_DIR}/linker.ld)
    set(_BM_TEST_NAME arch_smoke_riscv64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv")
    set(_BM_ARCH_BACKEND external)
    set(_BM_BOOT_DIR "")
    set(_BM_STARTUP "")
    set(_BM_CRT0 "")
    set(_BM_LINKER "")
    set(_BM_TEST_NAME arch_smoke_riscv32)
else()
    message(FATAL_ERROR "tests/arch: unsupported processor ${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(BM_BACKEND "${_BM_ARCH_BACKEND}" CACHE STRING "" FORCE)
add_subdirectory(${BM_ROOT} bmelod EXCLUDE_FROM_ALL)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv32|riscv" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
    add_subdirectory(${BM_ROOT}/portable/arch/riscv32 _bm_arch_riscv32_smoke)
endif()

add_executable(${_BM_TEST_NAME}.elf
    test_arch_critical.c
    ${_BM_CRT0}
    ${_BM_STARTUP}
)

if(_BM_ARCH_BACKEND STREQUAL "external")
    target_link_libraries(${_BM_TEST_NAME}.elf PRIVATE
        bm_core bm_hal bm_port_arch_riscv32)
    target_link_options(${_BM_TEST_NAME}.elf PRIVATE
        -Wl,-e,main
        -nostartfiles
        -Wl,--gc-sections)
else()
    target_link_libraries(${_BM_TEST_NAME}.elf PRIVATE
        bm_core bm_hal bm_backend_qemu_riscv64)
endif()

if(_BM_LINKER)
    target_link_options(${_BM_TEST_NAME}.elf PRIVATE
        -T${_BM_LINKER}
        -nostartfiles
        -Wl,--gc-sections)
endif()

add_custom_target(arch_smoke ALL DEPENDS ${_BM_TEST_NAME}.elf)
message(STATUS "Arch smoke firmware: ${_BM_TEST_NAME}.elf")
