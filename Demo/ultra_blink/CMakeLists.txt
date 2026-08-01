cmake_minimum_required(VERSION 3.20)

if(NOT BM_DEMO_UNIFIED_BUILD)
    option(NATIVE_SIM_BUILD "Build for host native_sim" OFF)
endif()

set(EXAMPLE_ENABLE_MODULE OFF)
set(EXAMPLE_ENABLE_SHELL OFF)
set(EXAMPLE_ENABLE_WDG OFF)

if(BM_DEMO_NATIVE_SIM OR NATIVE_SIM_BUILD)
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(ultra_blink_native C)
    endif()
    include(../cmake/native_sim_example.cmake)
    # bm_ultra_*/bm_log 符号在 bm_core（bm_ultra.c/bm_log.c），需显式链接
    bm_add_native_sim_example(ultra_blink FRAMEWORK_LIBS bm_core)
else()
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(ultra_blink C ASM)
    endif()
    include(../cmake/qemu_example.cmake)
    bm_add_qemu_example(ultra_blink FRAMEWORK_LIBS bm_core)
endif()
