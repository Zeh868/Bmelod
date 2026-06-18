cmake_minimum_required(VERSION 3.20)

if(NOT BM_DEMO_UNIFIED_BUILD)
    option(NATIVE_SIM_BUILD "Build for host native_sim" OFF)
endif()

set(EXAMPLE_ENABLE_MODULE ON)
set(EXAMPLE_ENABLE_CHANNEL OFF)
set(EXAMPLE_ENABLE_SHELL OFF)
set(EXAMPLE_ENABLE_WDG OFF)

file(GLOB INTERRUPT_DEMO_MODULES CONFIGURE_DEPENDS modules/*.c)
list(APPEND INTERRUPT_DEMO_MODULES
    ${CMAKE_CURRENT_SOURCE_DIR}/app_irq.c)

if(BM_DEMO_NATIVE_SIM OR NATIVE_SIM_BUILD)
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(interrupt_demo_native C)
    endif()
    include(../cmake/native_sim_example.cmake)
    bm_add_native_sim_example(interrupt_demo
        FRAMEWORK_LIBS bm_core bm_module
        EXTRA_SOURCES interrupt_timer.c ${INTERRUPT_DEMO_MODULES}
    )
else()
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(interrupt_demo C ASM)
    endif()
    include(../cmake/qemu_example.cmake)
    bm_add_qemu_example(interrupt_demo
        FRAMEWORK_LIBS bm_core bm_module
        EXTRA_SOURCES interrupt_timer.c ${INTERRUPT_DEMO_MODULES}
    )
endif()
