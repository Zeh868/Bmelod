cmake_minimum_required(VERSION 3.20)

if(NOT BM_DEMO_UNIFIED_BUILD)
    option(NATIVE_SIM_BUILD "Build for host native_sim" OFF)
endif()

if(BM_DEMO_NATIVE_SIM OR NATIVE_SIM_BUILD)
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(hrt_servo_stub_native C)
    endif()
    set(EXAMPLE_ENABLE_MODULE ON)
    set(EXAMPLE_ENABLE_HRT ON)
    set(EXAMPLE_ENABLE_TICKER ON)
    set(EXAMPLE_ENABLE_EXEC ON)
    set(EXAMPLE_ENABLE_SYNC OFF)
    include(../cmake/native_sim_example.cmake)
    file(GLOB HRT_SERVO_MODULES CONFIGURE_DEPENDS modules/*.c)
    bm_add_native_sim_example(hrt_servo_stub
        FRAMEWORK_LIBS bm_core bm_module bm_hrt bm_ticker bm_exec
        EXTRA_SOURCES ${HRT_SERVO_MODULES}
    )
else()
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(hrt_servo_stub C ASM)
    endif()
    set(EXAMPLE_ENABLE_MODULE ON)
    set(EXAMPLE_ENABLE_HRT ON)
    set(EXAMPLE_ENABLE_TICKER ON)
    set(EXAMPLE_ENABLE_EXEC ON)
    set(EXAMPLE_ENABLE_SYNC OFF)
    include(../cmake/qemu_example.cmake)
    file(GLOB HRT_SERVO_MODULES CONFIGURE_DEPENDS modules/*.c)
    bm_add_qemu_example(hrt_servo_stub
        FRAMEWORK_LIBS bm_core bm_module bm_hrt bm_ticker bm_exec
        EXTRA_SOURCES ${HRT_SERVO_MODULES}
    )
endif()
