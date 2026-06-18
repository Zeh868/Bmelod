cmake_minimum_required(VERSION 3.20)

if(NOT BM_DEMO_UNIFIED_BUILD)
    option(NATIVE_SIM_BUILD "Build for host native_sim" OFF)
endif()

if(BM_DEMO_NATIVE_SIM OR NATIVE_SIM_BUILD)
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(multi_axis_sync_native C)
    endif()
    set(EXAMPLE_ENABLE_HRT ON)
    set(EXAMPLE_ENABLE_TICKER OFF)
    set(EXAMPLE_ENABLE_EXEC ON)
    set(EXAMPLE_ENABLE_SYNC ON)
    include(../cmake/native_sim_example.cmake)
    bm_add_native_sim_example(multi_axis_sync
        FRAMEWORK_LIBS bm_core bm_hrt bm_exec
    )
else()
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(multi_axis_sync C ASM)
    endif()
    set(EXAMPLE_ENABLE_HRT ON)
    set(EXAMPLE_ENABLE_TICKER OFF)
    set(EXAMPLE_ENABLE_EXEC ON)
    set(EXAMPLE_ENABLE_SYNC ON)
    include(../cmake/qemu_example.cmake)
    bm_add_qemu_example(multi_axis_sync
        FRAMEWORK_LIBS bm_core bm_hrt bm_exec
    )
endif()
