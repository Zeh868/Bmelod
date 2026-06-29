# QEMU test firmware build
# Usage from project root:
#   cmake -B build_qemu -S tests/qemu -DCMAKE_TOOLCHAIN_FILE=../../cmake/toolchain-arm-none-eabi.cmake
#   cmake --build build_qemu

cmake_minimum_required(VERSION 3.20)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/toolchain-arm-none-eabi.cmake
        CACHE FILEPATH "Toolchain file" FORCE)
endif()

project(qemu-tests C ASM)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(BM_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

set(_BM_QEMU_INCLUDES
    ${BM_ROOT}/include
    ${BM_ROOT}/include/hal
    ${BM_ROOT}/include/bm/common
    ${BM_ROOT}/include/bm/core
    ${BM_ROOT}/include/bm/hybrid
)

function(bm_qemu_target_includes target)
    target_include_directories(${target} PRIVATE ${_BM_QEMU_INCLUDES})
endfunction()

set(BM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_MODULE OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_CHANNEL OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_SHELL OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_WDG ON CACHE BOOL "" FORCE)
set(BM_ENABLE_HRT ON CACHE BOOL "" FORCE)
set(BM_ENABLE_STREAM ON CACHE BOOL "" FORCE)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    set(BM_BACKEND "qemu_cortex_m0" CACHE STRING "" FORCE)
    set(BM_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/bm_config_app.h" CACHE FILEPATH "" FORCE)

    add_subdirectory(${BM_ROOT} bmelod EXCLUDE_FROM_ALL)

    set(_BM_QEMU_BOOT
        ${BM_ROOT}/portable/boot/qemu_cortex_m0/startup_qemu_cm0.s
        ${BM_ROOT}/portable/boot/qemu_cortex_m0/crt0_qemu.c
    )
    set(_BM_QEMU_LINK
        -T${BM_ROOT}/portable/boot/qemu_cortex_m0/linker.ld
        -nostartfiles
        -Wl,--gc-sections
        --specs=nosys.specs
    )

    add_executable(qemu_smoke_cm0.elf
        test_qemu_smoke.c
        ${_BM_QEMU_BOOT}
    )
    target_link_libraries(qemu_smoke_cm0.elf PRIVATE
        "-Wl,--start-group"
        bm_backend_qemu_cortex_m0 bm_hal bm_core
        "-Wl,--end-group")
    bm_qemu_target_includes(qemu_smoke_cm0.elf)
    target_link_options(qemu_smoke_cm0.elf PRIVATE ${_BM_QEMU_LINK})

    add_executable(qemu_hybrid_smoke_cm0.elf
        test_hybrid_smoke.c
        ${_BM_QEMU_BOOT}
    )
    target_link_libraries(qemu_hybrid_smoke_cm0.elf PRIVATE
        "-Wl,--start-group"
        bm_backend_qemu_cortex_m0 bm_hrt bm_hal bm_core
        "-Wl,--end-group")
    bm_qemu_target_includes(qemu_hybrid_smoke_cm0.elf)
    target_link_options(qemu_hybrid_smoke_cm0.elf PRIVATE ${_BM_QEMU_LINK})

    add_executable(qemu_stream_smoke_cm0.elf
        test_qemu_stream_smoke.c
        ${_BM_QEMU_BOOT}
    )
    target_link_libraries(qemu_stream_smoke_cm0.elf PRIVATE
        "-Wl,--start-group"
        bm_backend_qemu_cortex_m0 bm_stream bm_hal bm_core
        "-Wl,--end-group")
    bm_qemu_target_includes(qemu_stream_smoke_cm0.elf)
    target_link_options(qemu_stream_smoke_cm0.elf PRIVATE ${_BM_QEMU_LINK})

    add_executable(qemu_uptime_smoke_cm0.elf
        test_qemu_uptime_smoke.c
        ${_BM_QEMU_BOOT}
    )
    target_link_libraries(qemu_uptime_smoke_cm0.elf PRIVATE
        "-Wl,--start-group"
        bm_backend_qemu_cortex_m0 bm_hal bm_core
        "-Wl,--end-group")
    bm_qemu_target_includes(qemu_uptime_smoke_cm0.elf)
    target_link_options(qemu_uptime_smoke_cm0.elf PRIVATE ${_BM_QEMU_LINK})

    find_program(QEMU_ARM qemu-system-arm)
    if(QEMU_ARM)
        message(STATUS "Found qemu-system-arm: ${QEMU_ARM}")

        add_custom_target(run-smoke-cm0
            COMMAND ${QEMU_ARM}
                -machine microbit
                -cpu cortex-m0
                -kernel $<TARGET_FILE:qemu_smoke_cm0.elf>
                --semihosting
                -display none
            DEPENDS qemu_smoke_cm0.elf
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Running Cortex-M0 smoke test in QEMU"
        )

        add_custom_target(run-hybrid-smoke-cm0
            COMMAND ${QEMU_ARM}
                -machine microbit
                -cpu cortex-m0
                -kernel $<TARGET_FILE:qemu_hybrid_smoke_cm0.elf>
                --semihosting
                -display none
            DEPENDS qemu_hybrid_smoke_cm0.elf
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Running hybrid HRT smoke test in QEMU"
        )

        add_custom_target(run-uptime-smoke-cm0
            COMMAND ${QEMU_ARM}
                -machine microbit
                -cpu cortex-m0
                -kernel $<TARGET_FILE:qemu_uptime_smoke_cm0.elf>
                --semihosting
                -display none
            DEPENDS qemu_uptime_smoke_cm0.elf
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Running uptime monotonic clock smoke test in QEMU"
        )
    else()
        message(WARNING "qemu-system-arm not found; run-smoke-cm0 target unavailable")
    endif()

else()
    message(FATAL_ERROR
        "tests/qemu: unsupported processor '${CMAKE_SYSTEM_PROCESSOR}' "
        "(use toolchain-arm-none-eabi; route-enabled tests live in the MP integration repo)")
endif()
