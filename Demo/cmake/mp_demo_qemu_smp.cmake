# MP 闭源 Demo — qemu_riscv64_smp 公共构建
include_guard(GLOBAL)

get_filename_component(_MP_QEMU_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(_MP_QEMU_DEMO_ROOT "${_MP_QEMU_CMAKE_DIR}/.." ABSOLUTE)
get_filename_component(_MP_QEMU_REPO_ROOT "${_MP_QEMU_DEMO_ROOT}/.." ABSOLUTE)

function(mp_demo_resolve_qemu_roots)
    set(BMEFLOD_MP_ROOT "${_MP_QEMU_REPO_ROOT}" CACHE PATH "Bmeflod MP extension root" FORCE)
    if(EXISTS "${BMEFLOD_MP_ROOT}/framework/Bmelod/CMakeLists.txt")
        set(BMELOD_CORE_ROOT "${BMEFLOD_MP_ROOT}/framework/Bmelod")
    elseif(EXISTS "${BMEFLOD_MP_ROOT}/../Bmelod/CMakeLists.txt")
        set(BMELOD_CORE_ROOT "${BMEFLOD_MP_ROOT}/../Bmelod")
    else()
        message(FATAL_ERROR "mp_demo_qemu_smp: cannot find Bmelod core")
    endif()
    get_filename_component(BMELOD_CORE_ROOT "${BMELOD_CORE_ROOT}" ABSOLUTE)
    set(BMELOD_CORE_ROOT "${BMELOD_CORE_ROOT}" CACHE PATH "" FORCE)
    set(BMELOD_ROOT "${BMELOD_CORE_ROOT}" CACHE PATH
        "Path to the Bmelod framework (core)" FORCE)
endfunction()

function(mp_ensure_qemu_smp_framework)
    if(TARGET bm_mp AND TARGET bm_backend_qemu_riscv64_smp)
        return()
    endif()

    mp_demo_resolve_qemu_roots()
    list(APPEND CMAKE_MODULE_PATH "${BMEFLOD_MP_ROOT}/cmake")
    if(NOT COMMAND bmeflod_mp_init)
        include(bmeflod_mp)
    endif()

    if(NOT DEFINED BM_SMP_BACKEND)
        set(BM_SMP_BACKEND "qemu_riscv64_smp" CACHE STRING "QEMU SMP backend id")
    endif()
    if(NOT DEFINED BM_SMP_BOOT_DIR)
        set(BM_SMP_BOOT_DIR "${BMEFLOD_MP_ROOT}/portable/boot/qemu_riscv64_smp" CACHE PATH
            "Boot sources for QEMU SMP backend")
    endif()

    set(BM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BM_ENABLE_MODULE ON CACHE BOOL "" FORCE)
    set(BM_ENABLE_ALGORITHM ON CACHE BOOL "" FORCE)
    set(BM_ENABLE_WDG ON CACHE BOOL "" FORCE)
    set(BM_ENABLE_HRT ON CACHE BOOL "" FORCE)
    set(BM_ENABLE_TICKER ON CACHE BOOL "" FORCE)
    set(BM_ENABLE_EXEC ON CACHE BOOL "" FORCE)
    set(BM_ENABLE_STREAM ON CACHE BOOL "" FORCE)
    set(BM_ENABLE_IPC ON CACHE BOOL "" FORCE)
    set(BM_SYNC_HAL_QEMU OFF CACHE BOOL "" FORCE)
    set(BM_SYNC_HAL_NATIVE OFF CACHE BOOL "" FORCE)
    set(BM_CONFIG_FILE
        "${_MP_QEMU_DEMO_ROOT}/common/bm_config_mp_demo_qemu_smp.h"
        CACHE FILEPATH "" FORCE)
    set(BM_BACKEND "${BM_SMP_BACKEND}" CACHE STRING "" FORCE)

    bmeflod_mp_init(CORE_ROOT "${BMELOD_CORE_ROOT}" MP_ROOT "${BMEFLOD_MP_ROOT}")

    if(TARGET bm_mp)
        target_compile_definitions(bm_mp PRIVATE BM_EXAMPLE_QEMU_SMP=1)
    endif()
    if(TARGET bm_config)
        target_compile_definitions(bm_config INTERFACE
            BM_HAL_CRITICAL_MASKS_STREAM_IRQ=1)
    endif()
endfunction()

function(mp_add_qemu_smp_demo TARGET)
    set(options LINK_ALGO)
    set(one_value_args)
    set(multi_value_args)
    cmake_parse_arguments(MP "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(MP_LINK_ALGO)
        set(BM_ENABLE_ALGORITHM ON CACHE BOOL "" FORCE)
    endif()
    mp_ensure_qemu_smp_framework()

    if(MP_LINK_ALGO AND NOT TARGET bmp_algo)
        bmeflod_mp_create_algo_library()
    endif()

    set(_bm_hal_target "bm_backend_${BM_SMP_BACKEND}")
    if(NOT TARGET ${_bm_hal_target})
        message(FATAL_ERROR "QEMU SMP backend target not found: ${_bm_hal_target}")
    endif()

    set(_bm_smp_boot_sources
        "${BM_SMP_BOOT_DIR}/startup_qemu_rv64_smp.S"
        "${BM_SMP_BOOT_DIR}/crt0_qemu_rv64_smp.c"
    )

    add_executable(${TARGET}.elf
        ${MP_UNPARSED_ARGUMENTS}
        "${BMELOD_CORE_ROOT}/Demo/common/example_support.c"
        "${BMELOD_CORE_ROOT}/Demo/common/hybrid_print.c"
        ${_bm_smp_boot_sources}
    )

    target_compile_definitions(${TARGET}.elf PRIVATE
        BM_EXAMPLE_QEMU_SMP=1
        BM_CONFIG_ENABLE_LOG=0
    )

    target_include_directories(${TARGET}.elf PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${_MP_QEMU_DEMO_ROOT}/common"
        "${BMELOD_CORE_ROOT}/Demo/common"
        "${BMELOD_CORE_ROOT}/include"
        "${BMELOD_CORE_ROOT}/include/bm/common"
        "${BMELOD_CORE_ROOT}/include/bm/core"
        "${BMELOD_CORE_ROOT}/include/bm/hybrid"
        "${BMEFLOD_MP_ROOT}/include"
        "${BMEFLOD_MP_ROOT}/include/bm/mp"
        "${BMELOD_CORE_ROOT}/include/hal"
        "${BMELOD_CORE_ROOT}/include/drv"
    )

    set(_fw_libs
        bm_config bm_core bm_module bm_hrt bm_ticker bm_exec
        bm_ipc bm_mp bm_wdg bm_resource
    )
    if(MP_LINK_ALGO)
        list(APPEND _fw_libs bmp_algo)
    endif()

    target_link_libraries(${TARGET}.elf PRIVATE
        "-Wl,--start-group"
        ${_bm_hal_target}
        bm_hal
        ${_fw_libs}
        "-Wl,--end-group"
        m
    )
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -print-file-name=nosys.specs
        OUTPUT_VARIABLE _bm_nosys_specs
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(_bm_nosys_specs AND EXISTS "${_bm_nosys_specs}")
        set(_bm_nosys_link "-specs=nosys.specs")
    else()
        set(_bm_nosys_link "-lnosys")
    endif()
    target_link_options(${TARGET}.elf PRIVATE
        "-T${BM_SMP_BOOT_DIR}/linker_smp.ld"
        "-nostartfiles"
        "-Wl,--gc-sections"
        "${_bm_nosys_link}"
    )
endfunction()
