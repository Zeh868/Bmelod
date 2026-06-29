cmake_minimum_required(VERSION 3.20)
option(NATIVE_SIM_BUILD "Build for host native_sim_mp" ON)
option(QEMU_SMP_BUILD "Build for qemu_riscv64_smp" OFF)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    if(QEMU_SMP_BUILD AND NATIVE_SIM_BUILD)
        message(FATAL_ERROR "QEMU_SMP_BUILD 与 NATIVE_SIM_BUILD 互斥，请只启用其一")
    endif()
    if(QEMU_SMP_BUILD)
        get_filename_component(BMEFLOD_MP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
        if(EXISTS "${BMEFLOD_MP_ROOT}/framework/Bmelod/CMakeLists.txt")
            set(BMELOD_CORE_ROOT "${BMEFLOD_MP_ROOT}/framework/Bmelod")
        elseif(EXISTS "${BMEFLOD_MP_ROOT}/../Bmelod/CMakeLists.txt")
            set(BMELOD_CORE_ROOT "${BMEFLOD_MP_ROOT}/../Bmelod")
        else()
            message(FATAL_ERROR "dual_core_bms: cannot find Bmelod core")
        endif()
        get_filename_component(BMELOD_CORE_ROOT "${BMELOD_CORE_ROOT}" ABSOLUTE)
        if(NOT CMAKE_TOOLCHAIN_FILE)
            set(CMAKE_TOOLCHAIN_FILE
                "${BMELOD_CORE_ROOT}/cmake/toolchain-riscv64-none-elf.cmake"
                CACHE FILEPATH "RISC-V64 cross toolchain" FORCE)
        endif()
        project(dual_core_bms_qemu_smp C ASM)
    else()
        project(dual_core_bms_native C)
    endif()
endif()

if(QEMU_SMP_BUILD)
    include(${CMAKE_CURRENT_LIST_DIR}/../cmake/mp_demo_qemu_smp.cmake)
    mp_add_qemu_smp_demo(dual_core_bms LINK_ALGO
        main.c
        ${CMAKE_CURRENT_LIST_DIR}/../common/mp_relay_algo_demo.c
    )
elseif(NATIVE_SIM_BUILD)
    include(${CMAKE_CURRENT_LIST_DIR}/../cmake/mp_demo_native.cmake)
    mp_add_native_demo(dual_core_bms LINK_ALGO
        main.c
        ${CMAKE_CURRENT_LIST_DIR}/../common/mp_relay_algo_demo.c
    )
else()
    message(FATAL_ERROR "dual_core_bms 须启用 NATIVE_SIM_BUILD 或 QEMU_SMP_BUILD")
endif()
