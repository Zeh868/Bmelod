cmake_minimum_required(VERSION 3.20)

if(NOT BM_DEMO_UNIFIED_BUILD)
    option(NATIVE_SIM_BUILD "Build for host native_sim" OFF)
endif()

set(EXAMPLE_ENABLE_MODULE ON)
set(EXAMPLE_ENABLE_CHANNEL OFF)
set(EXAMPLE_ENABLE_SHELL OFF)
set(EXAMPLE_ENABLE_WDG ON)

file(GLOB FULL_SYSTEM_MODULES CONFIGURE_DEPENDS modules/*.c)
list(APPEND FULL_SYSTEM_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/app_events.c)

if(BM_DEMO_NATIVE_SIM OR NATIVE_SIM_BUILD)
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(full_system_native C)
    endif()
    include(../cmake/native_sim_example.cmake)
    bm_add_native_sim_example(full_system
        FRAMEWORK_LIBS bm_core bm_module bm_wdg
        EXTRA_SOURCES ${FULL_SYSTEM_MODULES}
    )
else()
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(full_system C ASM)
    endif()
    include(../cmake/qemu_example.cmake)
    bm_add_qemu_example(full_system
        FRAMEWORK_LIBS bm_core bm_module bm_wdg
        EXTRA_SOURCES ${FULL_SYSTEM_MODULES}
    )
endif()
