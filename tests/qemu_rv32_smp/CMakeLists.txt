# QEMU RISC-V32 virt SMP 验证构建（克隆自 tests/qemu_rv64_smp）
#
# 用法（独立工程，须 Ninja + Zephyr SDK riscv64-zephyr-elf(multilib rv32) + qemu-system-riscv32）：
#   cmake -B build -S tests/qemu_rv32_smp -G Ninja \
#       -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchain-riscv-none-elf.cmake \
#       -DZEPHYR_SDK_INSTALL_DIR=<sdk路径>
#   cmake --build build && ctest --test-dir build --output-on-failure
#
# 本工程以 BM_CONFIG_SMP=ON 构建：除冒烟 ELF 外，还编译完整 bm_mp 调度层，
# 用于验证多核库在 riscv32 交叉工具链下可正常编译（配合 native 运行覆盖）。
# 冒烟测试本身只用 core+HAL+后端（不链接 bm_mp），验证 CPU ID / 硬件定时器 / 串口；
# 定时器 tick 递增同时覆盖 _trap_entry 寄存器保存、mie.MTIE 使能与 mcause 清零。
cmake_minimum_required(VERSION 3.20)

get_filename_component(BM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "${BM_ROOT}/cmake/toolchain-riscv-none-elf.cmake"
        CACHE FILEPATH "Toolchain file" FORCE)
endif()

project(qemu-rv32-smp-tests C ASM)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(BM_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(BM_CONFIG_SMP     ON  CACHE BOOL "" FORCE)   # 编译 bm_mp（交叉编译验证）
set(BM_SMP_ENABLE_ALGO OFF CACHE BOOL "" FORCE)  # 冒烟不需 bmp_algo/bm_algorithm
set(BM_BACKEND "qemu_riscv32_smp" CACHE STRING "" FORCE)
set(BM_CONFIG_FILE
    "${BM_ROOT}/tests/qemu/bm_config_smp.h"
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
    ${BM_ROOT}/include/bm/mp
)

set(_BM_RV32_BOOT
    ${BM_ROOT}/portable/boot/qemu_riscv32_smp/startup_qemu_rv32_smp.S
    ${BM_ROOT}/portable/boot/qemu_riscv32_smp/crt0_qemu_rv32_smp.c
)

add_executable(qemu_smp_smoke_rv32.elf
    ${BM_ROOT}/tests/qemu/test_qemu_smp_smoke.c
    ${_BM_RV32_BOOT}
)
target_link_libraries(qemu_smp_smoke_rv32.elf PRIVATE
    "-Wl,--start-group"
    "-Wl,--whole-archive"
    bm_backend_qemu_riscv32_smp bm_sim_qemu_riscv32_smp
    "-Wl,--no-whole-archive"
    bm_hal bm_core
    "-Wl,--end-group")
target_include_directories(qemu_smp_smoke_rv32.elf PRIVATE ${_BM_QEMU_INCLUDES})
target_link_options(qemu_smp_smoke_rv32.elf PRIVATE
    -T${BM_ROOT}/portable/boot/qemu_riscv32_smp/linker_smp.ld
    -nostartfiles -Wl,--gc-sections)

# 强制把 bm_mp 纳入构建图（验证交叉编译），即便冒烟 ELF 不链接它
add_custom_target(bm_mp_crosscompile_check ALL DEPENDS bm_mp)

find_program(QEMU_RISCV32 qemu-system-riscv32)
if(QEMU_RISCV32)
    enable_testing()
    add_test(NAME qemu_smp_smoke_rv32
        COMMAND ${CMAKE_COMMAND}
            -DQEMU=${QEMU_RISCV32}
            "-DQEMU_ARGS=-machine;virt;-cpu;rv32;-smp;2;-bios;none;-nographic;-display;none"
            -DELF=$<TARGET_FILE:qemu_smp_smoke_rv32.elf>
            -DSERIAL=${CMAKE_CURRENT_BINARY_DIR}/smp_smoke_serial.txt
            -DTIMEOUT=15
            -P ${BM_ROOT}/tests/qemu/run_qemu_tap.cmake)
endif()
