# stream_array_mvdr：多通道块流 + MVDR + pipeline LPF（M7 K2）
cmake_minimum_required(VERSION 3.20)

if(NOT BM_DEMO_UNIFIED_BUILD)
    option(NATIVE_SIM_BUILD "Build for host native_sim" OFF)
endif()

set(BM_ENABLE_ALGORITHM ON CACHE BOOL "" FORCE)
set(BM_ENABLE_COMPONENT_DIAGNOSTICS ON CACHE BOOL "" FORCE)
set(BM_ENABLE_COMPONENT_STREAM ON CACHE BOOL "" FORCE)
set(BM_ENABLE_PIPELINE ON CACHE BOOL "" FORCE)
set(BM_ENABLE_STREAM ON CACHE BOOL "" FORCE)

# 统一 Demo 入口在 add_subdirectory 之前已配置 bmelod，需补链 stream 前端源
if(BM_DEMO_UNIFIED_BUILD AND TARGET bm_component_diagnostics)
    get_property(_bm_stream_diag_appended GLOBAL PROPERTY BM_DEMO_STREAM_DIAG_APPENDED)
    if(NOT _bm_stream_diag_appended)
        target_sources(bm_component_diagnostics PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/../../Source/component/audio_array_frontend.c")
        set_property(GLOBAL PROPERTY BM_DEMO_STREAM_DIAG_APPENDED ON)
    endif()
endif()

if(BM_DEMO_NATIVE_SIM OR NATIVE_SIM_BUILD)
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(stream_array_mvdr_native C)
    endif()
    set(EXAMPLE_ENABLE_MODULE ON)
    set(EXAMPLE_ENABLE_HRT ON)
    set(EXAMPLE_ENABLE_TICKER ON)
    set(EXAMPLE_ENABLE_EXEC ON)
    include(../cmake/native_sim_example.cmake)
    file(GLOB ARRAY_MVDR_MODULES CONFIGURE_DEPENDS modules/*.c)
    set(_ARRAY_MVDR_EXTRA ${ARRAY_MVDR_MODULES})
    bm_add_native_sim_example(stream_array_mvdr
        FRAMEWORK_LIBS bm_core bm_module bm_hrt bm_ticker bm_exec
                       bm_algorithm bm_component_diagnostics bm_pipeline
        EXTRA_SOURCES ${_ARRAY_MVDR_EXTRA}
    )
else()
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(stream_array_mvdr C ASM)
    endif()
    set(EXAMPLE_ENABLE_MODULE ON)
    set(EXAMPLE_ENABLE_HRT ON)
    set(EXAMPLE_ENABLE_TICKER ON)
    set(EXAMPLE_ENABLE_EXEC ON)
    include(../cmake/qemu_example.cmake)
    file(GLOB ARRAY_MVDR_MODULES CONFIGURE_DEPENDS modules/*.c)
    set(_ARRAY_MVDR_EXTRA ${ARRAY_MVDR_MODULES})
    bm_add_qemu_example(stream_array_mvdr
        FRAMEWORK_LIBS bm_core bm_module bm_hrt bm_ticker bm_exec
                       bm_algorithm bm_component_diagnostics bm_pipeline
        EXTRA_SOURCES ${_ARRAY_MVDR_EXTRA}
    )
    target_link_libraries(stream_array_mvdr.elf PRIVATE m)
endif()
