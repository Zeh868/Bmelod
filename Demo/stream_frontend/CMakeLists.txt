# stream_frontend：bm_stream_frontend 包装 + 块级 RMS（M7 K2）
cmake_minimum_required(VERSION 3.20)

if(NOT BM_DEMO_UNIFIED_BUILD)
    option(NATIVE_SIM_BUILD "Build for host native_sim" OFF)
endif()

set(BM_ENABLE_ALGORITHM ON CACHE BOOL "" FORCE)
set(BM_ENABLE_COMPONENT_DIAGNOSTICS ON CACHE BOOL "" FORCE)
set(BM_ENABLE_COMPONENT_STREAM ON CACHE BOOL "" FORCE)

if(BM_DEMO_NATIVE_SIM OR NATIVE_SIM_BUILD)
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(stream_frontend_native C)
    endif()
    set(EXAMPLE_ENABLE_MODULE ON)
    set(EXAMPLE_ENABLE_HRT ON)
    set(EXAMPLE_ENABLE_TICKER ON)
    set(EXAMPLE_ENABLE_EXEC ON)
    include(../cmake/native_sim_example.cmake)
    file(GLOB STREAM_FE_MODULES CONFIGURE_DEPENDS modules/*.c)
    bm_add_native_sim_example(stream_frontend
        FRAMEWORK_LIBS bm_core bm_module bm_hrt bm_ticker bm_exec
                       bm_algorithm bm_component_diagnostics
        EXTRA_SOURCES ${STREAM_FE_MODULES}
    )
else()
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(stream_frontend C ASM)
    endif()
    set(EXAMPLE_ENABLE_MODULE ON)
    set(EXAMPLE_ENABLE_HRT ON)
    set(EXAMPLE_ENABLE_TICKER ON)
    set(EXAMPLE_ENABLE_EXEC ON)
    include(../cmake/qemu_example.cmake)
    file(GLOB STREAM_FE_MODULES CONFIGURE_DEPENDS modules/*.c)
    bm_add_qemu_example(stream_frontend
        FRAMEWORK_LIBS bm_core bm_module bm_hrt bm_ticker bm_exec
                       bm_algorithm bm_component_diagnostics
        EXTRA_SOURCES ${STREAM_FE_MODULES}
    )
    target_link_libraries(stream_frontend.elf PRIVATE m)
endif()
