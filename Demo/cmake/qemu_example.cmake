set(BMELOD_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH
    "Path to the bmelod-baremetal framework")

include(${CMAKE_CURRENT_LIST_DIR}/demo_example_common.cmake)

function(bm_demo_apply_qemu_example_flags)
    if(BM_DEMO_UNIFIED_BUILD)
        set(BM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(BM_BUILD_ALL_COMPONENTS ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_MODULE ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_SHELL OFF CACHE BOOL "" FORCE)
        set(BM_ENABLE_WDG ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_HRT ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_TICKER ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_EXEC ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_SYNC ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_STREAM ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_PIPELINE ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_ALGORITHM ON CACHE BOOL "" FORCE)
        set(BM_SYNC_HAL_QEMU ON CACHE BOOL "" FORCE)
        set(BM_SYNC_HAL_NATIVE OFF CACHE BOOL "" FORCE)
        set(BM_BACKEND "qemu_cortex_m0" CACHE STRING "" FORCE)
        return()
    endif()

    set(BM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BM_ENABLE_MODULE ${EXAMPLE_ENABLE_MODULE} CACHE BOOL "" FORCE)
    set(BM_ENABLE_SHELL ${EXAMPLE_ENABLE_SHELL} CACHE BOOL "" FORCE)
    set(BM_ENABLE_WDG ${EXAMPLE_ENABLE_WDG} CACHE BOOL "" FORCE)
    set(BM_ENABLE_HRT ${EXAMPLE_ENABLE_HRT} CACHE BOOL "" FORCE)
    set(BM_ENABLE_TICKER ${EXAMPLE_ENABLE_TICKER} CACHE BOOL "" FORCE)
    set(BM_ENABLE_EXEC ${EXAMPLE_ENABLE_EXEC} CACHE BOOL "" FORCE)
    set(BM_ENABLE_SYNC ${EXAMPLE_ENABLE_SYNC} CACHE BOOL "" FORCE)
    if(EXAMPLE_ENABLE_SYNC)
        set(BM_SYNC_HAL_QEMU ON CACHE BOOL "" FORCE)
        set(BM_SYNC_HAL_NATIVE OFF CACHE BOOL "" FORCE)
    else()
        set(BM_SYNC_HAL_QEMU OFF CACHE BOOL "" FORCE)
    endif()
    set(BM_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/bm_config_app.h" CACHE FILEPATH "" FORCE)
    set(BM_BACKEND "qemu_cortex_m0" CACHE STRING "" FORCE)
endfunction()

function(bm_demo_ensure_qemu)
    if(BM_DEMO_QEMU_INIT)
        return()
    endif()
    set(BM_DEMO_QEMU_INIT ON CACHE INTERNAL "" FORCE)
    bm_demo_apply_qemu_example_flags()
    if(BM_DEMO_UNIFIED_BUILD)
        set(BM_CONFIG_FILE "" CACHE FILEPATH "" FORCE)
    endif()
    add_subdirectory("${BMELOD_ROOT}" bmelod EXCLUDE_FROM_ALL)
endfunction()

function(bm_add_qemu_example TARGET)
    set(options)
    set(one_value_args)
    set(multi_value_args FRAMEWORK_LIBS EXTRA_SOURCES)
    cmake_parse_arguments(EX "${options}" "${one_value_args}"
        "${multi_value_args}" ${ARGN})

    add_executable(${TARGET}.elf
        main.c
        "${BMELOD_ROOT}/Demo/common/example_support.c"
        "${BMELOD_ROOT}/Demo/common/hybrid_print.c"
        "${BMELOD_ROOT}/portable/boot/qemu_cortex_m0/startup_qemu_cm0.s"
        "${BMELOD_ROOT}/portable/boot/qemu_cortex_m0/crt0_qemu.c"
        ${EX_EXTRA_SOURCES}
    )

    target_compile_definitions(${TARGET}.elf PRIVATE
        BM_EXAMPLE_QEMU=1
        BM_CONFIG_ENABLE_LOG=0
    )

    target_include_directories(${TARGET}.elf PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${BMELOD_ROOT}/Demo/common"
        "${BMELOD_ROOT}/include"
        "${BMELOD_ROOT}/include/bm/common"
        "${BMELOD_ROOT}/include/bm/core"
        "${BMELOD_ROOT}/include/bm/hybrid"
        "${BMELOD_ROOT}/include/hal"
        "${BMELOD_ROOT}/include/drv"
        "${BMELOD_ROOT}/portable/sim/native"
    )
    target_link_libraries(${TARGET}.elf PRIVATE
        bm_backend_qemu_cortex_m0 bm_hal ${EX_FRAMEWORK_LIBS})
    if(TARGET bm_port_arch_armv6m)
        target_link_libraries(${TARGET}.elf PRIVATE
            -Wl,--whole-archive bm_port_arch_armv6m -Wl,--no-whole-archive)
    endif()
    target_link_options(${TARGET}.elf PRIVATE
        "-T${BMELOD_ROOT}/portable/boot/qemu_cortex_m0/linker.ld"
        "-nostartfiles"
        "-Wl,--gc-sections"
        "--specs=nosys.specs"
    )
    if(BM_DEMO_UNIFIED_BUILD)
        bm_demo_apply_example_config(${TARGET}.elf)
    endif()
endfunction()

if(NOT BM_DEMO_UNIFIED_BUILD)
    bm_demo_ensure_qemu()
endif()
