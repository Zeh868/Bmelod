# stream_bms_pipeline：bm_stream + bm_pipeline 线性链 + 库仑 SOC（M6 E1）
cmake_minimum_required(VERSION 3.20)

if(NOT BM_DEMO_UNIFIED_BUILD)
    option(NATIVE_SIM_BUILD "Build for host native_sim" OFF)
endif()

set(BM_ENABLE_ALGORITHM ON CACHE BOOL "" FORCE)
set(BM_ENABLE_PIPELINE ON CACHE BOOL "" FORCE)
set(BM_ENABLE_STREAM ON CACHE BOOL "" FORCE)

if(BM_DEMO_NATIVE_SIM OR NATIVE_SIM_BUILD)
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(stream_bms_pipeline_native C)
    endif()
    set(EXAMPLE_ENABLE_MODULE ON)
    set(EXAMPLE_ENABLE_HRT ON)
    set(EXAMPLE_ENABLE_TICKER ON)
    set(EXAMPLE_ENABLE_EXEC ON)
    include(../cmake/native_sim_example.cmake)
    file(GLOB BMS_MODULES CONFIGURE_DEPENDS modules/*.c)
    bm_add_native_sim_example(stream_bms_pipeline
        FRAMEWORK_LIBS bm_core bm_module bm_hrt bm_ticker bm_exec bm_algorithm bm_pipeline
        EXTRA_SOURCES ${BMS_MODULES}
    )
else()
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(stream_bms_pipeline C ASM)
    endif()
    set(EXAMPLE_ENABLE_MODULE ON)
    set(EXAMPLE_ENABLE_HRT ON)
    set(EXAMPLE_ENABLE_TICKER ON)
    set(EXAMPLE_ENABLE_EXEC ON)
    include(../cmake/qemu_example.cmake)
    file(GLOB BMS_MODULES CONFIGURE_DEPENDS modules/*.c)
    bm_add_qemu_example(stream_bms_pipeline
        FRAMEWORK_LIBS bm_core bm_module bm_hrt bm_ticker bm_exec bm_algorithm bm_pipeline
        EXTRA_SOURCES ${BMS_MODULES}
    )
endif()
