# tinyml_graph：quantize → fc → relu → mul → dequantize 最小图（native_sim）
cmake_minimum_required(VERSION 3.20)

if(NOT BM_DEMO_UNIFIED_BUILD)
    option(NATIVE_SIM_BUILD "Build for host native_sim" OFF)
endif()

set(BM_ENABLE_ALGORITHM ON CACHE BOOL "" FORCE)
set(BM_ENABLE_COMPONENT_TINYML ON CACHE BOOL "" FORCE)

if(BM_DEMO_UNIFIED_BUILD)
    set(TINYML_GRAPH_EXTRA_SOURCES
        "${BMELOD_ROOT}/Source/component/tinyml_adapter.c")
    set(TINYML_GRAPH_LIBS bm_core bm_algorithm)
else()
    set(TINYML_GRAPH_EXTRA_SOURCES)
    set(TINYML_GRAPH_LIBS bm_core bm_algorithm bm_component_tinyml)
endif()

if(BM_DEMO_NATIVE_SIM OR NATIVE_SIM_BUILD)
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(tinyml_graph_native C)
    endif()
    set(EXAMPLE_ENABLE_MODULE ON)
    include(../cmake/native_sim_example.cmake)
    bm_add_native_sim_example(tinyml_graph
        FRAMEWORK_LIBS ${TINYML_GRAPH_LIBS}
        EXTRA_SOURCES ${TINYML_GRAPH_EXTRA_SOURCES}
    )
else()
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(tinyml_graph C)
    endif()
    message(STATUS "tinyml_graph: native_sim only in this batch")
endif()
