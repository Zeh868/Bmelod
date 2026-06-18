# power_buck_loop：Buck 双环电源组件 + native_sim 一阶 plant
cmake_minimum_required(VERSION 3.20)

if(NOT BM_DEMO_UNIFIED_BUILD)
    option(NATIVE_SIM_BUILD "Build for host native_sim" OFF)
endif()

set(BM_ENABLE_ALGORITHM ON CACHE BOOL "" FORCE)
set(BM_ENABLE_COMPONENT_POWER ON CACHE BOOL "" FORCE)

if(BM_DEMO_NATIVE_SIM OR NATIVE_SIM_BUILD)
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(power_buck_loop_native C)
    endif()
    set(EXAMPLE_ENABLE_MODULE ON)
    set(EXAMPLE_ENABLE_HRT ON)
    set(EXAMPLE_ENABLE_TICKER ON)
    set(EXAMPLE_ENABLE_EXEC ON)
    include(../cmake/native_sim_example.cmake)
    file(GLOB POWER_MODULES CONFIGURE_DEPENDS modules/*.c)
    bm_add_native_sim_example(power_buck_loop
        FRAMEWORK_LIBS bm_core bm_module bm_hrt bm_ticker bm_exec
                       bm_algorithm bm_component_power
        EXTRA_SOURCES ${POWER_MODULES}
    )
else()
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(power_buck_loop C ASM)
    endif()
    set(EXAMPLE_ENABLE_MODULE ON)
    set(EXAMPLE_ENABLE_HRT ON)
    set(EXAMPLE_ENABLE_TICKER ON)
    set(EXAMPLE_ENABLE_EXEC ON)
    include(../cmake/qemu_example.cmake)
    file(GLOB POWER_MODULES CONFIGURE_DEPENDS modules/*.c)
    bm_add_qemu_example(power_buck_loop
        FRAMEWORK_LIBS bm_core bm_module bm_hrt bm_ticker bm_exec
                       bm_algorithm bm_component_power
        EXTRA_SOURCES ${POWER_MODULES}
    )
endif()
